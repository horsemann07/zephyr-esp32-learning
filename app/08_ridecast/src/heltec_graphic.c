/* SPDX-License-Identifier: MIT
 * Monochrome RideCast screen for the Heltec V2 128x64 SSD1306.
 * Tested initialization sequence taken from the project's prior OLED samples:
 * I2C recovery -> deferred device_init -> CFB -> display unblank.
 */
#include "heltec_graphic.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/display/cfb.h>
#include <zephyr/sys/printk.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RC_OLED_W 128
#define RC_OLED_H 64
/* Reserve a right margin and a bottom margin for real-panel alignment. */
#define RC_UI_RIGHT 112
#define RC_UI_BOTTOM 60
#define RC_I2C_NODE DT_NODELABEL(i2c0)

#if !DT_HAS_CHOSEN(zephyr_display)
#error "Select shield ssd1306_128x64 (zephyr,display missing)"
#endif
#define RC_OLED_NODE DT_CHOSEN(zephyr_display)
#if !DT_NODE_HAS_STATUS(RC_I2C_NODE, okay)
#error "Heltec I2C0 must be enabled in app.overlay"
#endif
#if !DT_NODE_HAS_STATUS(RC_OLED_NODE, okay)
#error "SSD1306 must be enabled in app.overlay"
#endif
#if !DT_PROP(RC_OLED_NODE, zephyr_deferred_init)
#error "SSD1306 needs zephyr,deferred-init in app.overlay"
#endif

static const struct device *const i2c_dev = DEVICE_DT_GET(RC_I2C_NODE);
static const struct device *const oled_dev = DEVICE_DT_GET(RC_OLED_NODE);
static bool initialized;
static uint8_t font_width = 8;
static uint8_t font_height = 8;

#define DRAW_CHECK(expression) do { int _rc = (expression); if (_rc < 0) return _rc; } while (0)

static int draw_line(int x0, int y0, int x1, int y1)
{
    const struct cfb_position a = { (int16_t)x0, (int16_t)y0 };
    const struct cfb_position b = { (int16_t)x1, (int16_t)y1 };
    return cfb_draw_line(oled_dev, &a, &b);
}

/* Never send a line longer than the available pixels to the CFB driver. */
static int draw_text(const char *s, int x, int y)
{
    char out[24];
    size_t max_chars;
    size_t n;
    if (s == NULL || x < 0 || x >= RC_UI_RIGHT || y < 0 ||
        y + font_height > RC_UI_BOTTOM || font_width == 0U) {
        return -EINVAL;
    }
    max_chars = (size_t)(RC_UI_RIGHT - x) / font_width;
    if (max_chars > sizeof(out) - 1U) {
        max_chars = sizeof(out) - 1U;
    }
    n = strlen(s);
    if (n > max_chars) {
        n = max_chars;
    }
    memcpy(out, s, n);
    out[n] = '\0';
    return cfb_print(oled_dev, out, (int16_t)x, (int16_t)y);
}

/* v0.5 screen layout, physical SSD1306 pixels (128x64):
 *   mini header "RC" + "SIM/OFF/WIFI/ONLINE"  y=3..7
 *   arrow and 7-segment distance             y=16..36
 *   turn instruction (Zephyr CFB font)       y=39..46
 *   speed (CFB font)                        y=50..57
 *   GPS: (3x5 micro-font) + OK/NO (CFB)     y=50..57
 *
 * Intentionally NO horizontal separator lines. The GPS status is intentionally
 * a separate, fixed-width field so it never collides with speed at 240km/h.
 * Leave 16px from the right edge and 6px from the bottom.
 */

/* 3x5 micro-font used ONLY for the tiny top-left/right status labels.
 * The main instruction, speed and distance font sizes are unchanged.
 * Each row is a 3-bit mask (bit 2 is leftmost pixel).
 */
