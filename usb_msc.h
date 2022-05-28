#ifndef __UB_USB_MSC_H__
#define __UB_USB_MSC_H__

#ifdef __cplusplus
 extern "C" {
#endif

struct msc_ctx {
	const char *vid;
	const char *pid;
	const char *rev;

	uint16_t block_size;
	uint16_t num_blocks;
	uint8_t *data;
};

// Must be called before USB init!
void usb_msc_init(const struct msc_ctx *ctx);

#ifdef __cplusplus
 }
#endif

#endif /* __UB_USB_MSC_H__ */
