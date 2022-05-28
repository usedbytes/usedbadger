#include <stdio.h>
#include <string.h>

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
#include "usb_msc.h"

#define FATFS_SECTOR_SIZE 512
#define FATFS_NUM_SECTORS 128

// Don't initialise because this is only used when USB connected
static uint8_t __attribute__ ((section ("noinit"))) fatfs_ramdisk_data[FATFS_SECTOR_SIZE * FATFS_NUM_SECTORS];
static const struct fat_ramdisk fat_ramdisk = {
	.label = "usedbadger",
	.sector_size = FATFS_SECTOR_SIZE,
	.num_sectors = FATFS_NUM_SECTORS,
	.data = fatfs_ramdisk_data,
};

static void init_filesystem(struct msc_ctx *ctx)
{
	ctx->block_size = fat_ramdisk.sector_size;
	ctx->num_blocks = fat_ramdisk.num_sectors;
	ctx->data = fat_ramdisk.data;
	ctx->read_only = false;

	FIL fp = { 0 };
	UINT bw;
	const char *contents = "This is the readme written via fatfs";
	char error_buf[32];
	FRESULT res = fat_ramdisk_init(&fat_ramdisk);
	if (res) {
		snprintf(error_buf, sizeof(error_buf), "FatFS initialisation failed: %d", res);
		goto err_out;
	}

	res = f_open(&fp, "readme.txt", FA_WRITE | FA_CREATE_ALWAYS);
	if (res) {
		snprintf(error_buf, sizeof(error_buf), "f_open: %d", res);
		goto err_unmount;
	}

	res = f_write(&fp, contents, strlen(contents), &bw);
	if (res) {
		snprintf(error_buf, sizeof(error_buf), "f_write: %d", res);
		goto err_close;
	}

	res = f_close(&fp);
	if (res) {
		snprintf(error_buf, sizeof(error_buf), "f_close: %d", res);
		goto err_unmount;
	}

	f_unmount("");

	return;

err_close:
	f_close(&fp);
err_unmount:
	f_unmount("");
err_out:
	init_error_filesystem(&fat_ramdisk, error_buf);
	ctx->read_only = true;
	return;
}

queue_t msg_queue;

enum msg_type {
	MSG_TYPE_NONE = 0,
	MSG_TYPE_USB_CONNECTING,
	MSG_TYPE_USB_TIMEOUT,
	MSG_TYPE_USB_CONNECTED,
	MSG_TYPE_USB_DISCONNECTED,
	MSG_TYPE_MSC_UNMOUNTED,
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

static const struct usb_opt usb_opt = {
	.connect_cb = usb_connect_cb,
	.disconnect_cb = usb_disconnect_cb,
};

void core1_main()
{
	static struct msc_ctx msc_ctx = {
		.vid = "usedbytes",
		.pid = "usedbadger mass storage",
		.rev = "1.0",
	};

	init_filesystem(&msc_ctx);
	usb_msc_init(&msc_ctx);

	usb_main(&usb_opt);

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

	if (gpio_get(BADGER_PIN_VBUS_DETECT)) {
		queue_add_blocking(&msg_queue, &(struct msg){ .type = MSG_TYPE_USB_CONNECTING });
		multicore_launch_core1(core1_main);
	}

	badger_pen(15);
	badger_clear();
	badger_pen(0);
	badger_update_speed(1);
	badger_update(true);

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
