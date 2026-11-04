/* ============================================================================
 * File        : display_thread.c
 * Description : Event-driven OLED UI for the emoji notification center
 * Board       : Heltec WiFi LoRa 32 V2 | Zephyr 4.4.0
 * ============================================================================
 *
 * Screen flow:
 *   Power on     -> HELP screen (joystick control diagram)
 *   PRG press    -> EMOJI BROWSER
 *   PRG again    -> HELP screen
 *   Joystick X   -> Previous / next emoji
 *   Joystick Y   -> Previous / next category
 *   Joystick SW  -> Select icon (short blinking animation)
 *   System event -> Temporary notification (browser only)
 *
 * This is the ONLY thread that calls CFB or draws on the OLED.
 * main() must initialize I2C, the deferred SSD1306 device and CFB first.
 * ========================================================================== */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/display/cfb.h>
#include <zephyr/sys/printk.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "app_events.h"
#include "emoji_draw.h"

/* -------------------------------------------------------------------------- */
/* Configuration                                                              */
/* -------------------------------------------------------------------------- */

#define OLED_NODE               DT_CHOSEN(zephyr_display)
#define OLED_WIDTH_PIXELS       128
#define UI_EVENT_POLL_MS        50
#define UI_ANIMATION_FRAME_MS   250
#define UI_SELECTION_TIME_MS    1500
#define UI_NOTIFICATION_TIME_MS 2000

#if !DT_HAS_CHOSEN(zephyr_display)
    #error "Select the SSD1306 display in Devicetree"
#endif

static const struct device* const oled_dev = DEVICE_DT_GET(OLED_NODE);

/* -------------------------------------------------------------------------- */
/* UI State                                                                   */
/* -------------------------------------------------------------------------- */

enum ui_page
{
    UI_PAGE_HELP,
    UI_PAGE_BROWSER
};

enum ui_overlay
{
    UI_OVERLAY_NONE,
    UI_OVERLAY_SELECTION,
    UI_OVERLAY_NOTIFICATION
};

struct ui_state
{
    enum ui_page page;
    enum emoji_category category;
    size_t icon_index;

    enum ui_overlay overlay;
    enum emoji_type overlay_icon;
    bool icon_visible;
    int64_t overlay_deadline_ms;
    int64_t next_frame_ms;

    bool redraw;
};

/* Width of the selected monospace bitmap font; used for text clipping. */
static uint8_t font_width = 8U;

/* Return an error as soon as a framebuffer operation fails. */
#define UI_CHECK(expression)       \
    do                             \
    {                              \
        int ui_rc_ = (expression); \
        if(ui_rc_ < 0)             \
        {                          \
            return ui_rc_;         \
        }                          \
    } while(false)

/* -------------------------------------------------------------------------- */
/* Drawing Helpers                                                            */
/* -------------------------------------------------------------------------- */

static int ui_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    const struct cfb_position start = { x1, y1 };
    const struct cfb_position end   = { x2, y2 };

    return cfb_draw_line(oled_dev, &start, &end);
}

/* Print only the characters that can fit on the 128-pixel-wide display. */
static int ui_text(const char* text, int16_t x, int16_t y)
{
    char clipped[24];
    size_t max_chars;
    size_t length;

    if(x < 0 || x >= OLED_WIDTH_PIXELS)
    {
        return -EINVAL;
    }

    max_chars = (size_t)(OLED_WIDTH_PIXELS - x) / font_width;
    if(max_chars > sizeof(clipped) - 1U)
    {
        max_chars = sizeof(clipped) - 1U;
    }

    length = strlen(text);
    if(length > max_chars)
    {
        length = max_chars;
    }

    memcpy(clipped, text, length);
    clipped[length] = '\0';

    return cfb_print(oled_dev, clipped, x, y);
}

/* Prefer an 8-pixel-high or smaller font so help-screen labels fit. */
static void ui_select_compact_font(void)
{
    int count = cfb_get_numof_fonts(oled_dev);

    for(int i = 0; i < count; ++i)
    {
        uint8_t width  = 0;
        uint8_t height = 0;

        if(cfb_get_font_size(oled_dev, (uint8_t)i, &width, &height) == 0 &&
           width > 0U && width <= 8U && height <= 8U &&
           cfb_framebuffer_set_font(oled_dev, (uint8_t)i) == 0)
        {
            font_width = width;
            printk("UI: selected CFB font %d (%ux%u)\n", i, width, height);
            return;
        }
    }

    printk("UI: no 8-pixel font found; using default font\n");
}

