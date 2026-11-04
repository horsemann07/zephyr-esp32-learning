/* ============================================================================
 * File        : main.c
 * Application : 05_oled_emoji_gallery
 * Description : 42 monochrome icons, one icon per second, Zephyr CFB
 * Target      : Heltec WiFi LoRa 32 V2 / Zephyr 4.4.0
 * ============================================================================
 *
 * This is a graphics demonstration, not a Wi-Fi/Bluetooth driver demo.
 * Each icon is made from line segments, circles, rectangles and pixels.
 *
 * Critical Heltec initialization sequence (already verified on this board):
 *   1. Let board initialization enable OLED power and release OLED reset.
 *   2. Recover the I2C bus (was observed busy before recovery).
 *   3. Initialize the SSD1306 device deferred by app.overlay.
 *   4. Initialize CFB, then draw one icon at a time.
 *
 * Rendering sequence for each frame:
 *   Clear RAM framebuffer -> draw label and icon -> finalize to OLED
 *   -> sleep 1000 ms -> repeat.
 * ========================================================================== */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define I2C_NODE         DT_NODELABEL(i2c0)
#define OLED_NODE        DT_CHOSEN(zephyr_display)
#define ICON_DURATION_MS 1000U

#if !DT_NODE_EXISTS(I2C_NODE)
    #error "Missing i2c0 Devicetree node"
#endif
#if !DT_NODE_HAS_STATUS(I2C_NODE, okay)
    #error "i2c0 is disabled"
#endif
#if !DT_HAS_CHOSEN(zephyr_display)
    #error "Shield must define chosen zephyr,display"
#endif
#if !DT_NODE_HAS_STATUS(OLED_NODE, okay)
    #error "OLED is disabled"
#endif
#if !DT_PROP(OLED_NODE, zephyr_deferred_init)
    #error "Set zephyr,deferred-init in app.overlay"
#endif

static const struct device* const i2c_dev  = DEVICE_DT_GET(I2C_NODE);
static const struct device* const oled_dev = DEVICE_DT_GET(OLED_NODE);

enum emoji_type
{
    /* Facial expressions */
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

    /* Love / feelings */
    EMOJI_HEART,
    EMOJI_BROKEN_HEART,

    /* Connectivity (drawn symbols only) */
    EMOJI_WIFI,
    EMOJI_BLUETOOTH,

    /* Nature / weather */
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

    /* Transport */
    EMOJI_ROCKET,
    EMOJI_AIRPLANE,
    EMOJI_CAR,
    EMOJI_BICYCLE,

    /* Electronics / status */
    EMOJI_BATTERY,
    EMOJI_BATTERY_CHARGING,
    EMOJI_LOCK,
    EMOJI_UNLOCK,
    EMOJI_BELL,
    EMOJI_CLOCK,
    EMOJI_GEAR,

    /* Objects / symbols */
    EMOJI_HOME,
    EMOJI_MUSIC,
    EMOJI_GIFT,
    EMOJI_UMBRELLA,
    EMOJI_CHECK,
    EMOJI_CROSS,
    EMOJI_ARROW_UP,
    EMOJI_ARROW_DOWN,

    EMOJI_COUNT /* Keep last: not an icon! */
};

/* Position in this table MUST match enum emoji_type above. */
static const char* const icon_names[EMOJI_COUNT] = {
    "SMILE",      "SAD",       "NEUTRAL",   "WINK",      "SURPRISED",
    "ANGRY",      "GRIN",      "LAUGH",     "CRY",       "HAPPY TEARS",
    "SLEEPY",     "COOL",      "LOVE EYES", "TONGUE",    "CONFUSED",
    "KISS",       "SCARED",    "BLUSH",     "HEART",     "BROKEN HEART",
    "WIFI",       "BLUETOOTH", "STAR",      "SUN",       "MOON",
    "CLOUD",      "RAIN",      "LIGHTNING", "SNOWFLAKE", "FIRE",
    "WATER DROP", "TREE",      "FLOWER",    "LEAF",      "MOUNTAIN",
    "ROCKET",     "AIRPLANE",  "CAR",       "BICYCLE",   "BATTERY",
    "CHARGING",   "LOCK",      "UNLOCK",    "BELL",      "CLOCK",
    "GEAR",       "HOME",      "MUSIC",     "GIFT",      "UMBRELLA",
    "CHECK",      "CROSS",     "ARROW UP",  "ARROW DOWN"
};

BUILD_ASSERT(ARRAY_SIZE(icon_names) == EMOJI_COUNT, "Icon names must match enum");

/* Return from an icon immediately if a CFB operation fails. */
#define DRAW_OK(expression)     \
    do                          \
    {                           \
        int rc_ = (expression); \
        if(rc_ < 0)             \
        {                       \
            return rc_;         \
        }                       \
    } while(false)

