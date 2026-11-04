/* ============================================================================
 * File        : emoji_draw.h
 * Description : Emoji gallery drawing and category navigation API
 * Target      : Zephyr RTOS + 128x64 SSD1306 character framebuffer
 * ============================================================================
 * Drawing functions do not clear or flush the framebuffer.
 * Only the OLED/UI thread should access these routines.
 * ========================================================================== */

#ifndef APP_EMOJI_DRAW_H_
#define APP_EMOJI_DRAW_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    enum emoji_type
    {
        /* Facial expressions (18) */
        EMOJI_SMILE,
        EMOJI_SAD,
        EMOJI_NEUTRAL,
        EMOJI_WINK,
        EMOJI_SURPRISED,
        EMOJI_ANGRY,
        EMOJI_GRIN,
        EMOJI_LAUGH,
        EMOJI_CRY,
        EMOJI_HAPPY_TEARS,
        EMOJI_SLEEPY,
        EMOJI_COOL,
        EMOJI_LOVE_EYES,
        EMOJI_TONGUE,
        EMOJI_CONFUSED,
        EMOJI_KISS,
        EMOJI_SCARED,
        EMOJI_BLUSH,

        /* Love and feelings (2) */
        EMOJI_HEART,
        EMOJI_BROKEN_HEART,

        /* Connectivity symbols (2) */
        EMOJI_WIFI,
        EMOJI_BLUETOOTH,

        /* Nature and weather (13) */
        EMOJI_STAR,
        EMOJI_SUN,
        EMOJI_MOON,
        EMOJI_CLOUD,
        EMOJI_RAIN,
        EMOJI_LIGHTNING,
        EMOJI_SNOWFLAKE,
        EMOJI_FIRE,
        EMOJI_WATER_DROP,
        EMOJI_TREE,
        EMOJI_FLOWER,
        EMOJI_LEAF,
        EMOJI_MOUNTAIN,

        /* Transport (4) */
        EMOJI_ROCKET,
        EMOJI_AIRPLANE,
        EMOJI_CAR,
        EMOJI_BICYCLE,

        /* Electronics and status (7) */
        EMOJI_BATTERY,
        EMOJI_BATTERY_CHARGING,
        EMOJI_LOCK,
        EMOJI_UNLOCK,
        EMOJI_BELL,
        EMOJI_CLOCK,
        EMOJI_GEAR,

        /* Objects and symbols (8) */
        EMOJI_HOME,
        EMOJI_MUSIC,
        EMOJI_GIFT,
        EMOJI_UMBRELLA,
        EMOJI_CHECK,
        EMOJI_CROSS,
        EMOJI_ARROW_UP,
        EMOJI_ARROW_DOWN,

        EMOJI_COUNT /* Sentinel, not an icon; always keep last. */
    };

    enum emoji_category
    {
        EMOJI_CATEGORY_FACES,
        EMOJI_CATEGORY_LOVE,
        EMOJI_CATEGORY_CONNECTIVITY,
        EMOJI_CATEGORY_NATURE,
        EMOJI_CATEGORY_TRANSPORT,
        EMOJI_CATEGORY_ELECTRONICS,
        EMOJI_CATEGORY_OBJECTS,
        EMOJI_CATEGORY_COUNT /* Sentinel; always keep last. */
    };

    /**
     * @brief Draw the selected icon into the existing CFB RAM framebuffer.
     *
     * Prerequisites: OLED driver and CFB have been initialized in main().
     * This function does NOT clear or transfer the framebuffer.
     *
     * @param icon Icon ID from enum emoji_type.
     * @return 0 on success, or a negative errno-style value on failure.
     */
    int draw_icon(enum emoji_type icon);

    /** @return Icon label, or "INVALID" for an invalid ID. */
    const char* emoji_name(enum emoji_type icon);

    /** @return Category label, or "INVALID" for an invalid category. */
    const char* emoji_category_name(enum emoji_category category);

    /** @return Number of icons in the category (0 for invalid category). */
    size_t emoji_category_icon_count(enum emoji_category category);

    /**
     * @brief Map a category-relative index to an enum emoji_type.
     *
     * @return EMOJI_COUNT if the category or index is invalid.
     */
    enum emoji_type emoji_category_icon(enum emoji_category category, size_t index);

#ifdef __cplusplus
}
#endif

#endif /* APP_EMOJI_DRAW_H_ */