/* -------------------------------------------------------------------------- */
/* Screen 1: Help / Joystick Controls                                         */
/* -------------------------------------------------------------------------- */

static int ui_draw_help(void)
{
    const struct cfb_position select_center = { 64, 35 };

    UI_CHECK(ui_text("EMOJI CONTROLS", 0, 0));

    /* Up arrow = previous category. */
    UI_CHECK(ui_line(64, 14, 64, 25));
    UI_CHECK(ui_line(64, 14, 58, 20));
    UI_CHECK(ui_line(64, 14, 70, 20));
    UI_CHECK(ui_text("CAT", 77, 15));

    /* Left arrow = previous emoji. */
    UI_CHECK(ui_line(42, 35, 31, 35));
    UI_CHECK(ui_line(31, 35, 37, 29));
    UI_CHECK(ui_line(31, 35, 37, 41));
    UI_CHECK(ui_text("PREV", 0, 31));

    /* Middle circle = joystick SW button. */
    UI_CHECK(cfb_draw_circle(oled_dev, &select_center, 10));
    UI_CHECK(ui_text("SW", 57, 31));

    /* Right arrow = next emoji. */
    UI_CHECK(ui_line(86, 35, 97, 35));
    UI_CHECK(ui_line(97, 35, 91, 29));
    UI_CHECK(ui_line(97, 35, 91, 41));
    UI_CHECK(ui_text("NXT", 103, 31));

    /* Down arrow = next category. */
    UI_CHECK(ui_line(64, 45, 64, 54));
    UI_CHECK(ui_line(64, 54, 58, 48));
    UI_CHECK(ui_line(64, 54, 70, 48));
    UI_CHECK(ui_text("CAT", 77, 45));

    UI_CHECK(ui_text("PRG: OPEN/BACK", 0, 56));
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Screen 2: Emoji Browser                                                    */
/* -------------------------------------------------------------------------- */

static int ui_draw_browser(const struct ui_state* ui)
{
    enum emoji_type icon = emoji_category_icon(ui->category, ui->icon_index);

    if(icon == EMOJI_COUNT)
    {
        return -EINVAL;
    }

    UI_CHECK(ui_text(emoji_category_name(ui->category), 0, 0));
    UI_CHECK(ui_text(emoji_name(icon), 0, 10));
    UI_CHECK(ui_line(0, 19, 127, 19));

    /* Existing 54-icon module draws only; it does not clear or finalize. */
    return draw_icon(icon);
}

/* -------------------------------------------------------------------------- */
/* Temporary Selection / Notification Screens                                 */
/* -------------------------------------------------------------------------- */

static int ui_draw_overlay(const struct ui_state* ui)
{
    if(ui->overlay == UI_OVERLAY_SELECTION)
    {
        UI_CHECK(ui_text("SELECTED", 0, 0));
    }
    else
    {
        UI_CHECK(ui_text("NOTIFICATION", 0, 0));
    }

    UI_CHECK(ui_text(emoji_name(ui->overlay_icon), 0, 10));
    UI_CHECK(ui_line(0, 19, 127, 19));

    /* Blink the icon to make the selection/alert visibly animated. */
    if(ui->icon_visible)
    {
        UI_CHECK(draw_icon(ui->overlay_icon));
    }

    return 0;
}

static int ui_render(const struct ui_state* ui)
{
    int rc;

    UI_CHECK(cfb_framebuffer_clear(oled_dev, false));

    if(ui->page == UI_PAGE_HELP)
    {
        rc = ui_draw_help();
    }
    else if(ui->overlay != UI_OVERLAY_NONE)
    {
        rc = ui_draw_overlay(ui);
    }
    else
    {
        rc = ui_draw_browser(ui);
    }

    if(rc < 0)
    {
        return rc;
    }

    return cfb_framebuffer_finalize(oled_dev);
}

/* -------------------------------------------------------------------------- */
/* UI State Machine                                                           */
/* -------------------------------------------------------------------------- */

static void
ui_start_overlay(struct ui_state* ui, enum ui_overlay overlay, enum emoji_type icon, int64_t now_ms)
{
    ui->overlay      = overlay;
    ui->overlay_icon = icon;
    ui->icon_visible = true;
    ui->overlay_deadline_ms =
    now_ms + ((overlay == UI_OVERLAY_SELECTION) ? UI_SELECTION_TIME_MS : UI_NOTIFICATION_TIME_MS);
    ui->next_frame_ms = now_ms + UI_ANIMATION_FRAME_MS;
    ui->redraw        = true;
}

static void ui_change_category(struct ui_state* ui, bool next)
{
    const int count = EMOJI_CATEGORY_COUNT;
    int current     = (int)ui->category;

    current        = (current + (next ? 1 : count - 1)) % count;
    ui->category   = (enum emoji_category)current;
    ui->icon_index = 0U;
    ui->overlay    = UI_OVERLAY_NONE;
    ui->redraw     = true;
}

static void ui_change_icon(struct ui_state* ui, bool next)
{
    size_t count = emoji_category_icon_count(ui->category);

    if(count == 0U)
    {
        return;
    }

    ui->icon_index =
    next ? (ui->icon_index + 1U) % count : (ui->icon_index + count - 1U) % count;
    ui->overlay = UI_OVERLAY_NONE;
    ui->redraw  = true;
}

static void ui_handle_event(struct ui_state* ui, const struct app_event* event)
{
    int64_t now_ms = k_uptime_get();

    /* PRG toggles the initial help screen and the emoji browser. */
    if(event->type == APP_EVENT_TOGGLE_SCREEN)
    {
        ui->page = (ui->page == UI_PAGE_HELP) ? UI_PAGE_BROWSER : UI_PAGE_HELP;
        ui->overlay = UI_OVERLAY_NONE;
        ui->redraw  = true;
        return;
    }

    /* Ignore navigation and simulated notifications while on the help screen. */
    if(ui->page == UI_PAGE_HELP)
    {
        return;
    }

    switch(event->type)
    {
        case APP_EVENT_CATEGORY_PREVIOUS:
            ui_change_category(ui, false);
            break;
        case APP_EVENT_CATEGORY_NEXT:
            ui_change_category(ui, true);
            break;
        case APP_EVENT_EMOJI_PREVIOUS:
            ui_change_icon(ui, false);
            break;
        case APP_EVENT_EMOJI_NEXT:
            ui_change_icon(ui, true);
            break;
        case APP_EVENT_EMOJI_SELECT:
            ui_start_overlay(ui, UI_OVERLAY_SELECTION,
                             emoji_category_icon(ui->category, ui->icon_index), now_ms);
            break;
        case APP_EVENT_NOTIFICATION:
            if(ui->overlay == UI_OVERLAY_NONE && event->value >= 0 && event->value < EMOJI_COUNT)
            {
                ui_start_overlay(ui, UI_OVERLAY_NOTIFICATION,
                                 (enum emoji_type)event->value, now_ms);
            }
            break;
        default:
            break;
    }
}

static void ui_update_animation(struct ui_state* ui)
{
    int64_t now_ms;

    if(ui->page != UI_PAGE_BROWSER || ui->overlay == UI_OVERLAY_NONE)
    {
        return;
    }

    now_ms = k_uptime_get();

    if(now_ms >= ui->overlay_deadline_ms)
    {
        /* Return automatically to the emoji browser. */
        ui->overlay = UI_OVERLAY_NONE;
        ui->redraw  = true;
    }
    else if(now_ms >= ui->next_frame_ms)
    {
        ui->icon_visible  = !ui->icon_visible;
        ui->next_frame_ms = now_ms + UI_ANIMATION_FRAME_MS;
        ui->redraw        = true;
    }
}

/* -------------------------------------------------------------------------- */
/* Display Thread Entry                                                       */
/* -------------------------------------------------------------------------- */

void display_thread_entry(void* arg1, void* arg2, void* arg3)
{
    struct ui_state ui = {
        .page         = UI_PAGE_HELP,
        .category     = EMOJI_CATEGORY_FACES,
        .icon_index   = 0U,
        .overlay      = UI_OVERLAY_NONE,
        .overlay_icon = EMOJI_SMILE,
        .icon_visible = true,
        .redraw       = true,
    };

    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    if(!device_is_ready(oled_dev))
    {
        printk("UI: OLED not ready; initialize it in main() first\n");
        return;
    }

    ui_select_compact_font();
    printk("UI: display thread started (help screen)\n");

    while(true)
    {
        struct app_event event;
        int rc;

        ui_update_animation(&ui);

        if(ui.redraw)
        {
            rc = ui_render(&ui);
            if(rc < 0)
            {
                printk("UI: display draw failed: %d\n", rc);
            }
            ui.redraw = false;
        }

        /* Poll the queue with a short timeout so animations remain responsive. */
        rc = k_msgq_get(&app_event_queue, &event, K_MSEC(UI_EVENT_POLL_MS));

        if(rc == 0)
        {
            ui_handle_event(&ui, &event);
        }
        else if(rc != -EAGAIN)
        {
            printk("UI: queue receive failed: %d\n", rc);
        }
    }
}