static const uint8_t *micro_glyph(char ch)
{
    static const uint8_t glyph_r[5] = { 6, 5, 6, 5, 5 };
    static const uint8_t glyph_c[5] = { 3, 4, 4, 4, 3 };
    static const uint8_t glyph_s[5] = { 3, 4, 2, 1, 6 };
    static const uint8_t glyph_i[5] = { 7, 2, 2, 2, 7 };
    static const uint8_t glyph_m[5] = { 5, 7, 7, 5, 5 };
    static const uint8_t glyph_o[5] = { 2, 5, 5, 5, 2 };
    static const uint8_t glyph_f[5] = { 7, 4, 6, 4, 4 };
    static const uint8_t glyph_w[5] = { 5, 5, 7, 7, 5 };
    static const uint8_t glyph_n[5] = { 5, 7, 7, 7, 5 };
    static const uint8_t glyph_l[5] = { 4, 4, 4, 4, 7 };
    static const uint8_t glyph_e[5] = { 7, 4, 6, 4, 7 };
    static const uint8_t glyph_g[5] = { 3, 4, 5, 5, 3 };
    static const uint8_t glyph_p[5] = { 6, 5, 6, 4, 4 };
    static const uint8_t glyph_colon[5] = { 0, 2, 0, 2, 0 };
    switch (ch) {
    case 'R': return glyph_r;
    case 'C': return glyph_c;
    case 'S': return glyph_s;
    case 'I': return glyph_i;
    case 'M': return glyph_m;
    case 'O': return glyph_o;
    case 'F': return glyph_f;
    case 'W': return glyph_w;
    case 'N': return glyph_n;
    case 'L': return glyph_l;
    case 'E': return glyph_e;
    case 'G': return glyph_g;
    case 'P': return glyph_p;
    case ':': return glyph_colon;
    default:  return NULL;
    }
}

static int draw_micro_text(const char *text, int x, int y)
{
    if (text == NULL || x < 0 || y < 0) {
        return -EINVAL;
    }
    size_t count = strlen(text);
    /* each glyph is 3 wide; inter-character gap is 1 pixel */
    if (count == 0U || count > 16U ||
        x + (int)(count * 4U - 1U) > RC_UI_RIGHT || y + 5 > RC_UI_BOTTOM) {
        return -EINVAL;
    }
    for (size_t i = 0U; i < count; ++i) {
        const uint8_t *g = micro_glyph(text[i]);
        if (g == NULL) {
            return -EINVAL;
        }
        const int gx = x + (int)(i * 4U);
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 3; ++col) {
                if ((g[row] & (uint8_t)(4U >> col)) != 0U) {
                    DRAW_CHECK(draw_line(gx + col, y + row,
                                         gx + col, y + row));
                }
            }
        }
    }
    return 0;
}

static int segment(int x0, int y0, uint8_t which)
{
    switch (which) {
    case 0: return draw_line(x0 + 2, y0,     x0 + 9,  y0);       /* top */
    case 1: return draw_line(x0 + 10,y0 + 1, x0 + 10, y0 + 8);  /* top right */
    case 2: return draw_line(x0 + 10,y0 + 10,x0 + 10, y0 + 17); /* bottom right */
    case 3: return draw_line(x0 + 2, y0 + 18,x0 + 9, y0 + 18);  /* bottom */
    case 4: return draw_line(x0 + 1, y0 + 10,x0 + 1, y0 + 17);  /* bottom left */
    case 5: return draw_line(x0 + 1, y0 + 1, x0 + 1, y0 + 8);   /* top left */
    case 6: return draw_line(x0 + 2, y0 + 9, x0 + 9, y0 + 9);   /* middle */
    default: return -EINVAL;
    }
}

static int big_digit(uint8_t digit, int x, int y)
{
    static const uint8_t bitmap[] = {
        0x3f, /* 0: A B C D E F */
        0x06, /* 1: B C */
        0x5b, /* 2: A B D E G */
        0x4f, /* 3: A B C D G */
        0x66, /* 4: B C F G */
        0x6d, /* 5: A C D F G */
        0x7d, /* 6: A C D E F G */
        0x07, /* 7: A B C */
        0x7f, /* 8: all */
        0x6f  /* 9: A B C D F G */
    };
    if (digit > 9U) {
        return -EINVAL;
    }
    for (uint8_t seg = 0; seg < 7U; ++seg) {
        if ((bitmap[digit] & (1U << seg)) != 0U) {
            DRAW_CHECK(segment(x, y, seg));
        }
    }
    return 0;
}

static int draw_distance(uint16_t meters)
{
    if (meters > 999U) {
        meters = 999U; /* display limit; underlying model is unchanged */
    }

    /* Center the complete number + unit in the 50..126 pixel region. */
    const int digits = (meters >= 100U) ? 3 : (meters >= 10U ? 2 : 1);
    int x = (digits == 3) ? 50 : (digits == 2 ? 64 : 78);
    if (meters >= 100U) {
        DRAW_CHECK(big_digit((uint8_t)(meters / 100U), x, 16));
        x += 14;
    }
    if (meters >= 10U) {
        DRAW_CHECK(big_digit((uint8_t)((meters / 10U) % 10U), x, 16));
        x += 14;
    }
    DRAW_CHECK(big_digit((uint8_t)(meters % 10U), x, 16));
    x += 15;
    return draw_text("m", x, 22);
}

