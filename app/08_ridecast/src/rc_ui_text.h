/* SPDX-License-Identifier: MIT
 * RideCast 128x64 OLED text fitting, alignment and compact 5x7 font.
 * Independent of Zephyr, Wi-Fi, and display drivers; suitable for host tests.
 */
#ifndef RC_UI_TEXT_H_
#define RC_UI_TEXT_H_

#include <stddef.h>

typedef enum {
    RC_UI_ALIGN_LEFT = 0,
    RC_UI_ALIGN_CENTER,
    RC_UI_ALIGN_RIGHT
} rc_ui_align_t;

typedef int (*rc_ui_span_fn)(void *ctx, int x0, int y, int x1);

/* Copy a string, adding ... only if it does not fit. Always NUL terminate.
 * max_chars is a number of glyphs, not bytes or pixels. ASCII UI messages.
 */
size_t rc_ui_text_fit(char *out, size_t capacity, const char *text,
                      size_t max_chars);

/* Layout helpers: coordinate bounds are inclusive for the text renderer. */
int rc_ui_text_origin(int left, int right, int pixel_width, rc_ui_align_t align);
int rc_ui_compact_width(const char *text); /* 5px glyph + 1px spacing */

/* Render into a horizontal strip without leaving the provided bounds.
 * The caller supplies a horizontal-span callback to draw the 5x7 glyphs.
 * Excess text is abbreviated with an ellipsis; no wrapping or overlap.
 */
int rc_ui_compact_draw(const char *text, int left, int right, int y,
                       int bottom, rc_ui_align_t align,
                       rc_ui_span_fn span, void *ctx);

#endif /* RC_UI_TEXT_H_ */
