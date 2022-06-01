#include <stdio.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/util/queue.h"
#include "pico/platform.h"
#include "pico/multicore.h"

#include "badger.h"
#include "fatfs/ff.h"
#include "error_disk.h"
#include "fat_ramdisk.h"
#include "lfs_pico_flash.h"
#include "usb.h"

#define FATFS_SECTOR_SIZE 512
#define FATFS_NUM_SECTORS 128

#define README_CONTENTS "This is the default file"

// Don't initialise because this is only used when USB connected
static uint8_t __attribute__ ((section ("noinit"))) fatfs_ramdisk_data[FATFS_SECTOR_SIZE * FATFS_NUM_SECTORS];
static const struct fat_ramdisk fat_ramdisk = {
	.label = "usedbadger",
	.sector_size = FATFS_SECTOR_SIZE,
	.num_sectors = FATFS_NUM_SECTORS,
	.data = fatfs_ramdisk_data,
};

static int copy_file_flash_to_fat(lfs_t *lfs, lfs_file_t *lfp, FIL *ffp, uint32_t size)
{
	char buf[128];
	int res, nread, nwrote;

	while (size) {
		res = lfs_file_read(lfs, lfp, buf, sizeof(buf));
		printf("file_read: %d\n", res);
		if (res < 0) {
			return res;
		}
		nread = res;

		// Short read
		if (nread == 0) {
			res = -1;
			break;
		}

		res = f_write(ffp, buf, nread, &nwrote);
		printf("f_write: %d\n", res);
		if (res < 0) {
			return res;
		} else if (nwrote != nread) {
			return -1;
		}

		size -= nread;
	}

	return 0;
}

static int copy_file_fat_to_flash(lfs_t *lfs, lfs_file_t *dst, FIL *src, uint32_t size)
{
	char buf[128];
	int res, nread;

	while (size) {
		res = f_read(src, buf, sizeof(buf), &nread);
		if (res) {
			return res;
		}

		// Short read
		if (nread == 0) {
			res = -1;
			break;
		}

		res = lfs_file_write(lfs, dst, buf, nread);
		if (res < 0) {
			return res;
		} else if (res != nread) {
			return -1;
		}

		size -= nread;
	}

	return 0;
}

static int compare_files(lfs_t *lfs, lfs_file_t *lfp, FIL *ffp, uint32_t size)
{
	char abuf[128];
	char bbuf[128];
	int res, aread, bread;

	while (size) {
		res = lfs_file_read(lfs, lfp, abuf, sizeof(abuf));
		printf("file_read: %d\n", res);
		if (res < 0) {
			return res;
		}
		aread = res;

		// Short read
		if (aread == 0) {
			return -1;
		}

		res = f_read(ffp, bbuf, sizeof(bbuf), &bread);
		if (res) {
			return -res;
		}

		if (aread != bread) {
			// Can't handle this currently, just assume they're different
			// (it could just be that one FS decides to chunk reads differently)
			return 1;
		}

		if (memcmp(abuf, bbuf, aread)) {
			return 1;
		}

		size -= aread;
	}

	return 0;
}

static int copy_flash_to_fat(lfs_t *lfs)
{
	lfs_dir_t dir;
	struct lfs_info dirent;
	FIL fp = { 0 };
	int res;

	res = lfs_dir_open(lfs, &dir, "");
	printf("dir_open: %d\n", res);
	if (res < 0) {
		return res;
	}

	int dir_res;
	while ((dir_res = lfs_dir_read(lfs, &dir, &dirent)) > 0) {
		char tmp_buf[128];

		if (dirent.type == LFS_TYPE_DIR) {
			continue;
		}

		lfs_file_t lfp;
		res = lfs_file_open(lfs, &lfp, dirent.name, LFS_O_RDONLY);
		printf("file_open: %s, %d\n", dirent.name, res);
		if (res < 0) {
			break;
		}

		res = f_open(&fp, dirent.name, FA_WRITE | FA_CREATE_ALWAYS);
		printf("f_open: %d\n", res);
		if (res) {
			lfs_file_close(lfs, &lfp);
			break;
		}

		res = copy_file_flash_to_fat(lfs, &lfp, &fp, dirent.size);
		printf("copy_file: %d\n", res);
		if (res) {
			lfs_file_close(lfs, &lfp);
			f_close(&fp);
			break;
		}

		res = lfs_file_close(lfs, &lfp);
		printf("file_close: %d\n", res);
		if (res < 0) {
			f_close(&fp);
			break;
		}

		res = f_close(&fp);
		printf("f_close: %d\n", res);
		if (res) {
			break;
		}
	}

	lfs_dir_close(lfs, &dir);

	printf("dir_res: %d\n", dir_res);
	if (dir_res != 0) {
		return -1;
	}

	printf("res: %d\n", res);
	if (res) {
		return -1;
	}

	return 0;
}