/* ========================================================================== */
/* Basic primitives. All coordinates are pixels on a 128 x 64 OLED.           */
/* Top-left = (0,0), X grows right, Y grows down. */
/* ========================================================================== */

static int line(int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    const struct cfb_position a = { x1, y1 };
    const struct cfb_position b = { x2, y2 };
    return cfb_draw_line(oled_dev, &a, &b);
}

static int dot(int16_t x, int16_t y)
{
    const struct cfb_position p = { x, y };
    return cfb_draw_point(oled_dev, &p);
}

static int circle(int16_t x, int16_t y, uint16_t r)
{
    const struct cfb_position p = { x, y };
    return cfb_draw_circle(oled_dev, &p, r);
}

static int rect(int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
    const struct cfb_position a = { x1, y1 };
    const struct cfb_position b = { x2, y2 };
    return cfb_draw_rect(oled_dev, &a, &b);
}

/* Draw each consecutive pair, P0->P1, P1->P2, and so on. */
static int path(const struct cfb_position* points, size_t count)
{
    if(count < 2U)
    {
        return -EINVAL;
    }
    for(size_t i = 1U; i < count; ++i)
    {
        int rc = cfb_draw_line(oled_dev, &points[i - 1U], &points[i]);
        if(rc < 0)
        {
            return rc;
        }
    }
    return 0;
}

/* Reusable compact format for drawing a connected polyline. */
#define DRAW_PATH(points) path((points), ARRAY_SIZE(points))

/* ========================================================================== */
/* 1-6: Facial expressions                                                    */
/* ========================================================================== */

static int face(enum emoji_type expression)
{
    const struct cfb_position smile[] = {
        { 52, 46 }, { 58, 52 }, { 64, 54 }, { 70, 52 }, { 76, 46 }
    };
    const struct cfb_position sad[] = {
        { 52, 54 }, { 58, 48 }, { 64, 46 }, { 70, 48 }, { 76, 54 }
    };

    DRAW_OK(circle(64, 42, 20));
    if(expression == EMOJI_WINK)
    {
        DRAW_OK(line(53, 34, 61, 34));
    }
    else
    {
        DRAW_OK(circle(57, 34, 2));
    }
    DRAW_OK(circle(71, 34, 2));

    switch(expression)
    {
        case EMOJI_SMILE:
        case EMOJI_WINK:
            return DRAW_PATH(smile);
        case EMOJI_SAD:
            return DRAW_PATH(sad);
        case EMOJI_NEUTRAL:
            return line(53, 50, 75, 50);
        case EMOJI_SURPRISED:
            return circle(64, 51, 5);
        case EMOJI_ANGRY:
            DRAW_OK(line(51, 27, 62, 31));
            DRAW_OK(line(66, 31, 77, 27));
            return DRAW_PATH(sad);
        default:
            return -EINVAL;
    }
}


/* ============================================================================
 * Additional Facial Expressions
 * Description : Draw 12 new expressions using Zephyr CFB
 * ========================================================================== */

/**
 * @brief Draw an additional facial expression.
 *
 * Every face has:
 *   - A circular outline
 *   - Eyes or special eye patterns
 *   - A mouth representing the selected emotion
 *
 * @param expression Facial expression from enum emoji_type.
 *
 * @return 0 on success, negative error code on failure.
 */
