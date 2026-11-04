/* SPDX-License-Identifier: Apache-2.0 */
/* One OLED-owner UI thread. Never blocks for an entire animation. */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/display/cfb.h>
#include <zephyr/sys/printk.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "app_events.h"
#include "scene_draw.h"

#define OLED_NODE         DT_CHOSEN(zephyr_display)
#define FRAME_INTERVAL_MS 200 /* ~5 fps; conservative for 100-kHz I2C */
#define MAX_EVENT_WAIT_MS 50
#define SCREEN_W          128

static const struct device* const display = DEVICE_DT_GET(OLED_NODE);

enum page
{
    PAGE_HELP,
    PAGE_BROWSER,
    PAGE_PLAYING,
    PAGE_PAUSED
};

struct ui_state
{
    enum page page;
    enum scene_category category;
    size_t scene_index;
    uint32_t frame;
    int64_t next_frame_ms;
    bool dirty;
};

static uint8_t font_width  = 8U;
static uint8_t font_height = 8U;

#define UI_CHECK(expr)    \
    do                    \
    {                     \
        int rc_ = (expr); \
        if(rc_ < 0)       \
        {                 \
            return rc_;   \
        }                 \
    } while(0)

static int ui_line(int x0, int y0, int x1, int y1)
{
    const struct cfb_position a = { x0, y0 };
    const struct cfb_position b = { x1, y1 };
    return cfb_draw_line(display, &a, &b);
}

/* Width clipping prevents long scene labels from reaching outside the display. */
static int ui_text(const char* str, int x, int y)
{
    char clipped[25];
    size_t max_chars;
    if(x < 0 || x >= SCREEN_W || y < 0 || y + font_height > 64)
    {
        return -EINVAL;
    }
    max_chars = (SCREEN_W - (size_t)x) / font_width;
    if(max_chars >= sizeof(clipped))
    {
        max_chars = sizeof(clipped) - 1U;
    }
    size_t n = strlen(str);
    if(n > max_chars)
    {
        n = max_chars;
    }
    memcpy(clipped, str, n);
    clipped[n] = '\0';
    return cfb_print(display, clipped, (int16_t)x, (int16_t)y);
}

static void select_compact_font(void)
{
    int count = cfb_get_numof_fonts(display);
    for(int i = 0; i < count; ++i)
    {
        uint8_t w = 0, h = 0;
        if(cfb_get_font_size(display, (uint8_t)i, &w, &h) == 0 && w > 0 && w <= 8 &&
           h > 0 && h <= 8 && cfb_framebuffer_set_font(display, (uint8_t)i) == 0)
        {
            font_width  = w;
            font_height = h;
            printk("DISPLAY: CFB font %dx%d\n", w, h);
            return;
        }
    }
    printk("DISPLAY: no compact font found; default font in use\n");
}

static int draw_help(void)
{
    const struct cfb_position center = { 64, 34 };
    UI_CHECK(ui_text("SCENE PLAYER", 0, 0));
    UI_CHECK(ui_line(0, 10, 127, 10));

    UI_CHECK(ui_line(64, 14, 64, 23));
    UI_CHECK(ui_line(64, 14, 58, 20));
    UI_CHECK(ui_line(64, 14, 70, 20));
    UI_CHECK(ui_text("CAT", 77, 15));

    UI_CHECK(ui_line(40, 35, 29, 35));
    UI_CHECK(ui_line(29, 35, 35, 29));
    UI_CHECK(ui_line(29, 35, 35, 41));
    UI_CHECK(ui_text("PREV", 0, 31));

    UI_CHECK(cfb_draw_circle(display, &center, 9));
    UI_CHECK(ui_text("SW", 58, 30));

    UI_CHECK(ui_line(87, 35, 98, 35));
    UI_CHECK(ui_line(98, 35, 92, 29));
    UI_CHECK(ui_line(98, 35, 92, 41));
    UI_CHECK(ui_text("NEXT", 102, 31));

    UI_CHECK(ui_line(64, 45, 64, 53));
    UI_CHECK(ui_line(64, 53, 58, 47));
    UI_CHECK(ui_line(64, 53, 70, 47));
    UI_CHECK(ui_text("CAT", 77, 45));
    UI_CHECK(ui_text("PRG: OPEN/BACK", 0, 56));
    return 0;
}

static enum scene_id selected_scene(const struct ui_state* ui)
{
    return scene_category_get(ui->category, ui->scene_index);
}

