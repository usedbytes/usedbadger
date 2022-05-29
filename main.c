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
#include "usb.h"

#include "littlefs/lfs.h"
#include "littlefs/bd/lfs_rambd.h"

#define FATFS_SECTOR_SIZE 512
#define FATFS_NUM_SECTORS 128

#define README_CONTENTS "This is the default file"

struct lfs_flash_cfg {
	bool multicore;
	critical_section_t lock;
	uint32_t base;
};

struct lfs_flash_cfg lfs_flash_cfg = {
	// FIXME: The linker doesn't know anything about this
	.base = PICO_FLASH_SIZE_BYTES - (4096 * 16),
};

int lfs_flash_read(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, void *buffer, lfs_size_t size)
{
	struct lfs_flash_cfg *ctx = cfg->context;
	uint32_t flash_offs = ctx->base + (block * cfg->block_size) + off;

	uint8_t *src = (uint8_t *)(XIP_BASE + flash_offs);

	memcpy(buffer, src, size);

	return 0;
}

int lfs_flash_prog(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, const void *buffer, lfs_size_t size)
{
	struct lfs_flash_cfg *ctx = cfg->context;

	critical_section_enter_blocking(&ctx->lock);

	uint32_t flash_offs = ctx->base + (block * cfg->block_size) + off;
	flash_range_program(flash_offs, buffer, size);

	critical_section_exit(&ctx->lock);

	return 0;
}

int lfs_flash_erase(const struct lfs_config *cfg, lfs_block_t block)
{
	struct lfs_flash_cfg *ctx = cfg->context;

	critical_section_enter_blocking(&ctx->lock);

	uint32_t flash_offs = ctx->base + (block * cfg->block_size);
	flash_range_erase(flash_offs, cfg->block_size);

	critical_section_exit(&ctx->lock);

	return 0;
}

int lfs_flash_sync(const struct lfs_config *cfg)
{
	return 0;
}