static int face_extra(enum emoji_type expression)
{
    /* Reusable smiling mouth. */
    const struct cfb_position smile[] = {
        { 52, 46 }, { 58, 52 }, { 64, 54 }, { 70, 52 }, { 76, 46 }
    };

    /* Reusable sad mouth. */
    const struct cfb_position sad[] = {
        { 52, 54 }, { 58, 48 }, { 64, 46 }, { 70, 48 }, { 76, 54 }
    };

    /*
     * STEP 1: Draw the common face outline.
     *
     * Center = (64, 42)
     * Radius = 20 pixels
     */
    DRAW_OK(circle(64, 42, 20));

    /*
     * STEP 2: Select the facial expression.
     */
    switch(expression)
    {
            /* ---------------------------------------------------------- */
            /* 1. GRIN                                                     */
            /* ---------------------------------------------------------- */

        case EMOJI_GRIN:
        {
            /* Open eyes. */
            DRAW_OK(circle(57, 34, 2));
            DRAW_OK(circle(71, 34, 2));

            /* Large rectangular smiling mouth. */
            DRAW_OK(rect(53, 46, 75, 56));

            /* Teeth separation. */
            DRAW_OK(line(53, 50, 75, 50));

            return 0;
        }

            /* ---------------------------------------------------------- */
            /* 2. LAUGH                                                    */
            /* ---------------------------------------------------------- */

        case EMOJI_LAUGH:
        {
            /* Left closed laughing eye. */
            DRAW_OK(line(52, 36, 57, 32));
            DRAW_OK(line(57, 32, 62, 36));

            /* Right closed laughing eye. */
            DRAW_OK(line(66, 36, 71, 32));
            DRAW_OK(line(71, 32, 76, 36));

            /* Large open laughing mouth. */
            const struct cfb_position mouth[] = { { 52, 46 }, { 57, 55 },
                                                  { 64, 58 }, { 71, 55 },
                                                  { 76, 46 }, { 52, 46 } };

            return DRAW_PATH(mouth);
        }

            /* ---------------------------------------------------------- */
            /* 3. CRY                                                      */
            /* ---------------------------------------------------------- */

        case EMOJI_CRY:
        {
            DRAW_OK(circle(57, 34, 2));
            DRAW_OK(circle(71, 34, 2));

            /* Tear on the left cheek. */
            DRAW_OK(line(53, 38, 50, 45));
            DRAW_OK(line(50, 45, 48, 48));

            /* Sad mouth. */
            return DRAW_PATH(sad);
        }

            /* ---------------------------------------------------------- */
            /* 4. HAPPY TEARS                                              */
            /* ---------------------------------------------------------- */

        case EMOJI_HAPPY_TEARS:
        {
            /* Laughing eyes. */
            DRAW_OK(line(53, 36, 57, 32));
            DRAW_OK(line(57, 32, 61, 36));

            DRAW_OK(line(67, 36, 71, 32));
            DRAW_OK(line(71, 32, 75, 36));

            /* Tears on both cheeks. */
            DRAW_OK(line(51, 38, 47, 45));
            DRAW_OK(line(77, 38, 81, 45));

            return DRAW_PATH(smile);
        }

            /* ---------------------------------------------------------- */
            /* 5. SLEEPY                                                   */
            /* ---------------------------------------------------------- */

        case EMOJI_SLEEPY:
        {
            /* Both eyes closed. */
            DRAW_OK(line(52, 35, 62, 35));
            DRAW_OK(line(66, 35, 76, 35));

            /* Small sleepy mouth. */
            DRAW_OK(circle(64, 51, 3));

            return 0;
        }

            /* ---------------------------------------------------------- */
            /* 6. COOL                                                     */
            /* ---------------------------------------------------------- */

        case EMOJI_COOL:
        {
            /* Sunglasses: left lens. */
            DRAW_OK(rect(50, 31, 62, 39));

            /* Sunglasses: right lens. */
            DRAW_OK(rect(66, 31, 78, 39));

            /* Connecting bridge. */
            DRAW_OK(line(62, 34, 66, 34));

            /* Happy mouth. */
            return DRAW_PATH(smile);
        }

            /* ---------------------------------------------------------- */
            /* 7. LOVE EYES                                                */
            /* ---------------------------------------------------------- */

        case EMOJI_LOVE_EYES:
        {
            /* Left heart-shaped eye. */
            const struct cfb_position left_heart[] = {
                { 57, 39 }, { 52, 34 }, { 52, 31 }, { 55, 30 }, { 57, 32 },
                { 59, 30 }, { 62, 31 }, { 62, 34 }, { 57, 39 }
            };

            /* Right heart-shaped eye. */
            const struct cfb_position right_heart[] = {
                { 71, 39 }, { 66, 34 }, { 66, 31 }, { 69, 30 }, { 71, 32 },
                { 73, 30 }, { 76, 31 }, { 76, 34 }, { 71, 39 }
            };

            DRAW_OK(DRAW_PATH(left_heart));
            DRAW_OK(DRAW_PATH(right_heart));

            return DRAW_PATH(smile);
        }

            /* ---------------------------------------------------------- */
            /* 8. TONGUE                                                   */
            /* ---------------------------------------------------------- */

        case EMOJI_TONGUE:
        {
            DRAW_OK(circle(57, 34, 2));
            DRAW_OK(circle(71, 34, 2));

            /* Open mouth. */
            DRAW_OK(line(53, 47, 75, 47));
            DRAW_OK(line(53, 47, 64, 55));
            DRAW_OK(line(64, 55, 75, 47));

            /* Tongue sticking out. */
            DRAW_OK(rect(60, 51, 68, 58));
            DRAW_OK(line(64, 53, 64, 58));

            return 0;
        }

            /* ---------------------------------------------------------- */
            /* 9. CONFUSED                                                 */
            /* ---------------------------------------------------------- */

        case EMOJI_CONFUSED:
        {
            DRAW_OK(circle(57, 34, 2));
            DRAW_OK(circle(71, 34, 2));

            /* One raised eyebrow. */
            DRAW_OK(line(66, 27, 77, 30));

            /* Slanted mouth. */
            DRAW_OK(line(56, 52, 72, 47));

            return 0;
        }

            /* ---------------------------------------------------------- */
            /* 10. KISS                                                    */
            /* ---------------------------------------------------------- */

        case EMOJI_KISS:
        {
            /* Eyes closed. */
            DRAW_OK(line(53, 34, 61, 34));
            DRAW_OK(line(67, 34, 75, 34));

            /* Small puckered lips. */
            DRAW_OK(circle(64, 51, 3));

            return 0;
        }

            /* ---------------------------------------------------------- */
            /* 11. SCARED                                                  */
            /* ---------------------------------------------------------- */

        case EMOJI_SCARED:
        {
            /* Wide-open eyes. */
            DRAW_OK(circle(57, 34, 4));
            DRAW_OK(circle(71, 34, 4));

            /* Wide-open mouth. */
            DRAW_OK(circle(64, 51, 6));

            return 0;
        }

            /* ---------------------------------------------------------- */
            /* 12. BLUSH                                                   */
            /* ---------------------------------------------------------- */

        case EMOJI_BLUSH:
        {
            DRAW_OK(circle(57, 34, 2));
            DRAW_OK(circle(71, 34, 2));

            /* Cheek marks. */
            DRAW_OK(circle(50, 43, 3));
            DRAW_OK(circle(78, 43, 3));

            return DRAW_PATH(smile);
        }

        default:
        {
            return -EINVAL;
        }
    }
}

