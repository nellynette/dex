#include "buffer.h"
#include "uchar.h"

struct view *view;

void update_cursor_y(void)
{
	struct block *blk;
	unsigned int nl = 0;

	list_for_each_entry(blk, &buffer->blocks, node) {
		if (blk == view->cursor.blk) {
			nl += count_nl(blk->data, view->cursor.offset);
			view->cy = nl;
			return;
		}
		nl += blk->nl;
	}
	BUG_ON(1);
}

void update_cursor_x(void)
{
	unsigned int tw = buffer->options.tab_width;
	unsigned int idx = 0;
	struct lineref lr;
	int c = 0;
	int w = 0;

	view->cx = fetch_this_line(&view->cursor, &lr);
	while (idx < view->cx) {
		unsigned int u = (unsigned char)lr.line[idx++];

		c++;
		if (likely(u < 0x80)) {
			if (!u_is_ctrl(u)) {
				w++;
			} else if (u == '\t') {
				w = (w + tw) / tw * tw;
			} else {
				w += 2;
			}
		} else if (buffer->options.utf8) {
			idx--;
			u = u_buf_get_char(lr.line, lr.size, &idx);
			w += u_char_width(u);
		} else if (u > 0x9f) {
			w++;
		} else {
			w += 4;
		}
	}
	view->cx_char = c;
	view->cx_display = w;
}

void update_preferred_x(void)
{
	update_cursor_x();
	view->preferred_x = view->cx_display;
}
