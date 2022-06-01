#ifndef __SCREEN_PAGE_H__
#define __SCREEN_PAGE_H__

#include <stdint.h>

#include "layout.h"

enum page_item_type {
	PAGE_ITEM_TYPE_IMAGE = 1,
	PAGE_ITEM_TYPE_TEXT,
};

// Direct from the file
struct screen_page_item {
	// Needed by layout engine
	int width, height;

	// Updated during layout
	lay_id lay_id;

	enum page_item_type type;
	union {
		// img WIDTH HEIGHT path.img
		struct {
			int width, height;
			uint8_t *data;
		} image;
		// text[.font] SIZE COLOR THICKNESS Text to display
		struct {
			float size;
			uint8_t color;
			uint8_t thickness;
			char *text;
		} text;
	};
};

struct screen_page {
	int n_items;
	struct screen_page_item *items;
};

void screen_page_calculate_sizes(struct screen_page *page);
void screen_page_display(struct screen_page *page);
void page_item_calculate_size(struct screen_page_item *item);

#endif /* __SCREEN_PAGE_H__ */
