#include "common/pimoroni_common.hpp"
#include "badger2040.hpp"

#include "badger.h"

using namespace pimoroni;

Badger2040 badger;

void badger_init(void)
{
	badger.init();
}

void badger_update(bool blocking)
{
	badger.update(blocking);
}

void badger_partial_update(int x, int y, int w, int h, bool blocking)
{
	badger.partial_update(x, y, w, h, blocking);
}

void badger_update_speed(uint8_t speed)
{
	badger.update_speed(speed);
}

uint32_t badger_update_time()
{
	return badger.update_time();
}

void badger_halt()
{
	badger.halt();
}

void badger_sleep()
{
	badger.sleep();
}

bool badger_is_busy()
{
	return badger.is_busy();
}

void badger_power_off()
{
	badger.power_off();
}

void badger_invert(bool invert)
{
	badger.invert(invert);
}

// state
void badger_led(uint8_t brightness)
{
	badger.led(brightness);
}

void badger_font(const char *name)
{
	badger.font(std::string(name));
}

void badger_pen(uint8_t pen)
{
	badger.pen(pen);
}

void badger_thickness(uint8_t thickness)
{
	badger.thickness(thickness);
}

// inputs (buttons: A, B, C, D, E, USER)
bool badger_pressed(uint8_t button)
{
	return badger.pressed(button);
}

bool badger_pressed_to_wake(uint8_t button)
{
	return badger.pressed_to_wake(button);
}

void badger_wait_for_press()
{
	badger.wait_for_press();
}

void badger_update_button_states()
{
	badger.update_button_states();
}

uint32_t badger_button_states()
{
	return badger.button_states();
}

// drawing primitives
void badger_clear()
{
	badger.clear();
}

void badger_pixel(int32_t x, int32_t y)
{
	badger.pixel(x, y);
}

void badger_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
	badger.line(x1, y1, x2, y2);
}

void badger_rectangle(int32_t x, int32_t y, int32_t w, int32_t h)
{
	badger.rectangle(x, y, w, h);
}

void badger_icon(const uint8_t *data, int sheet_width, int icon_size, int index, int dx, int dy)
{
	badger.icon(data, sheet_width, icon_size, index, dx, dy);
}

void badger_image_fullscreen(const uint8_t *data)
{
	badger.image(data);
}

void badger_image(const uint8_t *data, int w, int h, int x, int y)
{
	badger.image(data, w, h, x, y);
}

void badger_subimage(const uint8_t *data, int stride, int sx, int sy, int dw, int dh, int dx, int dy)
{
	badger.image(data, stride, sx, sy, dw, dh, dx, dy);
}

void badger_text(const char *message, int32_t x, int32_t y, float s, float a, uint8_t letter_spacing)
{
	badger.text(std::string(message), x, y, s, a, letter_spacing);
}

int32_t badger_glyph(unsigned char c, int32_t x, int32_t y, float s, float a)
{
	return badger.glyph(c, x, y, s, a);
}

int32_t badger_measure_text(const char *message, float s, uint8_t letter_spacing)
{
	return badger.measure_text(std::string(message), s, letter_spacing);
}

int32_t badger_measure_glyph(unsigned char c, float s)
{
	return badger.measure_glyph(c, s);
}
