#include <stdio.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/util/queue.h"
#include "pico/platform.h"
#include "pico/multicore.h"

#include "badger.h"
#include "lfs_pico_flash.h"
#include "screen_page.h"
#include "usb.h"

#define README_CONTENTS "This is the default file"

extern void prepare_usb_filesystem(lfs_t *lfs, struct usb_msc_disk *msc_disk);
extern int do_flash_update(lfs_t *lfs);

queue_t msg_queue;

enum msg_type {
	MSG_TYPE_NONE = 0,
	MSG_TYPE_CORE1_LAUNCHED,
	MSG_TYPE_USB_CONNECTING,
	MSG_TYPE_USB_TIMEOUT,
	MSG_TYPE_USB_CONNECTED,
	MSG_TYPE_USB_DISCONNECTED,
	MSG_TYPE_MSC_UNMOUNTED,
	MSG_TYPE_CDC_CONNECTED,
	MSG_TYPE_POWER_OFF,
	MSG_TYPE_BTNS_CHANGED,
};

struct msg {
	enum msg_type type;
};

static void usb_connect_cb()
{
	queue_add_blocking(&msg_queue, &(struct msg){ .type = MSG_TYPE_USB_CONNECTED });
}

static void usb_disconnect_cb()
{
	queue_add_blocking(&msg_queue, &(struct msg){ .type = MSG_TYPE_USB_DISCONNECTED });
}

static void usb_msc_start_stop_cb(void *user, uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
	if (load_eject && !start) {
		queue_add_blocking(&msg_queue, &(struct msg){ .type = MSG_TYPE_MSC_UNMOUNTED });
	}
}

static void usb_cdc_line_state_cb(void *user, uint8_t itf, bool dtr, bool rts)
{
	if (dtr && rts) {
		queue_add_blocking(&msg_queue, &(struct msg){ .type = MSG_TYPE_CDC_CONNECTED });
	}
}

static struct usb_opt usb_opt = {
	.user = NULL,
	.connect_cb = usb_connect_cb,
	.disconnect_cb = usb_disconnect_cb,
	.cdc = {
		.line_state_cb = usb_cdc_line_state_cb,
	},
	.msc = {
		.disk = {
			.vid = "usedbytes",
			.pid = "usedbadger mass storage",
			.rev = "1.0",
		},
		.start_stop_cb = usb_msc_start_stop_cb,
	},
};

void core1_main()
{
	multicore_lockout_victim_init();
	queue_add_blocking(&msg_queue, &(struct msg){ .type = MSG_TYPE_CORE1_LAUNCHED });

	usb_main(&usb_opt);

	// Will never reach here
	return;
}

void launch_usb()
{
	multicore_launch_core1(core1_main);
}

static int64_t usb_connect_timeout(alarm_id_t id, void *d)
{
	queue_add_blocking(&msg_queue, &(struct msg){ .type = MSG_TYPE_USB_TIMEOUT });

	return 0;
}

static int power_ref = 0;
static alarm_id_t power_alarm;
#define POWER_DOWN_MS 1000
static int64_t power_timeout(alarm_id_t id, void *d)
{
	queue_add_blocking(&msg_queue, &(struct msg){ .type = MSG_TYPE_POWER_OFF });

	return 0;
}

static void power_ref_get()
{
	power_ref++;
	gpio_put(BADGER_PIN_ENABLE_3V3, 1);

	cancel_alarm(power_alarm);
}

static void power_ref_put()
{
	power_ref--;

	if (power_ref == 0) {
		power_alarm = add_alarm_in_ms(POWER_DOWN_MS, power_timeout, NULL, true);
	}
}

enum lfs_state {
	LFS_STATE_ERROR = -1,
	LFS_STATE_NONE = 0,
	LFS_STATE_MOUNTED,
	LFS_STATE_UNMOUNTED,
};

struct lfs_ctx {
	struct lfs_config cfg;
	struct lfs_flash_cfg priv;
	lfs_t lfs;
	enum lfs_state state;
};

