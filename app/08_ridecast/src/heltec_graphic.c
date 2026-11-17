/* SPDX-License-Identifier: MIT
 * Monochrome RideCast screen for the Heltec V2 128x64 SSD1306.
 * Tested initialization sequence taken from the project's prior OLED samples:
 * I2C recovery -> deferred device_init -> CFB -> display unblank.
 */
#include "heltec_graphic.h"
#include "rc_ui_text.h"

#include <zephyr/kernel.h>
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

#define RC_OLED_W    128
#define RC_OLED_H    64
/* SSD1306 has 128 real columns. Use safe 4px side and 2px bottom margins. */
#define RC_UI_LEFT   4
#define RC_UI_RIGHT  123 /* inclusive */
#define RC_UI_BOTTOM 61  /* inclusive */
#define RC_I2C_NODE  DT_NODELABEL(i2c0)

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

static const struct device* const i2c_dev  = DEVICE_DT_GET(RC_I2C_NODE);
static const struct device* const oled_dev = DEVICE_DT_GET(RC_OLED_NODE);
static bool initialized;
static uint8_t font_width  = 8;
static uint8_t font_height = 8;

#define DRAW_CHECK(expression)  \
    do                          \
    {                           \
        int _rc = (expression); \
        if(_rc < 0)             \
            return _rc;         \
    } while(0)


static rc_err_sts_t rc_display_from_zephyr(int result)
{
    if(result == 0)
    {
        return RC_OK;
    }

    /* Only use for Zephyr APIs documented
     * to return zero or negative errno.
     */
    if(result > 0)
    {
        return RC_ERR_DISPLAY_UNEXPECTED_RETURN;
    }

    switch(-result)
    {
        case ENODEV:
            return RC_ERR_DISPLAY_NOT_READY;

        case EINVAL:
            return RC_ERR_INVALID_ARG;

        case ENOTSUP:
        case ENOSYS:
            return RC_ERR_DISPLAY_UNSUPPORTED_OPERATION;

        case ENOMEM:
            return RC_ERR_DISPLAY_MEMORY_EXHAUSTED;

        case EAGAIN:
        case EBUSY:
            return RC_ERR_DISPLAY_BUSY;

        case ETIMEDOUT:
            return RC_ERR_DISPLAY_TIMEOUT;

        case ERANGE:
            return RC_ERR_DISPLAY_OUT_OF_BOUNDS;

        case EIO:
            return RC_ERR_IO;

        default:
            return RC_ERR_DISPLAY_RENDER_FAILED;
    }
}


static int draw_line(int x0, int y0, int x1, int y1)
{
    const struct cfb_position a = { (int16_t)x0, (int16_t)y0 };
    const struct cfb_position b = { (int16_t)x1, (int16_t)y1 };
    return cfb_draw_line(oled_dev, &a, &b);
}

/* Generic pixel-span adapter for the compact 5x7 text renderer. */
static int draw_span(void* ctx, int x0, int y, int x1)
{
    (void)ctx;
    return draw_line(x0, y, x1, y);
}

static int draw_small_text(const char* s, int left, int right, int y, rc_ui_align_t align)
{
    return rc_ui_compact_draw(s, left, right, y, RC_UI_BOTTOM, align, draw_span, NULL);
}

/* Fit each row before drawing. Full-size Zephyr CFB first; compact 5x7
 * automatically if the CFB font would overflow, then ellipsis as a last
 * resort. This prevents the driver's automatic line wrapping/overlap. */
static int draw_fit_text(const char* s, int left, int right, int y, rc_ui_align_t align, bool prefer_large)
{
    if(s == NULL || left < 0 || right > RC_UI_RIGHT || right < left || y < 0)
    {
        return -EINVAL;
    }
    int available = right - left + 1;
    size_t n      = strlen(s);
    if(prefer_large && font_width > 0U && font_height > 0U &&
       font_height <= 8U && /* never overlap the next independent row */
       y + font_height - 1U <= RC_UI_BOTTOM && n <= (size_t)(available / font_width))
    {
        int width = (int)(n * font_width);
        int x     = rc_ui_text_origin(left, right, width, align);
        return cfb_print(oled_dev, s, (int16_t)x, (int16_t)y);
    }
    return draw_small_text(s, left, right, y, align);
}

/* UI bands are bounded and never overlap:
 *   header RC + broadcast indicator : y=2..12
 *   connection title                 : y=14..21
 *   connection primary message       : y=26..33
 *   connection instruction           : y=39..46
 *   connection radio/status footer   : y=54..61
 * Navigation uses its own centered arrow/distance row.
 */

