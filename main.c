#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/time.h"
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

void core1_main()
{
	static struct msc_ctx msc_ctx = {
		.vid = "usedbytes",
		.pid = "usedbadger mass storage",
		.rev = "1.0",
	};

	init_filesystem(&msc_ctx);
	usb_msc_init(&msc_ctx);

	usb_main();

	// Will never reach here
	return;
}

int main() {
	char message[128];
	const char *button = "";

	badger_init();

	if (gpio_get(BADGER_PIN_VBUS_DETECT)) {
		snprintf(message, sizeof(message), "on VBUS");
		multicore_launch_core1(core1_main);
	}

	// find which button was used to wake up
	if(badger_pressed_to_wake(BADGER_PIN_A)) { button = "A"; }
	if(badger_pressed_to_wake(BADGER_PIN_B)) { button = "B"; }
	if(badger_pressed_to_wake(BADGER_PIN_C)) { button = "C"; }
	if(badger_pressed_to_wake(BADGER_PIN_D)) { button = "D"; }
	if(badger_pressed_to_wake(BADGER_PIN_E)) { button = "E"; }

	if(button != "") {
		snprintf(message, sizeof(message), "woken up by button %s", button);
	}

	badger_thickness(2);

	badger_pen(15);
	badger_clear();
	badger_pen(0);
	badger_text(message, 10, 20, 0.6f, 0.0f, 1);
	badger_text("(press any button to go to sleep.)", 10, 70, 0.4f, 0.0f, 1);
	badger_update(false);

	while (badger_is_busy()) {
		sleep_ms(10);
	}

	badger_wait_for_press();

	/*
	badger_pen(15);
	badger_clear();
	badger_pen(0);
	badger_text("going back to sleep...", 10, 20, 0.6f);
	badger_text("z", 220, 50, 0.6f);
	badger_text("z", 230, 40, 0.8f);
	badger_text("z", 240, 30, 1.0f);
	badger_text("(press any button to wake up.)", 10, 70, 0.4f);
	badger_update();

	while (badger_is_busy()) {
		sleep_ms(10);
	}
	*/

	if (!gpio_get(BADGER_PIN_VBUS_DETECT)) {
		badger_halt();
	} else {
		//do_fat();
		uint8_t val = 0xff;
		while (1) {
			printf("Hello?\n");
			sleep_ms(500);
			val = ~val;
			badger_led(val);
		}
	}
}