static int draw_browser(const struct ui_state* ui)
{
    enum scene_id id = selected_scene(ui);
    char heading[25];
    int n = snprintf(heading, sizeof(heading), "%s %u/%u",
                     scene_category_name(ui->category),
                     (unsigned int)(ui->scene_index + 1U),
                     (unsigned int)scene_category_count(ui->category));
    if(n < 0)
    {
        return -EINVAL;
    }
    UI_CHECK(scene_draw(display, id, 0)); /* Preview first frame */
    UI_CHECK(ui_text(heading, 0, 0));
    UI_CHECK(ui_text(scene_name(id), 0, 56));
    return 0;
}

static int ui_render(const struct ui_state* ui)
{
    UI_CHECK(cfb_framebuffer_clear(display, false));
    switch(ui->page)
    {
        case PAGE_HELP:
            UI_CHECK(draw_help());
            break;
        case PAGE_BROWSER:
            UI_CHECK(draw_browser(ui));
            break;
        case PAGE_PLAYING:
        case PAGE_PAUSED:
            UI_CHECK(scene_draw(display, selected_scene(ui), ui->frame));
            if(ui->page == PAGE_PAUSED)
            {
                UI_CHECK(ui_text("PAUSED", 0, 0));
                UI_CHECK(ui_text("SW: RESUME", 0, 56));
            }
            break;
        default:
            return -EINVAL;
    }
    return cfb_framebuffer_finalize(display);
}

/* A PRG press always returns to the help page from anywhere except help. */
static void handle_event(struct ui_state* ui, enum app_event_type ev)
{
    if(ev == APP_EVENT_TOGGLE_HELP)
    {
        ui->page  = (ui->page == PAGE_HELP) ? PAGE_BROWSER : PAGE_HELP;
        ui->dirty = true;
        return;
    }
    if(ui->page == PAGE_HELP)
    {
        return;
    }

    if(ev == APP_EVENT_SELECT)
    {
        switch(ui->page)
        {
            case PAGE_BROWSER:
                ui->page          = PAGE_PLAYING;
                ui->frame         = 0;
                ui->next_frame_ms = k_uptime_get() + FRAME_INTERVAL_MS;
                break;
            case PAGE_PLAYING:
                ui->page = PAGE_PAUSED;
                break;
            case PAGE_PAUSED:
                ui->page          = PAGE_PLAYING;
                ui->next_frame_ms = k_uptime_get() + FRAME_INTERVAL_MS;
                break;
            default:
                break;
        }
        ui->dirty = true;
        return;
    }
    if(ui->page != PAGE_BROWSER)
    {
        return;
    }

    switch(ev)
    {
        case APP_EVENT_CATEGORY_PREVIOUS:
            ui->category = (enum scene_category)(
            (ui->category + SCENE_CATEGORY_COUNT - 1) % SCENE_CATEGORY_COUNT);
            ui->scene_index = 0;
            ui->dirty       = true;
            break;
        case APP_EVENT_CATEGORY_NEXT:
            ui->category = (enum scene_category)((ui->category + 1) % SCENE_CATEGORY_COUNT);
            ui->scene_index = 0;
            ui->dirty       = true;
            break;
        case APP_EVENT_SCENE_PREVIOUS:
            ui->scene_index =
            (ui->scene_index + scene_category_count(ui->category) - 1U) %
            scene_category_count(ui->category);
            ui->dirty = true;
            break;
        case APP_EVENT_SCENE_NEXT:
            ui->scene_index = (ui->scene_index + 1U) % scene_category_count(ui->category);
            ui->dirty = true;
            break;
        default:
            break;
    }
}

void display_thread_entry(void* arg1, void* arg2, void* arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    struct ui_state ui = {
        .page          = PAGE_HELP,
        .category      = SCENE_CAT_SPACE,
        .scene_index   = 0,
        .frame         = 0,
        .next_frame_ms = 0,
        .dirty         = true,
    };
    select_compact_font();
    printk("DISPLAY: scene player started (help screen)\n");

    while(true)
    {
        int64_t now = k_uptime_get();
        int wait_ms = MAX_EVENT_WAIT_MS;
        if(ui.page == PAGE_PLAYING)
        {
            int64_t remaining = ui.next_frame_ms - now;
            if(remaining <= 0)
            {
                wait_ms = 0;
            }
            else if(remaining < wait_ms)
            {
                wait_ms = (int)remaining;
            }
        }

        struct app_event msg;
        if(k_msgq_get(&app_event_queue, &msg, K_MSEC(wait_ms)) == 0)
        {
            handle_event(&ui, msg.type);
        }

        now = k_uptime_get();
        if(ui.page == PAGE_PLAYING && now >= ui.next_frame_ms)
        {
            ui.frame++;
            ui.next_frame_ms = now + FRAME_INTERVAL_MS;
            ui.dirty         = true;
        }
        if(ui.dirty)
        {
            int rc = ui_render(&ui);
            if(rc != 0)
            {
                printk("DISPLAY: render error %d\n", rc);
            }
            ui.dirty = false;
        }
    }
}