// Leaves the filesystem mounted on success
static int lfs_format_init(struct lfs_ctx *ctx)
{
	lfs_file_t lfp = { 0 };

	int res = lfs_format(&ctx->lfs, &ctx->cfg);
	if (res) {
		return res;
	}

	res = lfs_mount(&ctx->lfs, &ctx->cfg);
	if (res) {
		return res;
	}

	res = lfs_file_open(&ctx->lfs, &lfp, "readme.txt", LFS_O_RDWR | LFS_O_CREAT);
	if (res) {
		goto err_unmount;
	}

	// TODO: Might fail?
	lfs_file_write(&ctx->lfs, &lfp, README_CONTENTS, strlen(README_CONTENTS));

	res = lfs_file_close(&ctx->lfs, &lfp);
	if (res) {
		goto err_unmount;
	}

	return 0;

err_unmount:
	lfs_unmount(&ctx->lfs);
	return res;
}

int lfs_ctx_mount(struct lfs_ctx *ctx, bool multicore)
{
	int res;

	if (ctx->state == LFS_STATE_ERROR) {
		return -1;
	}

	if (ctx->state == LFS_STATE_NONE) {
		critical_section_init(&ctx->priv.lock);
	}

	if (ctx->state == LFS_STATE_MOUNTED) {
	       if (multicore == ctx->priv.multicore) {
			// Nothing to do
			return 0;
		} else {
			res = lfs_unmount(&ctx->lfs);
			if (res) {
				ctx->state = LFS_STATE_ERROR;
				return -1;
			}
		}
	}

	ctx->priv.multicore = multicore;

	res = lfs_mount(&ctx->lfs, &ctx->cfg);
	if (res) {
		if (ctx->state == LFS_STATE_NONE) {
			res = lfs_format_init(ctx);
			if (!res) {
				// Success
				ctx->state = LFS_STATE_MOUNTED;
				return 0;
			}
		}

		ctx->state = LFS_STATE_ERROR;
		return res;
	}

	ctx->state = LFS_STATE_MOUNTED;
	return 0;
}

int lfs_ctx_unmount(struct lfs_ctx *ctx)
{
	int res;

	if (ctx->state == LFS_STATE_ERROR) {
		return -1;
	}

	if (ctx->state == LFS_STATE_UNMOUNTED) {
		return 0;
	}

	if (ctx->state != LFS_STATE_MOUNTED) {
		return -1;
	}

	res = lfs_unmount(&ctx->lfs);
	if (res) {
		ctx->state = LFS_STATE_ERROR;
		return res;
	}

	ctx->state = LFS_STATE_UNMOUNTED;
	return 0;
}

char *read_file(lfs_t *lfs, const char *path)
{
	struct lfs_info st;
	lfs_file_t fp;
	int res;
	char *buf;

	res = lfs_stat(lfs, path, &st);
	if (res != 0) {
		return NULL;
	}

	printf("stat %s: %d\n", path, st.size);

	buf = malloc(st.size);

	res = lfs_file_open(lfs, &fp, path, LFS_O_RDONLY);
	if (res) {
		free(buf);
		return NULL;
	}

	res = lfs_file_read(lfs, &fp, buf, st.size);
	lfs_file_close(lfs, &fp);
	if (res != st.size) {
		free(buf);
		return NULL;
	}

	return buf;
}