const struct lfs_config lfs_cfg = {
	.context = &lfs_flash_cfg,
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
};


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
			break;
		}

		res = copy_file_flash_to_fat(lfs, &lfp, &fp, dirent.size);
		printf("copy_file: %d\n", res);
		if (res) {
			break;
		}

		res = lfs_file_close(lfs, &lfp);
		printf("file_close: %d\n", res);
		if (res < 0) {
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

static int lfs_init(lfs_t *lfs, bool multicore)
{
	critical_section_init(&lfs_flash_cfg.lock);
	lfs_flash_cfg.multicore = multicore;

	int res = lfs_mount(lfs, &lfs_cfg);
	if (res) {
		// On first try, format and write an initial file
		lfs_file_t lfp = { 0 };

		res = lfs_format(lfs, &lfs_cfg);
		if (res) {
			goto err_out;
		}

		res = lfs_mount(lfs, &lfs_cfg);
		if (res) {
			goto err_out;
		}

		res = lfs_file_open(lfs, &lfp, "readme.txt", LFS_O_RDWR | LFS_O_CREAT);
		if (res) {
			goto err_unmount;
		}

		lfs_file_write(lfs, &lfp, README_CONTENTS, strlen(README_CONTENTS));

		res = lfs_file_close(lfs, &lfp);
		if (res) {
			goto err_unmount;
		}
	}

	return 0;

err_unmount:
	lfs_unmount(lfs);
err_out:
	critical_section_deinit(&lfs_flash_cfg.lock);
	return res;
}

static void lfs_term(lfs_t *lfs)
{
	lfs_unmount(lfs);
	critical_section_deinit(&lfs_flash_cfg.lock);
}

queue_t msg_queue;

enum msg_type {
	MSG_TYPE_NONE = 0,
	MSG_TYPE_USB_CONNECTING,
	MSG_TYPE_USB_TIMEOUT,
	MSG_TYPE_USB_CONNECTED,
	MSG_TYPE_USB_DISCONNECTED,
	MSG_TYPE_MSC_UNMOUNTED,
	MSG_TYPE_CDC_CONNECTED,
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
	queue_add_blocking(&msg_queue, &(struct msg){ .type = MSG_TYPE_USB_CONNECTING });

	usb_main(&usb_opt);

	// Will never reach here
	return;
}

void prepare_usb_filesystem(struct usb_msc_disk *msc_disk)
{
	int res;
	char error_buf[64];
	lfs_t lfs;

	// Set-up the FAT singleton - this leaves the filesystem mounted
	res = fat_ramdisk_init(&fat_ramdisk);
	if (res) {
		snprintf(error_buf, sizeof(error_buf), "FatFS initialisation failed: %d", res);
		goto done;
	}

	// Core 1 isn't running yet, so no multicore
	res = lfs_init(&lfs, false);
	if (res) {
		snprintf(error_buf, sizeof(error_buf), "littlefs initialisation failed: %d", res);
		goto fatfs_unmount;
	}

	res = copy_flash_to_fat(&lfs);
	if (res) {
		snprintf(error_buf, sizeof(error_buf), "copy flash to MSC failed: %d", res);
		goto lfs_unmount;
	}

lfs_unmount:
	lfs_term(&lfs);
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
	prepare_usb_filesystem(&usb_opt.msc.disk);
	multicore_launch_core1(core1_main);
}

static int64_t usb_connect_timeout(alarm_id_t id, void *d)
{
	queue_add_blocking(&msg_queue, &(struct msg){ .type = MSG_TYPE_USB_TIMEOUT });

	return 0;
}

int main() {
	char message[128];
	const char *button = "";
	badger_init();

	badger_led(255);

	// Nothing needs power yet
	int power_ref = 0;

	if (!gpio_get(BADGER_PIN_VBUS_DETECT)) {
		// find which button was used to wake up
		if(badger_pressed_to_wake(BADGER_PIN_A)) { button = "A"; }
		if(badger_pressed_to_wake(BADGER_PIN_B)) { button = "B"; }
		if(badger_pressed_to_wake(BADGER_PIN_C)) { button = "C"; }
		if(badger_pressed_to_wake(BADGER_PIN_D)) { button = "D"; }
		if(badger_pressed_to_wake(BADGER_PIN_E)) { button = "E"; }

		if(button != "") {
			snprintf(message, sizeof(message), "On battery, button %s", button);
		}

		badger_thickness(2);

		badger_pen(15);
		badger_clear();
		badger_pen(0);
		badger_text(message, 10, 20, 0.6f, 0.0f, 1);
		badger_update(true);

		badger_text("sleeping", 10, 72, 0.4f, 0.0f, 1);
		badger_partial_update(0, 64, 296, 16, true);

		badger_halt();
	}

	queue_init(&msg_queue, sizeof(struct msg), 8);

	badger_pen(15);
	badger_clear();
	badger_pen(0);
	badger_update_speed(1);
	badger_update(true);

	if (gpio_get(BADGER_PIN_VBUS_DETECT)) {
		launch_usb();
	}

	bool usb_connected = false;
	bool multicore = false;

	for ( ;; ) {
		/*
		char buf[10];
		sprintf(buf, "%d", n++);
		badger_text(buf, 10, 24, 0.4f, 0.0f, 1);
		badger_partial_update(0, 16, 296, 16, true);
		sleep_ms(300);
		*/

		struct msg msg;
		queue_remove_blocking(&msg_queue, &msg);
		switch (msg.type) {
		case MSG_TYPE_USB_CONNECTING:
			multicore = true;
			badger_text("USB connecting", 10, 16, 0.4f, 0.0f, 1);
			badger_partial_update(0, 8, 296, 16, true);

			// Keep power long enough to handle disconnect
			power_ref++;
			gpio_put(BADGER_PIN_ENABLE_3V3, 1);

			add_alarm_in_ms(1000, usb_connect_timeout, NULL, true);
			break;
		case MSG_TYPE_USB_TIMEOUT:
			if (usb_connected) {
				break;
			}

			badger_text("USB timeout", 10, 24, 0.4f, 0.0f, 1);
			badger_partial_update(0, 16, 296, 16, true);
			power_ref--;

			break;
		case MSG_TYPE_USB_CONNECTED:
			badger_text("USB connected", 10, 24, 0.4f, 0.0f, 1);
			badger_partial_update(0, 16, 296, 16, true);
			usb_connected = true;
			break;
		case MSG_TYPE_MSC_UNMOUNTED:
			badger_text("MSC unmounted", 10, 24, 0.4f, 0.0f, 1);
			badger_partial_update(0, 16, 296, 16, true);
			break;
		case MSG_TYPE_CDC_CONNECTED:
			printf("Hello CDC\n");
			sleep_ms(100);
			/*
			{
				struct usb_msc_disk disk;
				init_filesystem(&disk);
			}
			*/
			break;
		case MSG_TYPE_USB_DISCONNECTED:
			badger_text("USB disconnected", 10, 32, 0.4f, 0.0f, 1);
			badger_partial_update(0, 24, 296, 16, true);
			usb_connected = false;

			power_ref--;
			break;
		}

		if (power_ref <= 0) {
			badger_text("sleeping", 10, 72, 0.4f, 0.0f, 1);
			badger_partial_update(0, 64, 296, 16, true);

			gpio_put(BADGER_PIN_ENABLE_3V3, 0);

			while (1) {
				__wfi();
			}
		}
	}
}
