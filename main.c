#include <stdio.h>
#include <string.h>

#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/util/queue.h"
#include "pico/platform.h"
#include "pico/multicore.h"

#include "spiffs/src/spiffs.h"
#include "badger.h"
#include "fatfs/ff.h"
#include "error_disk.h"
#include "fat_ramdisk.h"
#include "usb.h"

#define FATFS_SECTOR_SIZE 512
#define FATFS_NUM_SECTORS 128

static uint8_t __attribute__ ((section ("noinit"))) spiffs_ramdisk_data[FATFS_SECTOR_SIZE * 32];

s32_t ram_spiffs_read(u32_t addr, u32_t size, u8_t *dst)
{
	memcpy(dst, &spiffs_ramdisk_data[addr], size);
	return SPIFFS_OK;
}

s32_t ram_spiffs_write(u32_t addr, u32_t size, u8_t *src)
{
	memcpy(&spiffs_ramdisk_data[addr], src, size);
	return SPIFFS_OK;
}

s32_t ram_spiffs_erase(u32_t addr, u32_t size)
{
	memset(&spiffs_ramdisk_data[addr], 0xff, size);
	return SPIFFS_OK;
}

static spiffs_config spiffs_cfg = {
    .phys_size = FATFS_SECTOR_SIZE * 32,
    .phys_addr = 0,
    .phys_erase_block = 4096,
    .log_block_size = 4096,
    .log_page_size = 256,
    .hal_read_f = ram_spiffs_read,
    .hal_write_f = ram_spiffs_write,
    .hal_erase_f = ram_spiffs_erase,
};

// Don't initialise because this is only used when USB connected
static uint8_t __attribute__ ((section ("noinit"))) fatfs_ramdisk_data[FATFS_SECTOR_SIZE * FATFS_NUM_SECTORS];
static const struct fat_ramdisk fat_ramdisk = {
	.label = "usedbadger",
	.sector_size = FATFS_SECTOR_SIZE,
	.num_sectors = FATFS_NUM_SECTORS,
	.data = fatfs_ramdisk_data,
};

static int copy_file_spiffs_to_fat(spiffs *spiffs, spiffs_file sfd, FIL *ffp, uint32_t size)
{
	char buf[128];
	int res, nread, nwrote;

	while (size) {
		res = SPIFFS_read(spiffs, sfd, buf, sizeof(buf));
		if (res < 0) {
			return res;
		}
		nread = res;

		res = f_write(ffp, buf, nread, &nwrote);
		if (res < 0) {
			return res;
		} else if (nwrote != nread) {
			return -1;
		}

		size -= nread;
	}

	return 0;
}

// Grr...
#define SPIFFS_VIS_END -10072

static int copy_spiffs_to_fat(spiffs *spiffs)
{
	int spiffs_err;
	int fatfs_err;
	spiffs_DIR dir;
	struct spiffs_dirent de;
	FIL fp = { 0 };

	SPIFFS_clearerr(spiffs);

	SPIFFS_opendir(spiffs, "", &dir);
	if (SPIFFS_errno(spiffs)) {
		return SPIFFS_errno(spiffs);
	}

	while (SPIFFS_readdir(&dir, &de)) {
		char tmp_buf[128];
		spiffs_file sfd = SPIFFS_open_by_dirent(spiffs, &de, SPIFFS_RDONLY, 0);
		if (sfd < 0) {
			break;
		}

		fatfs_err = f_open(&fp, de.name, FA_WRITE | FA_CREATE_ALWAYS);
		if (fatfs_err) {
			break;
		}

		fatfs_err = copy_file_spiffs_to_fat(spiffs, sfd, &fp, de.size);
		if (fatfs_err) {
			break;
		}

		SPIFFS_close(spiffs, sfd);

		fatfs_err = f_close(&fp);
		if (fatfs_err) {
			break;
		}
	}
	spiffs_err = SPIFFS_errno(spiffs);
	SPIFFS_closedir(&dir);
	spiffs_err = SPIFFS_errno(spiffs);

	if ((spiffs_err && (spiffs_err != SPIFFS_VIS_END)) || fatfs_err) {
		return -1;
	}

	return 0;
}

static void init_fat_from(spiffs *spiffs, struct usb_msc_disk *msc_disk)
{
	msc_disk->block_size = fat_ramdisk.sector_size;
	msc_disk->num_blocks = fat_ramdisk.num_sectors;
	msc_disk->data = fat_ramdisk.data;
	msc_disk->read_only = false;

	char error_buf[32];

	FRESULT res = fat_ramdisk_init(&fat_ramdisk);
	if (res) {
		snprintf(error_buf, sizeof(error_buf), "FatFS initialisation failed: %d", res);
		goto err_out;
	}

	res = copy_spiffs_to_fat(spiffs);
	if (res) {
		snprintf(error_buf, sizeof(error_buf), "Copy spiffs to FatFS failed", res);
		goto err_unmount;
	}

	f_unmount("");

	return;

err_unmount:
	f_unmount("");
err_out:
	init_error_filesystem(&fat_ramdisk, error_buf);
	msc_disk->read_only = true;
	return;
}

static void init_filesystem(struct usb_msc_disk *msc_disk)
{
	static spiffs fs;
	static u8_t spiffs_work_buf[256*2];
	static u8_t spiffs_fds[32*2];
	static u8_t spiffs_cache_buf[(256+32)*4];
	char error_buf[32];

	memset(spiffs_ramdisk_data, 0xff, sizeof(spiffs_ramdisk_data));

	int res = SPIFFS_mount(&fs,
			&spiffs_cfg,
			spiffs_work_buf,
			spiffs_fds,
			sizeof(spiffs_fds),
			spiffs_cache_buf,
			sizeof(spiffs_cache_buf),
			0);

	spiffs_file fd = SPIFFS_open(&fs, "readme.txt", SPIFFS_CREAT | SPIFFS_TRUNC | SPIFFS_RDWR, 0);

	const char *str = "Hello world from spiffs";
	res = SPIFFS_write(&fs, fd, (u8_t *)str, strlen(str));
	SPIFFS_close(&fs, fd);

	init_fat_from(&fs, msc_disk);
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

static const struct usb_opt usb_opt = {
	.connect_cb = usb_connect_cb,
	.disconnect_cb = usb_disconnect_cb,
};

void core1_main()
{
	static struct usb_opt opt = {
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

	init_filesystem(&opt.msc.disk);
	usb_main(&opt);

	// Will never reach here
	return;
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
		queue_add_blocking(&msg_queue, &(struct msg){ .type = MSG_TYPE_USB_CONNECTING });
		multicore_launch_core1(core1_main);
	}

	bool usb_connected = false;

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