int parse_img(lfs_t *lfs, struct screen_page_item *item, char *buf)
{
	char *tok, *tmp;

	item->type = PAGE_ITEM_TYPE_IMAGE;

	// img
	tok = strsep(&buf, " ");
	printf("first tok: %s\n", tok);
	tmp = index(tok, '.');
	if (tmp) {
		printf("font: %s\n", tmp);
	}

	tok = strsep(&buf, " ");
	printf("width tok: %s\n", tok);
	if (sscanf(tok, "%d", &item->image.width) != 1) {
		printf("failed parsing img.width\n");
		return -1;
	}

	tok = strsep(&buf, " ");
	printf("height tok: %s\n", tok);
	if (sscanf(tok, "%d", &item->image.height) != 1) {
		printf("failed parsing image.height\n");
		return -1;
	}

	tok = strsep(&buf, " ");
	printf("path tok: %s\n", tok);
	item->image.data = (uint8_t *)read_file(lfs, tok);
	if (!item->image.data) {
		printf("failed reading image.data\n");
		return -1;
	}

	printf("Parsed image: %d %d '%08x'\n",
			item->image.width, item->image.height, *(uint32_t *)item->image.data);

	return 0;
}

int parse_text(lfs_t *lfs, struct screen_page_item *item, char *buf)
{
	char *tok, *tmp;

	item->type = PAGE_ITEM_TYPE_TEXT;

	// text.font
	tok = strsep(&buf, " ");
	printf("first tok: %s\n", tok);
	tmp = index(tok, '.');
	if (tmp) {
		printf("font: %s\n", tmp);
	}

	tok = strsep(&buf, " ");
	printf("size tok: %s\n", tok);
	if (sscanf(tok, "%f", &item->text.size) != 1) {
		printf("failed parsing text.size\n");
		return -1;
	}

	tok = strsep(&buf, " ");
	printf("color tok: %s\n", tok);
	if (sscanf(tok, "%hhd", &item->text.color) != 1) {
		printf("failed parsing text.color\n");
		return -1;
	}

	tok = strsep(&buf, " ");
	printf("thickness tok: %s\n", tok);
	if (sscanf(tok, "%hhd", &item->text.thickness) != 1) {
		printf("failed parsing text.thickness\n");
		return -1;
	}

	tok = strsep(&buf, "\n");
	printf("text tok: %s\n", tok);
	item->text.text = malloc(strlen(tok) + 1);
	strcpy(item->text.text, tok);

	printf("Parsed text: %1.3f %d %d '%s'\n",
			item->text.size, item->text.color, item->text.thickness, item->text.text);

	return 0;
}

void screen_page_free(struct screen_page *page)
{
	if (!page) {
		return;
	}

	for (int i = 0; i < page->n_items; i++) {
		struct screen_page_item *item = &page->items[i];
		switch (item->type) {
		case PAGE_ITEM_TYPE_IMAGE:
			free(item->image.data);
			break;
		case PAGE_ITEM_TYPE_TEXT:
			free(item->text.text);
			break;
		}
	}

	free(page);
}

struct screen_page *parse_file(lfs_t *lfs, const char *path)
{
	int res;
	struct screen_page *page = NULL;

	char *buf = read_file(lfs, path);
	if (!buf) {
		return NULL;
	}

	int nlines = 0;
	char *p = buf;
	while (*p) {
		if (*p == '\n') {
			nlines++;
		}
		p++;
	}
	printf("nlines: %d\n", nlines);

	page = calloc(1, sizeof(*page));
	page->items = calloc(nlines, sizeof(*page->items));

	char *next = buf, *line;
	while (next) {
		line = strsep(&next, "\n");
		printf("item: %d, %s\n", page->n_items, line);

		if (strncmp(line, "text", strlen("text")) == 0) {
			res = parse_text(lfs, &page->items[page->n_items], line);
		} else if (strncmp(line, "img", strlen("img")) == 0) {
			res = parse_img(lfs, &page->items[page->n_items], line);
		} else {
			printf("skip unknown type: '%s'\n", line);
			continue;
		}

		if (res) {
			break;
		}

		page_item_calculate_size(&page->items[page->n_items]);

		page->n_items++;
	}

	free(buf);

	printf("res: %d, n_items: %d\n", res, page->n_items);

	if (res) {
		screen_page_free(page);
		page = NULL;
	}

	return page;
}

