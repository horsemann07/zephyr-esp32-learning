/* SPDX-License-Identifier: MIT
 * RideCast Heltec 128x64 OLED navigation UI (Zephyr CFB / SSD1306).
 * Graphics layer only: no Wi-Fi, TCP, phone, or RideCast protocol dependency.
 */
#ifndef HELTEC_GRAPHIC_H_
#define HELTEC_GRAPHIC_H_

#include <stdbool.h>
#include <stdint.h>
#include "rc_err_sts.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HELTEC_TURN_NONE = 0,
    HELTEC_TURN_STRAIGHT,
    HELTEC_TURN_LEFT,
    HELTEC_TURN_RIGHT,
    HELTEC_TURN_ARRIVED
} heltec_turn_t;

typedef enum {
    HELTEC_LINK_DEMO = 0, /* No radio connection: locally generated samples */
    HELTEC_LINK_OFFLINE,  /* Wi-Fi unavailable */
    HELTEC_LINK_WIFI,     /* Associated to AP, no TCP peer */
    HELTEC_LINK_ONLINE    /* Wi-Fi associated and demo TCP peer connected */
} heltec_link_t;

typedef struct {
    heltec_turn_t turn;
    heltec_link_t link;
    uint16_t distance_m;    /* On-screen distance is capped at 999 m */
    uint16_t speed_kmh;     /* 0...240 */
    bool gps_valid;
} heltec_nav_screen_t;

/* Must be called once, from thread context, before render(). */
rc_err_sts_t heltec_graphic_init(void);

/* Only the application/UI thread may call this function.
 * Uses a full back-buffer and one finalize() per frame.
 * v0.5: uncluttered footer: speed | GPS: OK/NO, no separator lines.
 */
rc_err_sts_t heltec_graphic_render(const heltec_nav_screen_t *screen);

#ifdef __cplusplus
}
#endif
#endif /* HELTEC_GRAPHIC_H_ */
