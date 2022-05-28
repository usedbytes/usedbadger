#ifndef __USEDBADGER_USB_H__
#define __USEDBADGER_USB_H__

#ifdef __cplusplus
 extern "C" {
#endif

struct usb_opt {
	void (*connect_cb)(void);
	void (*disconnect_cb)(void);
};

int usb_main(const struct usb_opt *opt);

#ifdef __cplusplus
 }
#endif

#endif /* __USEDBADGER_USB_H__ */
