#include "pico/stdlib.h"
#include <stdio.h>
#include <cstring>
#include <string>
#include <algorithm>
#include "pico/time.h"
#include "pico/platform.h"
#include "pico/multicore.h"

#include "common/pimoroni_common.hpp"
#include "badger2040.hpp"

#include "fatfs/ff.h"
#include "error_disk.h"
#include "fat_ramdisk.h"
#include "usb.h"
#include "usb_msc.h"

using namespace pimoroni;

#define FATFS_SECTOR_SIZE 512
#define FATFS_NUM_SECTORS 128

// Don't initialise because this is only used when USB connected
static uint8_t __attribute__ ((section ("noinit"))) fatfs_ramdisk_data[FATFS_SECTOR_SIZE * FATFS_NUM_SECTORS];
static const fat_ramdisk fat_ramdisk = {
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

// this simple example tells you which button was used to wake up
// Badger2040 and then immediately halts again on another button press

Badger2040 badger;

int main() {

	badger.init();

	// find which button was used to wake up
	std::string button = "";
	std::string message = "started up.";
	if(badger.pressed_to_wake(badger.A)) { button += "A"; }
	if(badger.pressed_to_wake(badger.B)) { button += "B"; }
	if(badger.pressed_to_wake(badger.C)) { button += "C"; }
	if(badger.pressed_to_wake(badger.D)) { button += "D"; }
	if(badger.pressed_to_wake(badger.E)) { button += "E"; }

	if(button != "") {
		message = "woken up by button " + button + ".";
	}

	if (gpio_get(badger.VBUS_DETECT)) {
		message = "on VBUS";
		multicore_launch_core1(core1_main);
	}

	badger.thickness(2);

	badger.pen(15);
	badger.clear();
	badger.pen(0);
	badger.text(message, 10, 20, 0.6f);
	badger.text("(press any button to go to sleep.)", 10, 70, 0.4f);
	badger.update();

	while (badger.is_busy()) {
		sleep_ms(10);
	}

	badger.wait_for_press();

	/*
	badger.pen(15);
	badger.clear();
	badger.pen(0);
	badger.text("going back to sleep...", 10, 20, 0.6f);
	badger.text("z", 220, 50, 0.6f);
	badger.text("z", 230, 40, 0.8f);
	badger.text("z", 240, 30, 1.0f);
	badger.text("(press any button to wake up.)", 10, 70, 0.4f);
	badger.update();

	while (badger.is_busy()) {
		sleep_ms(10);
	}
	*/

	if (!gpio_get(badger.VBUS_DETECT)) {
		badger.halt();
	} else {
		//do_fat();
		uint8_t val = 0xff;
		while (1) {
			printf("Hello?\n");
			sleep_ms(500);
			val = ~val;
			badger.led(val);
		}
	}

}