/* ========================================================================== */
/* 7-10: Heart, broken heart, Wi-Fi, Bluetooth */
/* ========================================================================== */

static int heart(void)
{
    const struct cfb_position p[] = {
        { 64, 30 }, { 59, 25 }, { 52, 23 }, { 45, 23 }, { 38, 29 }, { 36, 36 },
        { 40, 44 }, { 47, 51 }, { 64, 61 }, { 81, 51 }, { 88, 44 }, { 92, 36 },
        { 90, 29 }, { 83, 23 }, { 76, 23 }, { 69, 25 }, { 64, 30 }
    };
    return DRAW_PATH(p);
}

static int broken_heart(void)
{
    const struct cfb_position left[]  = { { 61, 30 }, { 54, 24 }, { 46, 23 },
                                          { 39, 29 }, { 36, 36 }, { 40, 44 },
                                          { 60, 61 }, { 62, 51 }, { 57, 46 },
                                          { 63, 38 }, { 58, 34 }, { 61, 30 } };
    const struct cfb_position right[] = { { 67, 30 }, { 74, 24 }, { 82, 23 },
                                          { 89, 29 }, { 92, 36 }, { 88, 44 },
                                          { 68, 61 }, { 66, 51 }, { 72, 45 },
                                          { 65, 38 }, { 70, 34 }, { 67, 30 } };
    DRAW_OK(DRAW_PATH(left));
    return DRAW_PATH(right);
}

static int wifi(void)
{
    const struct cfb_position outer[]  = { { 34, 39 }, { 41, 33 }, { 48, 28 },
                                           { 56, 25 }, { 64, 24 }, { 72, 25 },
                                           { 80, 28 }, { 87, 33 }, { 94, 39 } };
    const struct cfb_position middle[] = { { 43, 46 }, { 49, 41 }, { 56, 38 },
                                           { 64, 37 }, { 72, 38 }, { 79, 41 },
                                           { 85, 46 } };
    const struct cfb_position inner[]  = {
        { 54, 52 }, { 59, 49 }, { 64, 48 }, { 69, 49 }, { 74, 52 }
    };
    DRAW_OK(DRAW_PATH(outer));
    DRAW_OK(DRAW_PATH(middle));
    DRAW_OK(DRAW_PATH(inner));
    return circle(64, 58, 2);
}

static int bluetooth(void)
{
    DRAW_OK(line(64, 21, 64, 59));
    DRAW_OK(line(64, 21, 77, 31));
    DRAW_OK(line(77, 31, 52, 51));
    DRAW_OK(line(52, 31, 77, 49));
    return line(77, 49, 64, 59);
}

/* ========================================================================== */
/* 11-23: Nature and weather                                                  */
/* ========================================================================== */

static int star(void)
{
    const struct cfb_position p[] = { { 64, 20 }, { 70, 34 }, { 87, 34 },
                                      { 74, 44 }, { 79, 59 }, { 64, 50 },
                                      { 49, 59 }, { 54, 44 }, { 41, 34 },
                                      { 58, 34 }, { 64, 20 } };
    return DRAW_PATH(p);
}

static int sun(void)
{
    DRAW_OK(circle(64, 41, 11));
    DRAW_OK(line(64, 20, 64, 27));
    DRAW_OK(line(64, 55, 64, 62));
    DRAW_OK(line(43, 41, 50, 41));
    DRAW_OK(line(78, 41, 85, 41));
    DRAW_OK(line(49, 26, 54, 31));
    DRAW_OK(line(74, 51, 79, 56));
    DRAW_OK(line(79, 26, 74, 31));
    return line(54, 51, 49, 56);
}

