/* SPDX-License-Identifier: MIT */
#include "rc_ui_text.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

#define GLYPH_WIDTH  5
#define GLYPH_HEIGHT 7
#define GLYPH_ADVANCE 6
#define COMPACT_TEXT_CAP 32

/* Each entry contains seven 5-bit rows, MSB (bit 4) on the left.
 * Custom tiny font deliberately covers the full RideCast UI alphabet.
 * Lowercase is mapped to uppercase in compact status text.
 */
typedef struct {
    char ch;
    uint8_t row[GLYPH_HEIGHT];
} glyph_t;

static const glyph_t glyphs[] = {
    {' ', {0,0,0,0,0,0,0}},
    {'0', {14,17,19,21,25,17,14}},
    {'1', {4,12,4,4,4,4,14}},
    {'2', {14,17,1,2,4,8,31}},
    {'3', {30,1,1,14,1,1,30}},
    {'4', {2,6,10,18,31,2,2}},
    {'5', {31,16,30,1,1,17,14}},
    {'6', {6,8,16,30,17,17,14}},
    {'7', {31,1,2,4,8,8,8}},
    {'8', {14,17,17,14,17,17,14}},
    {'9', {14,17,17,15,1,2,12}},
    {'A', {14,17,17,31,17,17,17}},
    {'B', {30,17,17,30,17,17,30}},
    {'C', {14,17,16,16,16,17,14}},
    {'D', {30,17,17,17,17,17,30}},
    {'E', {31,16,16,30,16,16,31}},
    {'F', {31,16,16,30,16,16,16}},
    {'G', {14,17,16,23,17,17,15}},
    {'H', {17,17,17,31,17,17,17}},
    {'I', {14,4,4,4,4,4,14}},
    {'J', {1,1,1,1,17,17,14}},
    {'K', {17,18,20,24,20,18,17}},
    {'L', {16,16,16,16,16,16,31}},
    {'M', {17,27,21,21,17,17,17}},
    {'N', {17,25,21,19,17,17,17}},
    {'O', {14,17,17,17,17,17,14}},
    {'P', {30,17,17,30,16,16,16}},
    {'Q', {14,17,17,17,21,18,13}},
    {'R', {30,17,17,30,20,18,17}},
    {'S', {15,16,16,14,1,1,30}},
    {'T', {31,4,4,4,4,4,4}},
    {'U', {17,17,17,17,17,17,14}},
    {'V', {17,17,17,17,17,10,4}},
    {'W', {17,17,17,21,21,21,10}},
    {'X', {17,17,10,4,10,17,17}},
    {'Y', {17,17,10,4,4,4,4}},
    {'Z', {31,1,2,4,8,16,31}},
    {':', {0,4,4,0,4,4,0}},
    {'.', {0,0,0,0,0,12,12}},
    {'-', {0,0,0,31,0,0,0}},
    {'/', {1,1,2,4,8,16,16}},
    {'+', {0,4,4,31,4,4,0}},
    {'_', {0,0,0,0,0,0,31}},
    {'?', {14,17,1,2,4,0,4}},
    {'!', {4,4,4,4,4,0,4}},
    {'%', {25,25,2,4,8,19,19}},
    {'(', {2,4,8,8,8,4,2}},
    {')', {8,4,2,2,2,4,8}},
    {'#', {10,10,31,10,31,10,10}},
    {'=', {0,0,31,0,31,0,0}},
};

static const uint8_t *glyph_rows(char ch)
{
    /* Preserve legible lowercase in speed and distance units. */
    static const uint8_t lower_h[GLYPH_HEIGHT] = {16,16,30,17,17,17,17};
    static const uint8_t lower_k[GLYPH_HEIGHT] = {16,16,18,20,24,20,18};
    static const uint8_t lower_m[GLYPH_HEIGHT] = {0,0,26,21,21,21,21};
    if (ch == 'h') return lower_h;
    if (ch == 'k') return lower_k;
    if (ch == 'm') return lower_m;
    if (ch >= 'a' && ch <= 'z') {
        ch = (char)(ch - 'a' + 'A');
    }
    for (size_t i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); ++i) {
        if (glyphs[i].ch == ch) {
            return glyphs[i].row;
        }
    }
    static const uint8_t unknown[GLYPH_HEIGHT] = {14,17,1,2,4,0,4};
    return unknown; /* ? */
}

size_t rc_ui_text_fit(char *out, size_t capacity, const char *text,
                      size_t max_chars)
{
    if (out == NULL || capacity == 0U) {
        return 0U;
    }
    out[0] = '\0';
    if (text == NULL || max_chars == 0U) {
        return 0U;
    }
    if (max_chars > capacity - 1U) {
        max_chars = capacity - 1U;
    }
    size_t len = strlen(text);
    if (len <= max_chars) {
        memcpy(out, text, len + 1U);
        return len;
    }
    if (max_chars < 4U) {
        memcpy(out, text, max_chars);
    } else {
        size_t kept = max_chars - 3U;
        memcpy(out, text, kept);
        memcpy(out + kept, "...", 3U);
    }
    out[max_chars] = '\0';
    return max_chars;
}

int rc_ui_text_origin(int left, int right, int pixel_width, rc_ui_align_t align)
{
    if (right < left || pixel_width < 0) {
        return left;
    }
    int available = right - left + 1;
    if (pixel_width > available) {
        pixel_width = available;
    }
    if (align == RC_UI_ALIGN_CENTER) {
        return left + (available - pixel_width) / 2;
    }
    if (align == RC_UI_ALIGN_RIGHT) {
        return right - pixel_width + 1;
    }
    return left;
}

int rc_ui_compact_width(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    size_t n = strlen(text);
    /* Saturate rather than overflow for arbitrary inputs. */
    if (n > 10000U) {
        return 60000;
    }
    return (int)(n * GLYPH_ADVANCE - 1U);
}

int rc_ui_compact_draw(const char *text, int left, int right, int y,
                       int bottom, rc_ui_align_t align,
                       rc_ui_span_fn span, void *ctx)
{
    if (text == NULL || span == NULL || left < 0 || right < left ||
        y < 0 || y + GLYPH_HEIGHT - 1 > bottom) {
        return -EINVAL;
    }
    int available = right - left + 1;
    size_t max_chars = (size_t)(available + 1) / GLYPH_ADVANCE;
    if (max_chars > COMPACT_TEXT_CAP - 1U) {
        max_chars = COMPACT_TEXT_CAP - 1U;
    }
    char label[COMPACT_TEXT_CAP];
    size_t n = rc_ui_text_fit(label, sizeof(label), text, max_chars);
    int width = rc_ui_compact_width(label);
    int x = rc_ui_text_origin(left, right, width, align);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t *rows = glyph_rows(label[i]);
        int gx = x + (int)(i * GLYPH_ADVANCE);
        for (int r = 0; r < GLYPH_HEIGHT; ++r) {
            uint8_t bits = rows[r];
            int c = 0;
            while (c < GLYPH_WIDTH) {
                if ((bits & (1U << (GLYPH_WIDTH - 1 - c))) == 0U) {
                    ++c;
                    continue;
                }
                int start = c;
                while (c < GLYPH_WIDTH &&
                       (bits & (1U << (GLYPH_WIDTH - 1 - c))) != 0U) {
                    ++c;
                }
                /* Every pixel remains inside the bounding box. */
                int result = span(ctx, gx + start, y + r, gx + c - 1);
                if (result < 0) {
                    return result;
                }
            }
        }
    }
    return 0;
}