static int draw_turn_icon(heltec_turn_t turn)
{
    switch (turn) {
    case HELTEC_TURN_RIGHT:
        DRAW_CHECK(draw_line(12, 36, 12, 26));
        DRAW_CHECK(draw_line(12, 26, 35, 26));
        DRAW_CHECK(draw_line(35, 26, 27, 18));
        return draw_line(35, 26, 27, 34);
    case HELTEC_TURN_LEFT:
        DRAW_CHECK(draw_line(35, 36, 35, 26));
        DRAW_CHECK(draw_line(35, 26, 12, 26));
        DRAW_CHECK(draw_line(12, 26, 20, 18));
        return draw_line(12, 26, 20, 34);
    case HELTEC_TURN_STRAIGHT:
        DRAW_CHECK(draw_line(24, 36, 24, 17));
        DRAW_CHECK(draw_line(24, 17, 16, 25));
        return draw_line(24, 17, 32, 25);
    case HELTEC_TURN_ARRIVED:
        DRAW_CHECK(draw_line(12, 27, 20, 36));
        return draw_line(20, 36, 37, 17);
    case HELTEC_TURN_NONE:
        DRAW_CHECK(draw_line(18, 20, 29, 20));
        DRAW_CHECK(draw_line(29, 20, 29, 28));
        return draw_line(24, 33, 24, 35);
    default:
        return -EINVAL;
    }
}

static void select_compact_font(void)
{
    int best = -1;
    uint8_t best_w = 255U;
    uint8_t best_h = 255U;
    int font_count = cfb_get_numof_fonts(oled_dev);
    for (int i = 0; i < font_count; ++i) {
        uint8_t w = 0U;
        uint8_t h = 0U;
        if (cfb_get_font_size(oled_dev, (uint8_t)i, &w, &h) == 0 &&
            w > 0U && w <= 8U && h > 0U && h <= 8U &&
            (best < 0 || w < best_w || (w == best_w && h < best_h))) {
            best = i;
            best_w = w;
            best_h = h;
        }
    }
    if (best >= 0 && cfb_framebuffer_set_font(oled_dev, (uint8_t)best) == 0) {
        font_width = best_w;
        font_height = best_h;
    }
    printk("RideCast OLED font %ux%u\n", font_width, font_height);
}

rc_err_sts_t heltec_graphic_init(void)
{
    if (initialized) {
        return RC_ERR_ALREADY_INITIALIZED;
    }
    if (!device_is_ready(i2c_dev)) {
        return RC_ERR_NOT_READY;
    }
    int status = i2c_recover_bus(i2c_dev);
    if (status < 0) {
        printk("OLED I2C recovery failed: %d\n", status);
        return RC_ERR_IO;
    }
    status = device_init(oled_dev);
    if (status < 0 || !device_is_ready(oled_dev)) {
        printk("OLED device initialization failed: %d\n", status);
        return RC_ERR_INIT_FAILED;
    }
    status = display_set_pixel_format(oled_dev, PIXEL_FORMAT_MONO01);
    if (status < 0) {
        return RC_ERR_NOT_SUPPORTED;
    }
    status = cfb_framebuffer_init(oled_dev);
    if (status < 0) {
        return RC_ERR_INIT_FAILED;
    }
    select_compact_font();
    status = cfb_framebuffer_clear(oled_dev, false);
    if (status < 0 || display_blanking_off(oled_dev) < 0) {
        return RC_ERR_IO;
    }
    initialized = true;
    return RC_OK;
}

