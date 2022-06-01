#include <stdio.h>

#include "screen_page.h"

#include "badger.h"

// Note: MUST BE LAST!
#define LAY_IMPLEMENTATION
#include "layout.h"

#define IMAGE_MARGINS lay_vec4_xyzw(4, 4, 4, 4)
#define TEXT_MARGINS  lay_vec4_xyzw(4, 4, 4, 4)

static lay_vec4 page_item_get_margins(struct screen_page_item *item)
{
	switch (item->type) {
	case PAGE_ITEM_TYPE_IMAGE:
		return IMAGE_MARGINS;
	case PAGE_ITEM_TYPE_TEXT:
		return TEXT_MARGINS;
	default:
		return lay_vec4_xyzw(0, 0, 0, 0);
	}
}

static void page_item_image_draw(struct screen_page_item *item, lay_vec4 rect)
{
	badger_pen(15);
	badger_image(item->image.data, item->width, item->height, rect[0], rect[1]);
}

static void page_item_text_draw(struct screen_page_item *item, lay_vec4 rect)
{
	/*
	badger_pen(0);
	badger_thickness(1);
	badger_line(rect[0], rect[1], rect[0] + rect[2], rect[1]);
	badger_line(rect[0] + rect[2], rect[1], rect[0] + rect[2], rect[1] + rect[3]);
	badger_line(rect[0], rect[1] + rect[3], rect[0] + rect[2], rect[1] + rect[3]);
	badger_line(rect[0], rect[1], rect[0], rect[1] + rect[3]);
	*/

	badger_pen(item->text.color);
	badger_thickness(item->text.thickness);
	badger_text(item->text.text, rect[0] + item->text.thickness / 2, rect[1] + (item->height / 2), item->text.size, 0.0f, 1);
}

static void page_item_draw(struct screen_page_item *item, lay_vec4 rect)
{
	switch (item->type) {
	case PAGE_ITEM_TYPE_IMAGE:
		page_item_image_draw(item, rect);
		break;
	case PAGE_ITEM_TYPE_TEXT:
		page_item_text_draw(item, rect);
		break;
	}

	return;
}

static void page_item_image_calculate_size(struct screen_page_item *item)
{
	item->width = item->image.width;
	item->height = item->image.height;
}

static void page_item_text_calculate_size(struct screen_page_item *item)
{
	item->width = badger_measure_text(item->text.text, item->text.size, 1) + item->text.thickness;
	item->height = (item->text.size * HERSHEY_HEIGHT) + item->text.thickness;
}

void page_item_calculate_size(struct screen_page_item *item)
{
	switch (item->type) {
	case PAGE_ITEM_TYPE_IMAGE:
		page_item_image_calculate_size(item);
		break;
	case PAGE_ITEM_TYPE_TEXT:
		page_item_text_calculate_size(item);
		break;
	}

	return;
}

void screen_page_calculate_sizes(struct screen_page *page)
{
	for (int i = 0; i < page->n_items; i++) {
		page_item_calculate_size(&page->items[i]);
	}
}

// Use https://github.com/randrew/layout
void screen_page_display(struct screen_page *page)
{
	lay_context ctx;

	lay_init_context(&ctx);

	// Root item plus up-to two columns
	lay_reserve_items_capacity(&ctx, page->n_items + 1 + 2);

	lay_id root = lay_item(&ctx);
	lay_set_size_xy(&ctx, root, BADGER_WIDTH, BADGER_HEIGHT);
	lay_set_contain(&ctx, root, LAY_ROW);

	struct screen_page_item *items = page->items;

	if ((items[0].type == PAGE_ITEM_TYPE_IMAGE) && (items[0].width <= BADGER_WIDTH / 2) && (page->n_items > 1)) {
		// Two columns
		lay_id left_col = lay_item(&ctx);
		lay_insert(&ctx, root, left_col);
		lay_set_size_xy(&ctx, left_col, items[0].width, 0);
		lay_set_behave(&ctx, left_col, LAY_VFILL);
		lay_set_contain(&ctx, left_col, LAY_COLUMN | LAY_MIDDLE);

		lay_id img = lay_item(&ctx);
		lay_insert(&ctx, left_col, img);
		lay_set_size_xy(&ctx, img, items[0].width, items[0].height);
		lay_set_behave(&ctx, img, LAY_VCENTER | LAY_HCENTER);
		lay_set_margins(&ctx, img, IMAGE_MARGINS);
		items[0].lay_id = img;

		lay_id right_col = lay_item(&ctx);
		lay_insert(&ctx, root, right_col);
		lay_set_behave(&ctx, right_col, LAY_VFILL | LAY_HFILL);
		lay_set_contain(&ctx, right_col, LAY_COLUMN | LAY_MIDDLE);

		for (int i = 1; i < page->n_items; i++) {
			struct screen_page_item *item = &items[i];

			lay_id lay = lay_item(&ctx);
			lay_insert(&ctx, right_col, lay);
			lay_set_size_xy(&ctx, lay, item->width, item->height);
			lay_set_behave(&ctx, lay, LAY_LEFT);
			lay_set_margins(&ctx, lay, page_item_get_margins(item));
			item->lay_id = lay;
		}
	} else {
		// One column
		lay_id col = lay_item(&ctx);
		lay_insert(&ctx, root, col);
		lay_set_size_xy(&ctx, col, items[0].width, 0);
		lay_set_behave(&ctx, col, LAY_FILL);
		lay_set_contain(&ctx, col, LAY_COLUMN | LAY_MIDDLE);

		for (int i = 0; i < page->n_items; i++) {
			struct screen_page_item *item = &items[i];
			lay_vec4 margins;

			lay_id lay = lay_item(&ctx);
			lay_insert(&ctx, col, lay);
			lay_set_size_xy(&ctx, lay, item->width, item->height);
			lay_set_behave(&ctx, lay, LAY_CENTER);
			lay_set_margins(&ctx, lay, page_item_get_margins(item));
			item->lay_id = lay;
		}
	}

	lay_run_context(&ctx);

	badger_pen(15);
	badger_clear();

	for (int i = 0; i < page->n_items; i++) {
		struct screen_page_item *item = &items[i];
		lay_vec4 rect = lay_get_rect(&ctx, item->lay_id);

		printf("%d: { %d, %d, %d, %d }\n", i, rect[0], rect[1], rect[2], rect[3]);
		page_item_draw(item, rect);
	}

	badger_update(true);
}