/* 3x5 micro-font used ONLY for the tiny top-left/right status labels.
 * Other labels automatically fit using CFB or the 5x7 compact font.
 * Each row is a 3-bit mask (bit 2 is leftmost pixel).
 */
static const uint8_t* micro_glyph(char ch)
{
    static const uint8_t glyph_r[5]     = { 6, 5, 6, 5, 5 };
    static const uint8_t glyph_c[5]     = { 3, 4, 4, 4, 3 };
    static const uint8_t glyph_s[5]     = { 3, 4, 2, 1, 6 };
    static const uint8_t glyph_i[5]     = { 7, 2, 2, 2, 7 };
    static const uint8_t glyph_m[5]     = { 5, 7, 7, 5, 5 };
    static const uint8_t glyph_o[5]     = { 2, 5, 5, 5, 2 };
    static const uint8_t glyph_f[5]     = { 7, 4, 6, 4, 4 };
    static const uint8_t glyph_w[5]     = { 5, 5, 7, 7, 5 };
    static const uint8_t glyph_n[5]     = { 5, 7, 7, 7, 5 };
    static const uint8_t glyph_l[5]     = { 4, 4, 4, 4, 7 };
    static const uint8_t glyph_e[5]     = { 7, 4, 6, 4, 7 };
    static const uint8_t glyph_g[5]     = { 3, 4, 5, 5, 3 };
    static const uint8_t glyph_p[5]     = { 6, 5, 6, 4, 4 };
    static const uint8_t glyph_colon[5] = { 0, 2, 0, 2, 0 };
    switch(ch)
    {
        case 'R':
            return glyph_r;
        case 'C':
            return glyph_c;
        case 'S':
            return glyph_s;
        case 'I':
            return glyph_i;
        case 'M':
            return glyph_m;
        case 'O':
            return glyph_o;
        case 'F':
            return glyph_f;
        case 'W':
            return glyph_w;
        case 'N':
            return glyph_n;
        case 'L':
            return glyph_l;
        case 'E':
            return glyph_e;
        case 'G':
            return glyph_g;
        case 'P':
            return glyph_p;
        case ':':
            return glyph_colon;
        default:
            return NULL;
    }
}

static int draw_micro_text(const char* text, int x, int y)
{
    if(text == NULL || x < 0 || y < 0)
    {
        return -EINVAL;
    }
    size_t count = strlen(text);
    /* each glyph is 3 wide; inter-character gap is 1 pixel */
    if(count == 0U || count > 16U ||
       x + (int)(count * 4U - 1U) - 1 > RC_UI_RIGHT || y + 4 > RC_UI_BOTTOM)
    {
        return -EINVAL;
    }
    for(size_t i = 0U; i < count; ++i)
    {
        const uint8_t* g = micro_glyph(text[i]);
        if(g == NULL)
        {
            return -EINVAL;
        }
        const int gx = x + (int)(i * 4U);
        for(int row = 0; row < 5; ++row)
        {
            for(int col = 0; col < 3; ++col)
            {
                if((g[row] & (uint8_t)(4U >> col)) != 0U)
                {
                    DRAW_CHECK(draw_line(gx + col, y + row, gx + col, y + row));
                }
            }
        }
    }
    return 0;
}