static int moon(void)
{
    /* A crescent drawn from two curved polygon boundaries. */
    const struct cfb_position p[] = { { 78, 22 }, { 67, 21 }, { 58, 26 },
                                      { 52, 34 }, { 50, 44 }, { 55, 53 },
                                      { 64, 60 }, { 76, 59 }, { 82, 55 },
                                      { 73, 54 }, { 66, 48 }, { 63, 40 },
                                      { 66, 31 }, { 72, 25 }, { 78, 22 } };
    return DRAW_PATH(p);
}

static int cloud(void)
{
    const struct cfb_position p[] = {
        { 43, 53 }, { 37, 49 }, { 35, 43 }, { 37, 37 }, { 43, 34 }, { 48, 35 },
        { 51, 28 }, { 57, 24 }, { 64, 24 }, { 71, 28 }, { 74, 35 }, { 80, 33 },
        { 87, 36 }, { 91, 42 }, { 90, 48 }, { 85, 53 }, { 43, 53 }
    };
    return DRAW_PATH(p);
}

static int rain(void)
{
    DRAW_OK(cloud());
    DRAW_OK(line(48, 55, 44, 62));
    DRAW_OK(line(64, 55, 60, 62));
    return line(80, 55, 76, 62);
}

static int lightning(void)
{
    const struct cfb_position p[] = { { 69, 19 }, { 51, 43 }, { 62, 43 },
                                      { 58, 62 }, { 81, 35 }, { 69, 35 },
                                      { 69, 19 } };
    return DRAW_PATH(p);
}

static int snowflake(void)
{
    DRAW_OK(line(64, 19, 64, 61));
    DRAW_OK(line(44, 40, 84, 40));
    DRAW_OK(line(49, 25, 79, 55));
    DRAW_OK(line(49, 55, 79, 25));
    /* Branches on the vertical and horizontal arms. */
    DRAW_OK(line(58, 25, 64, 31));
    DRAW_OK(line(70, 25, 64, 31));
    DRAW_OK(line(58, 55, 64, 49));
    DRAW_OK(line(70, 55, 64, 49));
    DRAW_OK(line(49, 34, 55, 40));
    DRAW_OK(line(49, 46, 55, 40));
    DRAW_OK(line(79, 34, 73, 40));
    return line(79, 46, 73, 40);
}

static int fire(void)
{
    const struct cfb_position outer[] = { { 64, 19 }, { 71, 31 }, { 72, 23 },
                                          { 81, 36 }, { 83, 46 }, { 79, 55 },
                                          { 71, 61 }, { 58, 61 }, { 48, 56 },
                                          { 45, 46 }, { 50, 34 }, { 55, 41 },
                                          { 58, 31 }, { 64, 19 } };
    const struct cfb_position inner[] = { { 64, 40 }, { 70, 48 }, { 69, 55 },
                                          { 64, 59 }, { 58, 55 }, { 58, 49 },
                                          { 64, 40 } };
    DRAW_OK(DRAW_PATH(outer));
    return DRAW_PATH(inner);
}

static int water_drop(void)
{
    const struct cfb_position p[] = { { 64, 20 }, { 59, 30 }, { 53, 38 },
                                      { 48, 46 }, { 49, 53 }, { 55, 59 },
                                      { 64, 61 }, { 73, 59 }, { 79, 53 },
                                      { 80, 46 }, { 75, 38 }, { 69, 30 },
                                      { 64, 20 } };
    return DRAW_PATH(p);
}

static int tree(void)
{
    const struct cfb_position canopy[] = { { 64, 20 }, { 48, 39 }, { 55, 39 },
                                           { 43, 51 }, { 85, 51 }, { 73, 39 },
                                           { 80, 39 }, { 64, 20 } };
    DRAW_OK(DRAW_PATH(canopy));
    DRAW_OK(rect(60, 51, 68, 61));
    return line(43, 61, 85, 61);
}

static int flower(void)
{
    DRAW_OK(circle(64, 36, 4));
    DRAW_OK(circle(64, 25, 7));
    DRAW_OK(circle(75, 36, 7));
    DRAW_OK(circle(64, 47, 7));
    DRAW_OK(circle(53, 36, 7));
    DRAW_OK(line(64, 51, 64, 62));
    DRAW_OK(line(64, 57, 56, 53));
    return line(64, 59, 73, 55);
}

static int leaf(void)
{
    const struct cfb_position p[] = { { 44, 58 }, { 46, 45 }, { 53, 32 },
                                      { 64, 24 }, { 84, 21 }, { 84, 39 },
                                      { 76, 53 }, { 64, 59 }, { 50, 61 },
                                      { 44, 58 } };
    DRAW_OK(DRAW_PATH(p));
    DRAW_OK(line(44, 58, 77, 30));
    DRAW_OK(line(61, 44, 59, 33));
    return line(61, 44, 74, 48);
}

