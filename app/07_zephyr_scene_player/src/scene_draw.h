/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SCENE_DRAW_H_
#define SCENE_DRAW_H_

#include <zephyr/device.h>
#include <stddef.h>
#include <stdint.h>

/* Six independent, procedurally animated, 1-bit OLED scenes. */
enum scene_id
{
    SCENE_ROCKET_LAUNCH,
    SCENE_CAR_DRIVING,
    SCENE_RAINSTORM,
    SCENE_SUNRISE,
    SCENE_BOUNCING_BALL,
    SCENE_SPACE_ADVENTURE,
    SCENE_COUNT
};

enum scene_category
{
    SCENE_CAT_SPACE,
    SCENE_CAT_MOTION,
    SCENE_CAT_NATURE,
    SCENE_CATEGORY_COUNT
};

const char* scene_name(enum scene_id scene);
const char* scene_category_name(enum scene_category category);
size_t scene_category_count(enum scene_category category);
enum scene_id scene_category_get(enum scene_category category, size_t index);

/* Draw scene into the current CFB buffer. Does not clear/finalize buffer. */
int scene_draw(const struct device* display, enum scene_id scene, uint32_t frame);

#endif /* SCENE_DRAW_H_ */
