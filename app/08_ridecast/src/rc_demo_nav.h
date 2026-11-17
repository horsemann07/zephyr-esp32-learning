/* RideCast OLED navigation snapshot. Source-aware for Wi-Fi/BLE fallback. */
#ifndef RC_DEMO_NAV_H_
#define RC_DEMO_NAV_H_
#include <stdbool.h>
#include "heltec_graphic.h"
#include "rc_err_sts.h"
#include "rc_app_message.h"

/* Legacy ASCII: NAV <LEFT|RIGHT|STRAIGHT|ARRIVED> <meters> <kmh> <gps:0|1>
 * Call only after authentication. The original wrapper defaults to Wi-Fi.
 */
rc_err_sts_t rc_demo_nav_receive(const char *text);
rc_err_sts_t rc_demo_nav_receive_from(const char *text, rc_app_source_t source);

/* Bridge: keep transport identity with the OLED preview. */
void rc_demo_nav_publish(const heltec_nav_screen_t *screen);
void rc_demo_nav_publish_from(const heltec_nav_screen_t *screen,
                              rc_app_source_t source);

/* A NAV snapshot expires after 8 seconds. Disabled and unauthenticated sources
 * are never selected; if preferred source expires, the other can take over.
 */
bool rc_demo_nav_latest(heltec_nav_screen_t *out); /* compatibility wrapper */
bool rc_demo_nav_latest_authorized(heltec_nav_screen_t *out,
                                   bool wifi_authenticated,
                                   bool ble_authenticated);
void rc_demo_nav_clear_source(rc_app_source_t source);
void rc_demo_nav_clear(void);
#endif