static int mountain(void)
{
    const struct cfb_position p[] = { { 30, 60 }, { 58, 22 },  { 79, 53 },
                                      { 90, 40 }, { 107, 60 }, { 30, 60 } };
    DRAW_OK(DRAW_PATH(p));
    DRAW_OK(line(51, 32, 58, 39));
    DRAW_OK(line(58, 39, 65, 32));
    DRAW_OK(line(86, 46, 90, 51));
    return line(90, 51, 94, 46);
}

/* ========================================================================== */
/* 24-27: Transport                                                           */
/* ========================================================================== */

static int rocket(void)
{
    const struct cfb_position body[] = { { 64, 20 }, { 71, 29 }, { 73, 49 },
                                         { 68, 55 }, { 60, 55 }, { 55, 49 },
                                         { 57, 29 }, { 64, 20 } };
    DRAW_OK(DRAW_PATH(body));
    DRAW_OK(circle(64, 36, 4));
    DRAW_OK(line(57, 44, 49, 55));
    DRAW_OK(line(49, 55, 60, 53));
    DRAW_OK(line(71, 44, 79, 55));
    DRAW_OK(line(79, 55, 68, 53));
    DRAW_OK(line(61, 56, 61, 61));
    DRAW_OK(line(67, 56, 67, 61));
    return line(64, 56, 64, 63);
}

static int airplane(void)
{
    const struct cfb_position outline[] = {
        { 64, 19 }, { 68, 36 }, { 88, 45 }, { 88, 49 }, { 68, 45 }, { 68, 56 },
        { 75, 59 }, { 75, 62 }, { 64, 58 }, { 53, 62 }, { 53, 59 }, { 60, 56 },
        { 60, 45 }, { 40, 49 }, { 40, 45 }, { 60, 36 }, { 64, 19 }
    };
    return DRAW_PATH(outline);
}

static int car(void)
{
    const struct cfb_position body[] = { { 38, 53 }, { 38, 42 }, { 46, 40 },
                                         { 53, 29 }, { 75, 29 }, { 82, 40 },
                                         { 90, 42 }, { 90, 53 }, { 38, 53 } };
    DRAW_OK(DRAW_PATH(body));
    DRAW_OK(line(49, 40, 79, 40));
    DRAW_OK(circle(48, 54, 6));
    return circle(80, 54, 6);
}

static int bicycle(void)
{
    DRAW_OK(circle(46, 52, 9));
    DRAW_OK(circle(83, 52, 9));
    DRAW_OK(line(46, 52, 57, 35));
    DRAW_OK(line(57, 35, 66, 52));
    DRAW_OK(line(66, 52, 46, 52));
    DRAW_OK(line(66, 52, 75, 35));
    DRAW_OK(line(75, 35, 83, 52));
    DRAW_OK(line(53, 35, 63, 35));
    DRAW_OK(line(72, 35, 81, 35));
    return dot(66, 52);
}

/* ========================================================================== */
/* 28-34: Electronics and status                                              */
/* ========================================================================== */

static int battery(bool charging)
{
    DRAW_OK(rect(39, 30, 84, 54));
    DRAW_OK(rect(84, 37, 90, 47));
    if(charging)
    {
        const struct cfb_position bolt[] = { { 67, 32 }, { 56, 44 }, { 64, 44 },
                                             { 61, 53 }, { 74, 40 }, { 66, 40 },
                                             { 67, 32 } };
        return DRAW_PATH(bolt);
    }
    DRAW_OK(rect(43, 34, 53, 50));
    DRAW_OK(rect(56, 34, 66, 50));
    return rect(69, 34, 79, 50);
}

static int lock_icon(bool unlocked)
{
    DRAW_OK(rect(48, 40, 80, 60));
    DRAW_OK(circle(64, 49, 2));
    DRAW_OK(line(64, 51, 64, 56));
    if(!unlocked)
    {
        const struct cfb_position shackle[] = { { 54, 40 }, { 54, 32 },
                                                { 57, 26 }, { 64, 24 },
                                                { 71, 26 }, { 74, 32 },
                                                { 74, 40 } };
        return DRAW_PATH(shackle);
    }
    const struct cfb_position open_shackle[] = { { 54, 40 }, { 54, 32 },
                                                 { 57, 26 }, { 64, 24 },
                                                 { 71, 26 }, { 78, 32 },
                                                 { 84, 30 } };
    return DRAW_PATH(open_shackle);
}

static int bell(void)
{
    const struct cfb_position p[] = { { 64, 22 }, { 60, 25 }, { 55, 29 },
                                      { 53, 36 }, { 53, 47 }, { 47, 54 },
                                      { 81, 54 }, { 75, 47 }, { 75, 36 },
                                      { 73, 29 }, { 68, 25 }, { 64, 22 } };
    DRAW_OK(DRAW_PATH(p));
    DRAW_OK(circle(64, 58, 3));
    return line(47, 55, 81, 55);
}

