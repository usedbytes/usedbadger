#ifndef __UB_BADGER_H__
#define __UB_BADGER_H__

#ifdef __cplusplus
 extern "C" {
#endif

#include <stdint.h>

enum badger_pin {
	BADGER_PIN_A           = 12,
	BADGER_PIN_B           = 13,
	BADGER_PIN_C           = 14,
	BADGER_PIN_D           = 15,
	BADGER_PIN_E           = 11,
	BADGER_PIN_UP          = 15, // alias for D
	BADGER_PIN_DOWN        = 11, // alias for E
	BADGER_PIN_USER        = 23,
	BADGER_PIN_CS          = 17,
	BADGER_PIN_CLK         = 18,
	BADGER_PIN_MOSI        = 19,
	BADGER_PIN_DC          = 20,
	BADGER_PIN_RESET       = 21,
	BADGER_PIN_BUSY        = 26,
	BADGER_PIN_VBUS_DETECT = 24,
	BADGER_PIN_LED         = 25,
	BADGER_PIN_BATTERY     = 29,
	BADGER_PIN_ENABLE_3V3  = 10
};

void badger_init(void);

void badger_update(bool blocking);
void badger_partial_update(int x, int y, int w, int h, bool blocking);
void badger_update_speed(uint8_t speed);
uint32_t badger_update_time();
void badger_halt();
void badger_sleep();
bool badger_is_busy();
void badger_power_off();
void badger_invert(bool invert);

// state
void badger_led(uint8_t brightness);
void badger_font(const char *name);
void badger_pen(uint8_t pen);
void badger_thickness(uint8_t thickness);

// inputs (buttons: A, B, C, D, E, USER)
bool badger_pressed(uint8_t button);
bool badger_pressed_to_wake(uint8_t button);
void badger_wait_for_press();
void badger_update_button_states();
uint32_t badger_button_states();

// drawing primitives
void badger_clear();
void badger_pixel(int32_t x, int32_t y);
void badger_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2);
void badger_rectangle(int32_t x, int32_t y, int32_t w, int32_t h);

void badger_icon(const uint8_t *data, int sheet_width, int icon_size, int index, int dx, int dy);
void badger_image_fullscreen(const uint8_t *data);
void badger_image(const uint8_t *data, int w, int h, int x, int y);
void badger_subimage(const uint8_t *data, int stride, int sx, int sy, int dw, int dh, int dx, int dy);

void badger_text(const char *message, int32_t x, int32_t y, float s, float a, uint8_t letter_spacing);
int32_t badger_glyph(unsigned char c, int32_t x, int32_t y, float s, float a);

int32_t badger_measure_text(const char *message, float s, uint8_t letter_spacing);
int32_t badger_measure_glyph(unsigned char c, float s);

#ifdef __cplusplus
 }
#endif

#endif /* __UB_BADGER_H__ */