static int copy_fat_to_flash(lfs_t *lfs)
{
	DIR dir = { 0 };
	FILINFO dirent = { 0 };
	FIL fp = { 0 };
	int res;

	res = f_opendir(&dir, "");
	if (res != 0) {
		return res;
	}

	do {
		res = f_readdir(&dir, &dirent);
		if (res) {
			break;
		} else if (strlen(dirent.fname) == 0) {
			break;
		} else if (dirent.fattrib & AM_DIR) {
			continue;
		}

		res = f_open(&fp, dirent.fname, FA_READ | FA_OPEN_EXISTING);
		printf("f_open: '%s' s%d %d\n", dirent.fname, dirent.fsize, res);
		if (res) {
			break;
		}

		lfs_file_t lfp;

		// Check if the files are the same
		res = lfs_file_open(lfs, &lfp, dirent.fname, LFS_O_RDONLY);
		printf("file_open read: %s, %d\n", dirent.fname, res);
		if (res == 0) {
			res = compare_files(lfs, &lfp, &fp, dirent.fsize);

			// Always close the read-only flash version
			lfs_file_close(lfs, &lfp);

			if (res < 0) {
				// Error
				printf("compare_files: %d\n", res);
				f_close(&fp);
				break;
			} else if (res == 0) {
				// The same
				printf("files are the same\n");
				f_close(&fp);
				continue;
			} else {
				// Different
				res = f_rewind(&fp);
				if (res) {
					printf("f_rewind: %d\n", res);
					f_close(&fp);
					break;
				}
			}
		}

		res = lfs_file_open(lfs, &lfp, dirent.fname, LFS_O_CREAT | LFS_O_TRUNC | LFS_O_RDWR);
		printf("file_open: %s, %d\n", dirent.fname, res);
		if (res < 0) {
			f_close(&fp);
			break;
		}

		res = copy_file_fat_to_flash(lfs, &lfp, &fp, dirent.fsize);
		printf("copy_file: %d\n", res);
		if (res) {
			lfs_file_close(lfs, &lfp);
			f_close(&fp);
			break;
		}

		res = lfs_file_close(lfs, &lfp);
		printf("file_close: %d\n", res);
		if (res < 0) {
			f_close(&fp);
			break;
		}

		res = f_close(&fp);
		printf("f_close: %d\n", res);
		if (res) {
			break;
		}
	} while (1);

	f_closedir(&dir);

	printf("res: %d\n", res);
	if (res) {
		return -1;
	}

	return 0;
}

static int do_flash_update(lfs_t *lfs)
{
	FATFS fat = { 0 };

	int res = f_mount(&fat, "", 1);
	if (res) {
		printf("failed to mount fat: %d", res);
		return res;
	}

	res = copy_fat_to_flash(lfs);
	if (res) {
		printf("failed to copy fat to flash: %d", res);
		goto err_unmount;
	}

	res = f_unmount("");
	if (res) {
		printf("failed to unmount fat: %d", res);
		return res;
	}

	return 0;

err_unmount:
	f_unmount("");
	return res;
}

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

void prepare_usb_filesystem(lfs_t *lfs, struct usb_msc_disk *msc_disk)
{
	int res;
	char error_buf[64];

	// Set-up the FAT singleton - this leaves the filesystem mounted
	res = fat_ramdisk_init(&fat_ramdisk);
	if (res) {
		snprintf(error_buf, sizeof(error_buf), "FatFS initialisation failed: %d", res);
		goto done;
	}

	res = copy_flash_to_fat(lfs);
	if (res) {
		snprintf(error_buf, sizeof(error_buf), "copy flash to MSC failed: %d", res);
	}

fatfs_unmount:
	f_unmount("");
done:
	if (res) {
		init_error_filesystem(&fat_ramdisk, error_buf);
	}
	msc_disk->block_size = fat_ramdisk.sector_size;
	msc_disk->num_blocks = fat_ramdisk.num_sectors;
	msc_disk->data = fat_ramdisk.data;
	msc_disk->read_only = (res != 0);
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

int main() {
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

	badger_init();
	badger_led(255);

	// Hold power alive for boot-up
	power_ref_get();

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
	badger_update_speed(1);
	badger_update(true);

	// Boot-up done
	power_ref_put();

	// TODO: Implement buttons
	bool buttons_pressed = false;
	// TODO: Should really just do the refresh while still holding the reference
	bool refresh = true;

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
				badger_text("USB connected", 10, 24, 0.4f, 0.0f, 1);
				badger_partial_update(0, 16, 296, 16, true);

				usb_state = USB_STATE_MOUNTED;
				power_ref_get();
				break;
			case MSG_TYPE_USB_DISCONNECTED:
				badger_text("USB disconnected", 10, 32, 0.4f, 0.0f, 1);
				badger_partial_update(0, 24, 296, 16, true);

				usb_state = USB_STATE_UNMOUNTED;

				res = lfs_ctx_mount(&lfs_ctx, multicore);
				if (!res) {
					// TODO: To be totally safe, should also take away the MSC disk here
					// in case of reconnect before the update is finished
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

				badger_text("sleeping", 10, 72, 0.4f, 0.0f, 1);
				badger_partial_update(0, 64, 296, 16, true);

				lfs_ctx_unmount(&lfs_ctx);

				gpio_put(BADGER_PIN_ENABLE_3V3, 0);

				// If we're on VBUS, then actually we keep running
				break;
			case MSG_TYPE_CDC_CONNECTED:
				sleep_ms(100);
				printf("Hello CDC\n");

				break;
			}
		}

		if (usb_state == USB_STATE_MOUNTED) {
			// Not a lot to do.
			if (buttons_pressed) {
				// Trigger disconnect
			}
		} else if (usb_state == USB_STATE_UNMOUNTED) {
			if (buttons_pressed || refresh) {
				power_ref_get();

				// Make sure we've got lfs mounted to be able to read screens
				lfs_ctx_mount(&lfs_ctx, multicore);

				badger_pen(15);
				badger_clear();
				badger_pen(0);
				badger_update_speed(1);
				badger_text("This is the main screen", 10, 20, 0.6f, 0.0f, 1);
				badger_update(true);

				refresh = false;
				lfs_ctx_unmount(&lfs_ctx);
				power_ref_put();
			}
		}
	}
}
