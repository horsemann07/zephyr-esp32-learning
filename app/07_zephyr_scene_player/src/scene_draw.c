/* SPDX-License-Identifier: Apache-2.0 */
/* Procedural animation graphics for the Heltec 128x64 SSD1306 OLED. */
#include <zephyr/kernel.h>
#include <zephyr/display/cfb.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <stdbool.h>
#include "scene_draw.h"

#define SCREEN_W 128
#define SCREEN_H 64

#define CHECK_DRAW(expr)  \
    do                    \
    {                     \
        int rc_ = (expr); \
        if(rc_ < 0)       \
        {                 \
            return rc_;   \
        }                 \
    } while(0)

static int point(const struct device* dev, int x, int y)
{
    if(x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H)
    {
        return 0;
    }
    const struct cfb_position p = { x, y };
    return cfb_draw_point(dev, &p);
}

/* Clamped endpoints keep moving shapes inside the framebuffer. */
static int line(const struct device* dev, int x0, int y0, int x1, int y1)
{
    x0                          = CLAMP(x0, 0, SCREEN_W - 1);
    x1                          = CLAMP(x1, 0, SCREEN_W - 1);
    y0                          = CLAMP(y0, 0, SCREEN_H - 1);
    y1                          = CLAMP(y1, 0, SCREEN_H - 1);
    const struct cfb_position a = { x0, y0 };
    const struct cfb_position b = { x1, y1 };
    return cfb_draw_line(dev, &a, &b);
}

static int rect(const struct device* dev, int x0, int y0, int x1, int y1)
{
    if(x1 < 0 || x0 >= SCREEN_W || y1 < 0 || y0 >= SCREEN_H)
    {
        return 0;
    }
    const struct cfb_position a = { CLAMP(x0, 0, SCREEN_W - 1), CLAMP(y0, 0, SCREEN_H - 1) };
    const struct cfb_position b = { CLAMP(x1, 0, SCREEN_W - 1), CLAMP(y1, 0, SCREEN_H - 1) };
    return cfb_draw_rect(dev, &a, &b);
}

static int circle(const struct device* dev, int x, int y, int radius)
{
    if(x - radius < 0 || y - radius < 0 || x + radius >= SCREEN_W || y + radius >= SCREEN_H)
    {
        return 0;
    }
    const struct cfb_position center = { x, y };
    return cfb_draw_circle(dev, &center, (uint16_t)radius);
}

static int stars(const struct device* dev, uint32_t frame, int speed)
{
    /* Different y positions and movement speeds produce a parallax effect. */
    for(int i = 0; i < 13; ++i)
    {
        int x = (i * 37 + 117 - (int)((frame * (uint32_t)speed) % 117U)) % 117 + 5;
        int y = 16 + ((i * 19) % 39);
        if(((frame / 4U) + (uint32_t)i) % 3U != 0U)
        {
            CHECK_DRAW(point(dev, x, y));
        }
    }
    return 0;
}

static int rocket_launch(const struct device* dev, uint32_t frame)
{
    int phase = (int)(frame % 48U);
    int y     = 38 - (phase * 28) / 47; /* nose ascends from y=38 to y=10 */

    CHECK_DRAW(stars(dev, frame, 1));
    CHECK_DRAW(line(dev, 5, 60, 45, 60));
    CHECK_DRAW(line(dev, 83, 60, 123, 60));
    CHECK_DRAW(line(dev, 49, 59, 79, 59)); /* Launch pad */

    /* Rocket: pointy nose, capsule, window, fins, alternating flame. */
    CHECK_DRAW(line(dev, 64, y, 57, y + 9));
    CHECK_DRAW(line(dev, 64, y, 71, y + 9));
    CHECK_DRAW(line(dev, 57, y + 9, 57, y + 20));
    CHECK_DRAW(line(dev, 71, y + 9, 71, y + 20));
    CHECK_DRAW(line(dev, 57, y + 20, 71, y + 20));
    CHECK_DRAW(circle(dev, 64, y + 12, 3));
    CHECK_DRAW(line(dev, 57, y + 15, 51, y + 22));
    CHECK_DRAW(line(dev, 51, y + 22, 58, y + 20));
    CHECK_DRAW(line(dev, 71, y + 15, 77, y + 22));
    CHECK_DRAW(line(dev, 77, y + 22, 70, y + 20));
    CHECK_DRAW(line(dev, 61, y + 20, 64, y + 24 + (frame % 2U)));
    CHECK_DRAW(line(dev, 67, y + 20, 64, y + 24 + (frame % 2U)));
    return 0;
}

