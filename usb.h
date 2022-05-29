#ifndef __USEDBADGER_USB_H__
#define __USEDBADGER_USB_H__

#include "tusb.h"

struct usb_msc_disk {
	const char *vid;
	const char *pid;
	const char *rev;

	uint16_t block_size;
	uint16_t num_blocks;
	uint8_t *data;
	bool read_only;
};

struct usb_opt {
	void *user;
	void (*connect_cb)(void *user);
	void (*disconnect_cb)(void *user);

	struct {
		void (*line_state_cb)(void *user, uint8_t itf, bool dts, bool rts);
	} cdc;

	struct {
		struct usb_msc_disk disk;
		void (*start_stop_cb)(void *user, uint8_t lun, uint8_t power_condition, bool start, bool load_eject);
	} msc;
};

int usb_main(const struct usb_opt *opt);

#endif /* __USEDBADGER_USB_H__ */