static int clock_icon(void)
{
    DRAW_OK(circle(64, 42, 19));
    DRAW_OK(line(64, 42, 64, 30));
    DRAW_OK(line(64, 42, 74, 49));
    DRAW_OK(dot(64, 24));
    DRAW_OK(dot(64, 60));
    DRAW_OK(dot(46, 42));
    return dot(82, 42);
}

static int gear(void)
{
    const struct cfb_position teeth[] = {
        { 59, 19 }, { 69, 19 }, { 71, 25 }, { 78, 23 }, { 84, 30 },
        { 81, 36 }, { 88, 39 }, { 88, 47 }, { 81, 49 }, { 84, 55 },
        { 78, 61 }, { 71, 58 }, { 69, 63 }, { 59, 63 }, { 57, 58 },
        { 50, 61 }, { 44, 55 }, { 47, 49 }, { 40, 47 }, { 40, 39 },
        { 47, 36 }, { 44, 30 }, { 50, 23 }, { 57, 25 }, { 59, 19 }
    };
    DRAW_OK(DRAW_PATH(teeth));
    return circle(64, 41, 9);
}

/* ========================================================================== */
/* 35-42: Objects and symbols                                                 */
/* ========================================================================== */

static int home(void)
{
    const struct cfb_position roof[] = { { 39, 40 }, { 64, 21 }, { 89, 40 } };
    DRAW_OK(DRAW_PATH(roof));
    DRAW_OK(rect(46, 40, 82, 61));
    return rect(59, 47, 69, 61);
}

static int music(void)
{
    DRAW_OK(line(63, 31, 63, 56));
    DRAW_OK(line(63, 31, 84, 26));
    DRAW_OK(line(84, 26, 84, 51));
    DRAW_OK(line(63, 36, 84, 31));
    DRAW_OK(circle(56, 56, 6));
    return circle(77, 51, 6);
}

static int gift(void)
{
    DRAW_OK(rect(43, 38, 85, 61));
    DRAW_OK(rect(40, 32, 88, 40));
    DRAW_OK(line(64, 32, 64, 61));
    DRAW_OK(circle(57, 26, 7));
    return circle(71, 26, 7);
}

static int umbrella(void)
{
    const struct cfb_position canopy[] = { { 38, 42 }, { 40, 35 }, { 47, 28 },
                                           { 55, 24 }, { 64, 22 }, { 73, 24 },
                                           { 81, 28 }, { 88, 35 }, { 90, 42 },
                                           { 80, 38 }, { 72, 42 }, { 64, 38 },
                                           { 56, 42 }, { 48, 38 }, { 38, 42 } };
    DRAW_OK(DRAW_PATH(canopy));
    DRAW_OK(line(64, 38, 64, 56));
    DRAW_OK(line(64, 56, 61, 61));
    return line(61, 61, 55, 61);
}

static int checkmark(void)
{
    const struct cfb_position p[] = { { 43, 42 }, { 56, 54 }, { 85, 25 } };
    return DRAW_PATH(p);
}

static int cross(void)
{
    DRAW_OK(line(47, 25, 81, 59));
    return line(81, 25, 47, 59);
}

static int arrow_up(void)
{
    DRAW_OK(line(64, 22, 64, 61));
    DRAW_OK(line(64, 22, 48, 38));
    return line(64, 22, 80, 38);
}

static int arrow_down(void)
{
    DRAW_OK(line(64, 20, 64, 59));
    DRAW_OK(line(64, 59, 48, 43));
    return line(64, 59, 80, 43);
}

/* ========================================================================== */
/* Graphics dispatcher: every icon has an implementation.                     */
/* ========================================================================== */