static int car_driving(const struct device* dev, uint32_t frame)
{
    int bob = ((frame / 2U) % 2U) ? 1 : 0;
    /* Low hills on the horizon. */
    CHECK_DRAW(line(dev, 0, 36, 26, 26));
    CHECK_DRAW(line(dev, 26, 26, 54, 36));
    CHECK_DRAW(line(dev, 54, 36, 79, 30));
    CHECK_DRAW(line(dev, 79, 30, 105, 36));
    CHECK_DRAW(line(dev, 105, 36, 127, 32));
    CHECK_DRAW(line(dev, 0, 54, 127, 54));
    CHECK_DRAW(line(dev, 0, 62, 127, 62));

    /* White road markings move to the left (like an endless road). */
    for(int i = 0; i < 5; ++i)
    {
        int x = ((i * 33 + 128) - (int)((frame * 4U) % 33U));
        if(x < 128)
        {
            CHECK_DRAW(line(dev, x, 58, x + 15, 58));
        }
    }

    /* Car stays near center while the background scrolls. */
    CHECK_DRAW(rect(dev, 38, 43 + bob, 88, 49 + bob));
    CHECK_DRAW(line(dev, 46, 43 + bob, 53, 35 + bob));
    CHECK_DRAW(line(dev, 53, 35 + bob, 74, 35 + bob));
    CHECK_DRAW(line(dev, 74, 35 + bob, 82, 43 + bob));
    CHECK_DRAW(line(dev, 63, 35 + bob, 63, 43 + bob));
    CHECK_DRAW(circle(dev, 49, 50 + bob, 4));
    CHECK_DRAW(circle(dev, 78, 50 + bob, 4));
    return 0;
}

static int rainstorm(const struct device* dev, uint32_t frame)
{
    /* Two overlapping outlined clouds. */
    CHECK_DRAW(line(dev, 18, 28, 18, 23));
    CHECK_DRAW(line(dev, 18, 23, 25, 19));
    CHECK_DRAW(line(dev, 25, 19, 34, 20));
    CHECK_DRAW(line(dev, 34, 20, 41, 15));
    CHECK_DRAW(line(dev, 41, 15, 54, 16));
    CHECK_DRAW(line(dev, 54, 16, 62, 23));
    CHECK_DRAW(line(dev, 62, 23, 62, 28));
    CHECK_DRAW(line(dev, 18, 28, 62, 28));
    CHECK_DRAW(line(dev, 56, 30, 67, 23));
    CHECK_DRAW(line(dev, 67, 23, 81, 23));
    CHECK_DRAW(line(dev, 81, 23, 86, 17));
    CHECK_DRAW(line(dev, 86, 17, 99, 18));
    CHECK_DRAW(line(dev, 99, 18, 110, 25));
    CHECK_DRAW(line(dev, 110, 25, 110, 30));
    CHECK_DRAW(line(dev, 56, 30, 110, 30));

    for(int i = 0; i < 19; ++i)
    {
        int x = 4 + (i * 29) % 119;
        int y = 33 + ((i * 11 + (int)(frame * 4U)) % 25);
        CHECK_DRAW(line(dev, x, y, x - 2, y + 4));
    }
    CHECK_DRAW(line(dev, 0, 61, 127, 61));
    /* Animated concentric ripple hints. */
    int ripple = (int)(frame % 5U) + 2;
    CHECK_DRAW(line(dev, 28 - ripple, 60, 28 + ripple, 60));
    CHECK_DRAW(line(dev, 90 - ripple, 60, 90 + ripple, 60));
    return 0;
}

static int sunrise(const struct device* dev, uint32_t frame)
{
    int phase = (int)(frame % 80U);
    int sun_y = 47 - (phase * 23) / 79;

    CHECK_DRAW(circle(dev, 64, sun_y, 9));
    CHECK_DRAW(line(dev, 64, sun_y - 15, 64, sun_y - 12));
    CHECK_DRAW(line(dev, 49, sun_y, 52, sun_y));
    CHECK_DRAW(line(dev, 76, sun_y, 79, sun_y));
    CHECK_DRAW(line(dev, 53, sun_y - 11, 56, sun_y - 8));
    CHECK_DRAW(line(dev, 75, sun_y - 11, 72, sun_y - 8));
    CHECK_DRAW(line(dev, 0, 56, 34, 31));
    CHECK_DRAW(line(dev, 34, 31, 59, 52));
    CHECK_DRAW(line(dev, 52, 56, 92, 25));
    CHECK_DRAW(line(dev, 92, 25, 127, 56));
    CHECK_DRAW(line(dev, 0, 57, 127, 57));
    CHECK_DRAW(line(dev, 0, 62, 127, 62));
    return 0;
}