static int64_t button_changed(alarm_id_t id, void *d)
{
	queue_try_add(&msg_queue, &(struct msg){ .type = MSG_TYPE_BTNS_CHANGED });

	return 0;
}

#define BUTTON_DEBOUNCE_US 10000
static void gpio_irq_cb(uint gpio, uint32_t events)
{
	static uint32_t ts = 0;

	uint32_t now = time_us_32();
	int32_t diff = now - ts;
	ts = now;

	if (diff >= BUTTON_DEBOUNCE_US) {
		add_alarm_in_us(BUTTON_DEBOUNCE_US, button_changed, NULL, true);
	}
}

int main() {
	struct screen_page empty_page = {
		.n_items = 2,
		.items = (struct screen_page_item[]){
			{
				.type = PAGE_ITEM_TYPE_TEXT,
				.text = {
					.size = 1.0,
					.color = 0,
					.thickness = 4,
					.text = "Error!",
				},
			},
			{
				.type = PAGE_ITEM_TYPE_TEXT,
				.text = {
					.size = 0.8,
					.color = 3,
					.thickness = 2,
					.text = "No main.txt content?",
				},
			},
		},
	};

	static struct lfs_ctx lfs_ctx = {
		.cfg = {
			.context = &lfs_ctx.priv,
			// block device operations
			.read  = lfs_flash_read,
			.prog  = lfs_flash_prog,
			.erase = lfs_flash_erase,
			.sync  = lfs_flash_sync,

			// block device configuration
			.read_size = 1,
			.prog_size = 256,
			.block_size = 4096,
			.block_count = 16,
			.cache_size = 256,
			.lookahead_size = 4,
			.block_cycles = 500,
		},
		.priv = {
			// FIXME: The linker doesn't know anything about this
			.base = PICO_FLASH_SIZE_BYTES - (4096 * 16),
		},
		.state = LFS_STATE_NONE,
	};

	enum {
		USB_STATE_NONE,
		USB_STATE_WAITING,
		USB_STATE_MOUNTED,
		USB_STATE_UNMOUNTED,
	} usb_state = USB_STATE_NONE;
	bool multicore = false;
	int res;

	uint32_t buttons = 0;
	uint32_t pressed = 0;
	uint32_t released = 0;

	if (badger_pressed_to_wake(BADGER_PIN_DOWN)) {
		buttons |= (1 << BADGER_PIN_DOWN);
	}

	badger_init();
	badger_led(255);

	// Hold power alive for boot-up
	power_ref_get();

	gpio_set_irq_enabled_with_callback(BADGER_PIN_A, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_irq_cb);
	gpio_set_irq_enabled(BADGER_PIN_B, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
	gpio_set_irq_enabled(BADGER_PIN_C, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
	gpio_set_irq_enabled(BADGER_PIN_D, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
	gpio_set_irq_enabled(BADGER_PIN_UP, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
	gpio_set_irq_enabled(BADGER_PIN_DOWN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);


	queue_init(&msg_queue, sizeof(struct msg), 8);

	// Unconditionally mount LFS to start with
	res = lfs_ctx_mount(&lfs_ctx, false);
	if (res) {
		// What do?
	}

	if (gpio_get(BADGER_PIN_VBUS_DETECT)) {
		// Hold power until USB has had a chance to connect
		power_ref_get();
		prepare_usb_filesystem(&lfs_ctx.lfs, &usb_opt.msc.disk);

		// Flash access not safe any more, need to remount as multicore
		lfs_ctx_unmount(&lfs_ctx);
		usb_state = USB_STATE_WAITING;

		launch_usb();
	} else {
		usb_state = USB_STATE_UNMOUNTED;
	}

	// TODO: Always?
	badger_pen(15);
	badger_clear();
	badger_pen(0);
	badger_update_speed(2);
	/*
	badger_update(true);
	*/

	// Boot-up done
	power_ref_put();

	// TODO: Implement buttons
	bool buttons_pressed = false;
	// TODO: Should really just do the refresh while still holding the reference
	bool refresh = true;

	int current_idx = 0;
	char current_page[64] = "main.txt";

	for ( ;; ) {
		struct msg msg;
		while (queue_try_remove(&msg_queue, &msg)) {
			switch (msg.type) {
			case MSG_TYPE_CORE1_LAUNCHED:
				multicore = true;

				// Start timeout for ->UNMOUNTED
				add_alarm_in_ms(1000, usb_connect_timeout, NULL, true);

				break;
			case MSG_TYPE_USB_TIMEOUT:
				if (usb_state == USB_STATE_WAITING) {
					usb_state = USB_STATE_UNMOUNTED;

					// Show buttons
					refresh = true;
				}
				power_ref_put();
				break;
			case MSG_TYPE_USB_CONNECTED:
				// Show USB screen
				badger_update_speed(2);
				badger_pen(15);
				badger_rectangle(0, 16, BADGER_WIDTH, 16);
				badger_pen(0);
				badger_thickness(1);
				badger_text("USB connected. Eject and press A to update", 10, 24, 0.4f, 0.0f, 1);
				badger_partial_update(0, 16, 296, 16, true);

				usb_state = USB_STATE_MOUNTED;
				power_ref_get();
				break;
			case MSG_TYPE_USB_DISCONNECTED:
				badger_update_speed(2);
				badger_pen(15);
				badger_rectangle(0, 24, BADGER_WIDTH, 16);
				badger_pen(0);
				badger_thickness(1);
				badger_text("USB disconnected", 10, 32, 0.4f, 0.0f, 1);
				badger_partial_update(0, 24, 296, 16, true);

				usb_state = USB_STATE_UNMOUNTED;

				res = lfs_ctx_mount(&lfs_ctx, multicore);
				if (!res) {
					// TODO: To be totally safe, should also take away the MSC disk here
					// in case of reconnect before the update is finished
					badger_update_speed(2);
					badger_pen(15);
					badger_rectangle(0, 40, BADGER_WIDTH, 16);
					badger_pen(0);
					badger_thickness(1);

					res = do_flash_update(&lfs_ctx.lfs);
					if (res) {
						printf("failed to update flash");
						badger_text("failed to update flash", 10, 48, 0.4f, 0.0f, 1);
						badger_partial_update(0, 40, 296, 16, true);
					} else {
						badger_text("flash updated", 10, 48, 0.4f, 0.0f, 1);
						badger_partial_update(0, 40, 296, 16, true);
					}

					lfs_ctx_unmount(&lfs_ctx);
				}

				// Show main screen
				refresh = true;

				power_ref_put();
				break;
			case MSG_TYPE_POWER_OFF:
				if (power_ref != 0) {
					break;
				}

				// TODO: Disable USB?
				res = lfs_ctx_mount(&lfs_ctx, multicore);
				printf("mount: %d\n", res);
				if (!res) {
					struct screen_page *page = parse_file(&lfs_ctx.lfs, current_page);
					if (!page) {
						sprintf(current_page, "main.txt");
						current_idx = 0;
						page = parse_file(&lfs_ctx.lfs, current_page);
					}
					lfs_ctx_unmount(&lfs_ctx);

					if (page) {
						badger_update_speed(0);
						screen_page_display(page);
						screen_page_free(page);
					}
				}

				badger_pen(0);
				badger_thickness(1);
				badger_update_speed(3);
				badger_text("o", 2, 4, 0.4f, 0.0f, 1);
				badger_partial_update(0, 0, 16, 16, true);

				lfs_ctx_unmount(&lfs_ctx);

				gpio_put(BADGER_PIN_ENABLE_3V3, 0);

				// If we're on VBUS, then actually we keep running
				break;
			case MSG_TYPE_CDC_CONNECTED:
				sleep_ms(100);
				printf("Hello CDC\n");

				res = lfs_ctx_mount(&lfs_ctx, multicore);
				printf("mount: %d\n", res);
				if (!res) {
					struct screen_page *page = parse_file(&lfs_ctx.lfs, "barcode.txt");
					lfs_ctx_unmount(&lfs_ctx);

					if (page) {
						screen_page_display(page);
						screen_page_free(page);
					}
				}

				break;
			case MSG_TYPE_BTNS_CHANGED:
				{
					power_ref_get();
					badger_update_button_states();
					uint32_t new = badger_button_states();
					uint32_t pressed = new & ~buttons;
					uint32_t released = buttons & ~new;
					buttons = new;

					if (released & (1 << BADGER_PIN_DOWN)) {
						current_idx++;
						sprintf(current_page, "page%d.txt", current_idx);
						res = lfs_ctx_mount(&lfs_ctx, multicore);
						printf("mount: %d\n", res);
						if (!res) {
							struct screen_page *page = parse_file(&lfs_ctx.lfs, current_page);
							if (!page) {
								sprintf(current_page, "main.txt");
								current_idx = 0;
								page = parse_file(&lfs_ctx.lfs, current_page);
							}
							lfs_ctx_unmount(&lfs_ctx);

							if (page) {
								badger_update_speed(3);
								screen_page_display(page);
								screen_page_free(page);
							}
						}
					}

					if ((released & (1 << BADGER_PIN_A)) && usb_state == USB_STATE_MOUNTED) {
						tud_disconnect();

						// HAX! This is just a copy of the full disconnect handler
						badger_update_speed(2);
						badger_pen(15);
						badger_rectangle(0, 24, BADGER_WIDTH, 16);
						badger_pen(0);
						badger_thickness(1);
						badger_text("USB disconnected", 10, 32, 0.4f, 0.0f, 1);
						badger_partial_update(0, 24, 296, 16, true);

						usb_state = USB_STATE_UNMOUNTED;

						res = lfs_ctx_mount(&lfs_ctx, multicore);
						if (!res) {
							// TODO: To be totally safe, should also take away the MSC disk here
							// in case of reconnect before the update is finished
							badger_update_speed(2);
							badger_pen(15);
							badger_rectangle(0, 40, BADGER_WIDTH, 16);
							badger_pen(0);
							badger_thickness(1);

							res = do_flash_update(&lfs_ctx.lfs);
							if (res) {
								printf("failed to update flash");
								badger_text("failed to update flash", 10, 48, 0.4f, 0.0f, 1);
								badger_partial_update(0, 40, 296, 16, true);
							} else {
								badger_text("flash updated", 10, 48, 0.4f, 0.0f, 1);
								badger_partial_update(0, 40, 296, 16, true);
							}

							lfs_ctx_unmount(&lfs_ctx);
						}

						// Drop the USB connection reference.
						// tud_disconnect() doesn't throw a disconnection event
						power_ref_put();

						// Connect again
						tud_connect();
					}

					printf("pressed: 0x%08x, released: 0x%08x\n", pressed, released);
					power_ref_put();
				}
				break;
			}
		}

		// FIXME: This is still needed to do the initial draw on battery
		if (usb_state == USB_STATE_MOUNTED) {
			// Not a lot to do.
			if (buttons_pressed) {
				// Trigger disconnect
			}
		} else if (usb_state == USB_STATE_UNMOUNTED) {
			if (buttons_pressed || refresh) {
				power_ref_get();

				res = lfs_ctx_mount(&lfs_ctx, multicore);
				printf("mount: %d\n", res);
				if (!res) {
					struct screen_page *page = parse_file(&lfs_ctx.lfs, "main.txt");
					lfs_ctx_unmount(&lfs_ctx);

					if (page) {
						screen_page_display(page);
						screen_page_free(page);
					} else {
						screen_page_calculate_sizes(&empty_page);
						screen_page_display(&empty_page);
					}
				} else {
					screen_page_calculate_sizes(&empty_page);
					screen_page_display(&empty_page);
				}

				refresh = false;
				power_ref_put();
			}
		}
	}
}