static int draw_icon(enum emoji_type icon)
{
    switch(icon)
    {
        case EMOJI_SMILE:
        case EMOJI_SAD:
        case EMOJI_NEUTRAL:
        case EMOJI_WINK:
        case EMOJI_SURPRISED:
        case EMOJI_ANGRY:
            return face(icon);
        case EMOJI_GRIN:
        case EMOJI_LAUGH:
        case EMOJI_CRY:
        case EMOJI_HAPPY_TEARS:
        case EMOJI_SLEEPY:
        case EMOJI_COOL:
        case EMOJI_LOVE_EYES:
        case EMOJI_TONGUE:
        case EMOJI_CONFUSED:
        case EMOJI_KISS:
        case EMOJI_SCARED:
        case EMOJI_BLUSH:
            return face_extra(icon);
        case EMOJI_HEART:
            return heart();
        case EMOJI_BROKEN_HEART:
            return broken_heart();
        case EMOJI_WIFI:
            return wifi();
        case EMOJI_BLUETOOTH:
            return bluetooth();
        case EMOJI_STAR:
            return star();
        case EMOJI_SUN:
            return sun();
        case EMOJI_MOON:
            return moon();
        case EMOJI_CLOUD:
            return cloud();
        case EMOJI_RAIN:
            return rain();
        case EMOJI_LIGHTNING:
            return lightning();
        case EMOJI_SNOWFLAKE:
            return snowflake();
        case EMOJI_FIRE:
            return fire();
        case EMOJI_WATER_DROP:
            return water_drop();
        case EMOJI_TREE:
            return tree();
        case EMOJI_FLOWER:
            return flower();
        case EMOJI_LEAF:
            return leaf();
        case EMOJI_MOUNTAIN:
            return mountain();
        case EMOJI_ROCKET:
            return rocket();
        case EMOJI_AIRPLANE:
            return airplane();
        case EMOJI_CAR:
            return car();
        case EMOJI_BICYCLE:
            return bicycle();
        case EMOJI_BATTERY:
            return battery(false);
        case EMOJI_BATTERY_CHARGING:
            return battery(true);
        case EMOJI_LOCK:
            return lock_icon(false);
        case EMOJI_UNLOCK:
            return lock_icon(true);
        case EMOJI_BELL:
            return bell();
        case EMOJI_CLOCK:
            return clock_icon();
        case EMOJI_GEAR:
            return gear();
        case EMOJI_HOME:
            return home();
        case EMOJI_MUSIC:
            return music();
        case EMOJI_GIFT:
            return gift();
        case EMOJI_UMBRELLA:
            return umbrella();
        case EMOJI_CHECK:
            return checkmark();
        case EMOJI_CROSS:
            return cross();
        case EMOJI_ARROW_UP:
            return arrow_up();
        case EMOJI_ARROW_DOWN:
            return arrow_down();
        case EMOJI_COUNT:
            return -EINVAL;
        default:
            return -EINVAL;
    }
}

/* ========================================================================== */
/* SSD1306 initialization                                                     */
/* ========================================================================== */

static int oled_initialize(void)
{
    int status;

    if(!device_is_ready(i2c_dev))
    {
        printk("ERROR: I2C controller is not ready\n");
        return -ENODEV;
    }

    /* This recovery fixed the original -EBUSY on this Heltec board. */
    printk("Recovering I2C bus...\n");
    status = i2c_recover_bus(i2c_dev);
    if(status < 0)
    {
        printk("ERROR: I2C recovery: %d\n", status);
        return status;
    }

    /* app.overlay sets zephyr,deferred-init on this display node. */
    status = device_init(oled_dev);
    if(status < 0)
    {
        printk("ERROR: deferred SSD1306 init: %d\n", status);
        return status;
    }
    if(!device_is_ready(oled_dev))
    {
        printk("ERROR: display is not ready\n");
        return -ENODEV;
    }

    status = display_set_pixel_format(oled_dev, PIXEL_FORMAT_MONO01);
    if(status < 0)
    {
        return status;
    }
    status = cfb_framebuffer_init(oled_dev);
    if(status < 0)
    {
        return status;
    }
    status = cfb_framebuffer_clear(oled_dev, false);
    if(status < 0)
    {
        return status;
    }
    status = display_blanking_off(oled_dev);
    if(status < 0)
    {
        return status;
    }

    printk("OLED ready: %s\n", oled_dev->name);
    return 0;
}

/* Draw ONE complete frame; no partial drawing is visible on the OLED. */
static int show_icon(enum emoji_type icon)
{
    int status;

    if(icon >= EMOJI_COUNT)
    {
        return -EINVAL;
    }
    status = cfb_framebuffer_clear(oled_dev, false);
    if(status < 0)
    {
        return status;
    }

    /* Short labels are chosen to fit comfortably on 128px screen. */
    status = cfb_print(oled_dev, icon_names[icon], 0, 0);
    if(status < 0)
    {
        return status;
    }
    status = draw_icon(icon);
    if(status < 0)
    {
        return status;
    }
    return cfb_framebuffer_finalize(oled_dev);
}

int main(void)
{
    int status;

    printk("\n=== Lesson 05: Zephyr CFB Icon Gallery (%d icons) ===\n", EMOJI_COUNT);

    status = oled_initialize();
    if(status < 0)
    {
        printk("OLED setup failed: %d\n", status);
        return status;
    }

    while(true)
    {
        for(int i = 0; i < EMOJI_COUNT; ++i)
        {
            printk("Icon %02d/%d: %s\n", i + 1, EMOJI_COUNT, icon_names[i]);

            status = show_icon((enum emoji_type)i);
            if(status < 0)
            {
                printk("Drawing '%s' failed: %d\n", icon_names[i], status);
                return status;
            }

            k_sleep(K_MSEC(ICON_DURATION_MS));
        }
    }

    return 0;
}