rc_err_sts_t heltec_graphic_render(const heltec_nav_screen_t *screen)
{
    if (!initialized) {
        return RC_ERR_NOT_INITIALIZED;
    }
    if (screen == NULL || screen->turn > HELTEC_TURN_ARRIVED ||
        screen->link > HELTEC_LINK_ONLINE || screen->speed_kmh > 240U) {
        return RC_ERR_INVALID_ARG;
    }
    const char *link_label;
    switch (screen->link) {
    case HELTEC_LINK_DEMO:    link_label = "SIM"; break;
    case HELTEC_LINK_OFFLINE: link_label = "OFF"; break;
    case HELTEC_LINK_WIFI:    link_label = "WIFI"; break;
    case HELTEC_LINK_ONLINE:  link_label = "ONLINE"; break;
    default:                 return RC_ERR_INVALID_ARG;
    }
    const char *turn_label;
    switch (screen->turn) {
    case HELTEC_TURN_RIGHT:    turn_label = "TURN RIGHT"; break;
    case HELTEC_TURN_LEFT:     turn_label = "TURN LEFT"; break;
    case HELTEC_TURN_STRAIGHT: turn_label = "STRAIGHT"; break;
    case HELTEC_TURN_ARRIVED:  turn_label = "ARRIVED"; break;
    case HELTEC_TURN_NONE:     turn_label = "WAIT NAV"; break;
    default:                   return RC_ERR_INVALID_ARG;
    }
    char footer[20];
    (void)snprintf(footer, sizeof(footer), "%ukm/h", (unsigned int)screen->speed_kmh);

    /* Small permanent product label; no debug version or separator lines. */
    const char *heading = "RC";
    const char *gps_result = screen->gps_valid ? "OK" : "NO";
    /* 3x5 micro-font has a four-pixel character pitch. */
    const int link_x = RC_UI_RIGHT - 4 - (int)(4U * strlen(link_label) - 1U);
    /* Physical layout measured on the real Heltec panel:
     *   speed: x=4..59 at maximum "240km/h" with an 8px CFB font
     *   GPS:   x=75..89 (3x5) -- clearly separated from speed
     *   OK/NO: x=96..111 (CFB) -- ends 16px before physical edge
     */
    const int gps_micro_x = 75;
    const int gps_result_x = RC_UI_RIGHT - (int)(font_width * strlen(gps_result));

    int status = cfb_framebuffer_clear(oled_dev, false);
    if (status >= 0) status = draw_micro_text(heading, 4, 3);
    if (status >= 0) status = draw_micro_text(link_label, link_x, 3);
    if (status >= 0) status = draw_turn_icon(screen->turn);
    if (status >= 0) status = draw_distance(screen->distance_m);
    /* Instruction now has its OWN full-width row, not beside the digits. */
    if (status >= 0) status = draw_text(turn_label, 4, 39);
    if (status >= 0) status = draw_text(footer, 4, 50);
    if (status >= 0) status = draw_micro_text("GPS:", gps_micro_x, 52);
    if (status >= 0) status = draw_text(gps_result, gps_result_x, 50);
    if (status >= 0) status = cfb_framebuffer_finalize(oled_dev);
    return status < 0 ? RC_ERR_IO : RC_OK;
}

/* Short, non-overlapping, NO separator lines or border.
 * OLED y rows: micro header 3..7; text y=14,26,38,50.
 * OLED physical edge 128x64; usable right<=112 and bottom<60.
 */
rc_err_sts_t heltec_graphic_render_connection(const rc_demo_snapshot_t *s)
{
    if (!initialized) return RC_ERR_NOT_INITIALIZED;
    if (s == NULL) return RC_ERR_INVALID_ARG;
    const char *title = "WAIT APP";
    const char *main_line = "WIFI + BLE";
    const char *detail = "READY";
    char pin_line[24] = "";
    char footer[20];
    const char *radio = "NONE";
    if (s->active == RC_DEMO_LINK_WIFI) radio = "WIFI";
    else if (s->active == RC_DEMO_LINK_BLE) radio = "BLE";
    switch (s->stage) {
    case RC_DEMO_WIFI_ENROLL:
        title = "WIFI PAIR";
        detail = "ENTER IN APP";
        main_line = "6 DIGIT CODE";
        break;
    case RC_DEMO_WIFI_TOKEN:
        title = "WIFI AUTH";
        main_line = "SAVED TOKEN";
        detail = "WAIT APP";
        break;
    case RC_DEMO_BLE_PAIR:
        title = "BLE PAIR";
        main_line = "PHONE: PAIR";
        detail = "ENTER PIN";
        break;
    case RC_DEMO_ACTIVE:
        title = "CONN OK";
        main_line = radio;
        detail = "AUTH SAVED";
        break;
    case RC_DEMO_FAILED:
        title = "CONN FAIL";
        main_line = "CHECK CODE";
        detail = "TRY RECONNECT";
        break;
    case RC_DEMO_WAIT:
    default:
        break;
    }
    if (s->pin_visible) {
        (void)snprintf(pin_line, sizeof(pin_line), "PIN: %06u", (unsigned)s->pin);
        main_line = pin_line;
    }
    (void)snprintf(footer, sizeof(footer), "W:%s B:%s",
                   s->wifi_ready ? "ON" : "--",
                   s->ble_ready ? "ON" : "--");
    int r = cfb_framebuffer_clear(oled_dev, false);
    if (r >= 0) r = draw_micro_text("RC", 4, 3);
    if (r >= 0) r = draw_text(title, 4, 14);
    if (r >= 0) r = draw_text(main_line, 4, 26);
    if (r >= 0) r = draw_text(detail, 4, 38);
    if (r >= 0) r = draw_text(footer, 4, 50);
    if (r >= 0) r = cfb_framebuffer_finalize(oled_dev);
    return r < 0 ? RC_ERR_IO : RC_OK;
}