static int segment(int x0, int y0, uint8_t which)
{
    switch(which)
    {
        case 0:
            return draw_line(x0 + 2, y0, x0 + 9, y0);           /* top */
        case 1:
            return draw_line(x0 + 10, y0 + 1, x0 + 10, y0 + 8); /* top right */
        case 2:
            return draw_line(x0 + 10, y0 + 10, x0 + 10, y0 + 17); /* bottom right */
        case 3:
            return draw_line(x0 + 2, y0 + 18, x0 + 9, y0 + 18); /* bottom */
        case 4:
            return draw_line(x0 + 1, y0 + 10, x0 + 1, y0 + 17); /* bottom left */
        case 5:
            return draw_line(x0 + 1, y0 + 1, x0 + 1, y0 + 8);   /* top left */
        case 6:
            return draw_line(x0 + 2, y0 + 9, x0 + 9, y0 + 9);   /* middle */
        default:
            return -EINVAL;
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
    if(digit > 9U)
    {
        return -EINVAL;
    }
    for(uint8_t seg = 0; seg < 7U; ++seg)
    {
        if((bitmap[digit] & (1U << seg)) != 0U)
        {
            DRAW_CHECK(segment(x, y, seg));
        }
    }
    return 0;
}

static int draw_distance(uint16_t meters)
{
    if(meters > 999U)
    {
        meters = 999U; /* display limit; underlying model is unchanged */
    }

    /* Center number AND unit within x=50..123, regardless of digit count. */
    const int digits      = (meters >= 100U) ? 3 : (meters >= 10U ? 2 : 1);
    const int unit_width  = 5; /* compact lower-case m */
    const int total_width = (digits - 1) * 14 + 11 + 3 + unit_width;
    int x = rc_ui_text_origin(50, RC_UI_RIGHT, total_width, RC_UI_ALIGN_CENTER);
    if(meters >= 100U)
    {
        DRAW_CHECK(big_digit((uint8_t)(meters / 100U), x, 16));
        x += 14;
    }
    if(meters >= 10U)
    {
        DRAW_CHECK(big_digit((uint8_t)((meters / 10U) % 10U), x, 16));
        x += 14;
    }
    DRAW_CHECK(big_digit((uint8_t)(meters % 10U), x, 16));
    x += 14;
    return draw_small_text("m", x, RC_UI_RIGHT, 22, RC_UI_ALIGN_LEFT);
}

static int draw_turn_icon(heltec_turn_t turn)
{
    switch(turn)
    {
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
    int best       = -1;
    uint8_t best_w = 255U;
    uint8_t best_h = 255U;
    int font_count = cfb_get_numof_fonts(oled_dev);
    for(int i = 0; i < font_count; ++i)
    {
        uint8_t w = 0U;
        uint8_t h = 0U;
        if(cfb_get_font_size(oled_dev, (uint8_t)i, &w, &h) == 0 && w > 0U &&
           h > 0U && (best < 0 || w < best_w || (w == best_w && h < best_h)))
        {
            best   = i;
            best_w = w;
            best_h = h;
        }
    }
    if(best >= 0 && cfb_framebuffer_set_font(oled_dev, (uint8_t)best) == 0)
    {
        font_width  = best_w;
        font_height = best_h;
    }
    printk("RideCast OLED CFB font %ux%u (5x7 auto-fit fallback)\n", font_width, font_height);
}

rc_err_sts_t heltec_graphic_init(void)
{
    if(initialized)
    {
        return RC_ERR_ALREADY_INITIALIZED;
    }
    if(!device_is_ready(i2c_dev))
    {
        return RC_ERR_NOT_READY;
    }
    int status = i2c_recover_bus(i2c_dev);
    if(status < 0)
    {
        printk("OLED I2C recovery failed: %d\n", status);
        return RC_ERR_IO;
    }
    status = device_init(oled_dev);
    if(status < 0 || !device_is_ready(oled_dev))
    {
        printk("OLED device initialization failed: %d\n", status);
        return RC_ERR_INIT_FAILED;
    }
    status = display_set_pixel_format(oled_dev, PIXEL_FORMAT_MONO01);
    if(status < 0)
    {
        return RC_ERR_NOT_SUPPORTED;
    }
    status = cfb_framebuffer_init(oled_dev);
    if(status < 0)
    {
        return RC_ERR_INIT_FAILED;
    }
    select_compact_font();
    status = cfb_framebuffer_clear(oled_dev, false);
    if(status < 0 || display_blanking_off(oled_dev) < 0)
    {
        return RC_ERR_IO;
    }
    initialized = true;
    return RC_OK;
}

rc_err_sts_t heltec_graphic_render(const heltec_nav_screen_t* screen)
{
    if(!initialized)
    {
        return RC_ERR_NOT_INITIALIZED;
    }
    if(screen == NULL || screen->turn > HELTEC_TURN_ARRIVED ||
       screen->link > HELTEC_LINK_ONLINE || screen->speed_kmh > 240U)
    {
        return RC_ERR_INVALID_ARG;
    }
    const char* link_label;
    switch(screen->link)
    {
        case HELTEC_LINK_DEMO:
            link_label = "SIM";
            break;
        case HELTEC_LINK_OFFLINE:
            link_label = "OFF";
            break;
        case HELTEC_LINK_WIFI:
            link_label = "WIFI";
            break;
        case HELTEC_LINK_ONLINE:
            link_label = "ONLINE";
            break;
        default:
            return RC_ERR_INVALID_ARG;
    }
    const char* turn_label;
    switch(screen->turn)
    {
        case HELTEC_TURN_RIGHT:
            turn_label = "TURN RIGHT";
            break;
        case HELTEC_TURN_LEFT:
            turn_label = "TURN LEFT";
            break;
        case HELTEC_TURN_STRAIGHT:
            turn_label = "STRAIGHT";
            break;
        case HELTEC_TURN_ARRIVED:
            turn_label = "ARRIVED";
            break;
        case HELTEC_TURN_NONE:
            turn_label = "WAIT NAV";
            break;
        default:
            return RC_ERR_INVALID_ARG;
    }
    char speed[20];
    char gps[12];
    (void)snprintf(speed, sizeof(speed), "%ukm/h", (unsigned int)screen->speed_kmh);
    (void)snprintf(gps, sizeof(gps), "GPS: %s", screen->gps_valid ? "OK" : "NO");

    const int link_x = RC_UI_RIGHT - (int)(4U * strlen(link_label) - 1U) + 1;
    int status       = cfb_framebuffer_clear(oled_dev, false);
    if(status >= 0)
        status = draw_micro_text("RC", RC_UI_LEFT, 3);
    if(status >= 0)
        status = draw_micro_text(link_label, link_x, 3);
    if(status >= 0)
        status = draw_turn_icon(screen->turn);
    if(status >= 0)
        status = draw_distance(screen->distance_m);
    if(status >= 0)
        status = draw_fit_text(turn_label, RC_UI_LEFT, RC_UI_RIGHT, 40,
                               RC_UI_ALIGN_CENTER, true);
    /* Two fixed lanes guarantee max speed and GPS labels never collide. */
    if(status >= 0)
        status = draw_fit_text(speed, 4, 64, 53, RC_UI_ALIGN_LEFT, true);
    if(status >= 0)
        status = draw_small_text(gps, 78, RC_UI_RIGHT, 54, RC_UI_ALIGN_RIGHT);
    if(status >= 0)
        status = cfb_framebuffer_finalize(oled_dev);
    return status < 0 ? RC_ERR_IO : RC_OK;
}

/* Show short BLE notification/music metadata using measured, clipped rows.
 * Full text belongs on the future STM32H7 color display. */
rc_err_sts_t heltec_graphic_render_content(const rc_demo_content_t* item)
{
    if(!initialized)
        return RC_ERR_NOT_INITIALIZED;
    if(!item || (item->kind != RC_CONTENT_NOTIFICATION && item->kind != RC_CONTENT_MUSIC))
        return RC_ERR_INVALID_ARG;
    const char* title = item->kind == RC_CONTENT_MUSIC ? "MUSIC" : "NOTIFICATION";
    int r = cfb_framebuffer_clear(oled_dev, false);
    if(r >= 0)
        r = draw_micro_text("RC", RC_UI_LEFT, 3);
    if(r >= 0)
        r = draw_small_text(title, RC_UI_LEFT, RC_UI_RIGHT, 13, RC_UI_ALIGN_CENTER);
    if(r >= 0)
        r = draw_fit_text(item->source, RC_UI_LEFT, RC_UI_RIGHT, 25,
                          RC_UI_ALIGN_CENTER, true);
    if(r >= 0)
        r = draw_small_text(item->sender, RC_UI_LEFT, RC_UI_RIGHT, 39, RC_UI_ALIGN_CENTER);
    if(r >= 0)
        r = draw_small_text(item->text, RC_UI_LEFT, RC_UI_RIGHT, 53, RC_UI_ALIGN_CENTER);
    if(r >= 0)
        r = cfb_framebuffer_finalize(oled_dev);
    return r < 0 ? RC_ERR_IO : RC_OK;
}

/* A tiny 12x12 animated broadcast badge in the top-right HEADER.
 * Unlike the old graphic, it never shares a row with the AP address.
 */
static int draw_wifi_broadcast(uint8_t frame)
{
    /* Pixel-dot and expanding strokes, all inside x=113..123, y=2..11. */
    DRAW_CHECK(draw_line(118, 10, 119, 10));
    if(frame == 1U || frame == 2U || frame == 3U)
    {
        DRAW_CHECK(draw_line(116, 7, 118, 5));
        DRAW_CHECK(draw_line(119, 5, 121, 7));
    }
    if(frame == 2U || frame == 3U)
    {
        DRAW_CHECK(draw_line(113, 5, 116, 2));
        DRAW_CHECK(draw_line(120, 2, 123, 5));
    }
    return 0;
}

rc_err_sts_t heltec_graphic_render_connection(const rc_demo_snapshot_t* s)
{
    rc_err_sts_t ret = RC_OK;
    if(!initialized)
        return RC_ERR_NOT_INITIALIZED;
    if(s == NULL)
        return RC_ERR_INVALID_ARG;
    const bool broadcast      = s->stage == RC_DEMO_WAIT && s->wifi_ready;
    const uint8_t pulse_frame = (uint8_t)((k_uptime_get_32() / 250U) % 4U);
    const char* title         = "WAIT APP";
    const char* main_line     = "WIFI + BLE";
    const char* detail        = "READY";
    char pin_line[24]         = "";
    char footer[20];
    const char* radio = "NONE";
    if(s->wifi_authenticated && s->ble_authenticated)
        radio = "WIFI+BLE";
    else if(s->wifi_authenticated)
        radio = "WIFI";
    else if(s->ble_authenticated)
        radio = "BLE";
    switch(s->stage)
    {
        case RC_DEMO_WIFI_ENROLL:
            title     = "WIFI PAIR";
            detail    = "ENTER IN APP";
            main_line = "6 DIGIT CODE";
            break;
        case RC_DEMO_WIFI_TOKEN:
            title     = "WIFI AUTH";
            main_line = "SAVED TOKEN";
            detail    = "WAIT APP";
            break;
        case RC_DEMO_BLE_PAIR:
            title     = "BLE PAIR";
            main_line = "PHONE: PAIR";
            detail    = "ENTER PIN";
            break;
        case RC_DEMO_ACTIVE:
            title     = "CONN OK";
            main_line = radio;
            detail    = "AUTH SAVED";
            break;
        case RC_DEMO_FAILED:
            title     = "CONN FAIL";
            main_line = "CHECK CODE";
            detail    = "TRY RECONNECT";
            break;
        case RC_DEMO_WAIT:
        default:
            if(s->wifi_ready)
            {
                title     = "WIFI READY";
                main_line = "192.168.4.1";
                detail    = s->wifi_stations > 0 ? "OPEN APP" : "JOIN RIDECAST";
            }
            break;
    }
    /* Surface radio init/AP failures even if the other radio still works. */
    if(s->stage == RC_DEMO_WAIT && !s->wifi_ready && !s->ble_ready &&
       (s->wifi_radio_error != RC_OK || s->ble_radio_error != RC_OK))
    {
        title     = "RADIO FAIL";
        main_line = s->wifi_radio_error != RC_OK ? "WIFI ERROR" : "BLE ERROR";
        detail    = "CHECK LOGS";
    }
    if(s->pin_visible)
    {
        (void)snprintf(pin_line, sizeof(pin_line), "PIN: %06u", (unsigned)s->pin);
        main_line = pin_line;
    }
    /* A station can be associated without a TCP/application connection.
     * The WAIT screen still shows the AP broadcast animation after TCP close. */
    (void)snprintf(footer, sizeof(footer), "W:%s B:%s S:%u",
                   s->wifi_radio_error != RC_OK ? "ER" : (s->wifi_ready ? "ON" : "--"),
                   s->ble_radio_error != RC_OK ? "ER" : (s->ble_ready ? "ON" : "--"),
                   (unsigned)s->wifi_stations);
    int r = cfb_framebuffer_clear(oled_dev, false);
    /* Strict row bands and centered text remove clipping on actual SSD1306. */
    if(r >= 0)
        r = draw_micro_text("RC", RC_UI_LEFT, 3);
    if(r >= 0 && broadcast)
        r = draw_wifi_broadcast(pulse_frame);
    if(r >= 0)
        r = draw_small_text(title, RC_UI_LEFT, RC_UI_RIGHT, 14, RC_UI_ALIGN_CENTER);
    if(r >= 0)
        r = draw_fit_text(main_line, RC_UI_LEFT, RC_UI_RIGHT, 26, RC_UI_ALIGN_CENTER, true);
    if(r >= 0)
        r = draw_small_text(detail, RC_UI_LEFT, RC_UI_RIGHT, 39, RC_UI_ALIGN_CENTER);
    if(r >= 0)
        r = draw_small_text(footer, RC_UI_LEFT, RC_UI_RIGHT, 54, RC_UI_ALIGN_CENTER);
    if(r >= 0)
        r = cfb_framebuffer_finalize(oled_dev);

    return rc_display_from_zephyr(r);
}