static int bouncing_ball(const struct device* dev, uint32_t frame)
{
    int phase    = (int)(frame % 32U);
    int up       = (phase <= 16) ? phase : 32 - phase;
    int jump     = up * (32 - up) * 25 / 256;
    int x        = 12 + (int)((frame * 2U) % 102U);
    int y        = 50 - jump;
    int shadow_r = 8 - jump / 6;

    CHECK_DRAW(line(dev, 0, 57, 127, 57));
    CHECK_DRAW(line(dev, 0, 58, 127, 58));
    CHECK_DRAW(line(dev, x - shadow_r, 60, x + shadow_r, 60));
    CHECK_DRAW(circle(dev, x, y, 6));
    CHECK_DRAW(circle(dev, x - 2, y - 2, 1));
    /* Little wall to bounce past. */
    CHECK_DRAW(rect(dev, 114, 45, 124, 56));
    return 0;
}

static int space_adventure(const struct device* dev, uint32_t frame)
{
    int bob        = (int)((frame / 3U) % 4U) - 1;
    int asteroid_x = 120 - (int)((frame * 2U) % 122U);

    CHECK_DRAW(stars(dev, frame, 3));
    /* Small distant planet. */
    CHECK_DRAW(circle(dev, 104, 25, 9));
    CHECK_DRAW(line(dev, 91, 26, 117, 22));

    if(asteroid_x >= 9 && asteroid_x <= 119)
    {
        CHECK_DRAW(circle(dev, asteroid_x, 51, 6));
        CHECK_DRAW(point(dev, asteroid_x - 2, 50));
        CHECK_DRAW(point(dev, asteroid_x + 2, 53));
    }

    /* Ship with a rear thruster and animated exhaust. */
    CHECK_DRAW(line(dev, 49, 36 + bob, 18, 26 + bob));
    CHECK_DRAW(line(dev, 49, 36 + bob, 18, 46 + bob));
    CHECK_DRAW(line(dev, 18, 26 + bob, 23, 36 + bob));
    CHECK_DRAW(line(dev, 23, 36 + bob, 18, 46 + bob));
    CHECK_DRAW(circle(dev, 35, 36 + bob, 3));
    CHECK_DRAW(line(dev, 19, 32 + bob, 12 - (frame % 4U), 32 + bob));
    CHECK_DRAW(line(dev, 19, 40 + bob, 12 - (frame % 4U), 40 + bob));
    return 0;
}

const char* scene_name(enum scene_id scene)
{
    static const char* const names[SCENE_COUNT] = {
        "ROCKET LAUNCH", "CAR DRIVING",   "RAINSTORM",
        "SUNRISE",       "BOUNCING BALL", "SPACE ADVENTURE"
    };
    return ((unsigned int)scene < SCENE_COUNT) ? names[scene] : "UNKNOWN";
}

const char* scene_category_name(enum scene_category category)
{
    static const char* const names[SCENE_CATEGORY_COUNT] = { "SPACE", "MOTION", "NATURE" };
    return ((unsigned int)category < SCENE_CATEGORY_COUNT) ? names[category] : "UNKNOWN";
}

size_t scene_category_count(enum scene_category category)
{
    return ((unsigned int)category < SCENE_CATEGORY_COUNT) ? 2U : 0U;
}

enum scene_id scene_category_get(enum scene_category category, size_t index)
{
    static const enum scene_id entries[SCENE_CATEGORY_COUNT][2] = {
        { SCENE_ROCKET_LAUNCH, SCENE_SPACE_ADVENTURE },
        { SCENE_CAR_DRIVING, SCENE_BOUNCING_BALL },
        { SCENE_RAINSTORM, SCENE_SUNRISE },
    };
    if((unsigned int)category >= SCENE_CATEGORY_COUNT || index >= 2U)
    {
        return SCENE_COUNT;
    }
    return entries[category][index];
}

int scene_draw(const struct device* display, enum scene_id scene, uint32_t frame)
{
    if(display == NULL)
    {
        return -EINVAL;
    }
    switch(scene)
    {
        case SCENE_ROCKET_LAUNCH:
            return rocket_launch(display, frame);
        case SCENE_CAR_DRIVING:
            return car_driving(display, frame);
        case SCENE_RAINSTORM:
            return rainstorm(display, frame);
        case SCENE_SUNRISE:
            return sunrise(display, frame);
        case SCENE_BOUNCING_BALL:
            return bouncing_ball(display, frame);
        case SCENE_SPACE_ADVENTURE:
            return space_adventure(display, frame);
        default:
            return -EINVAL;
    }
}
