/* RideCast v1 normalized application model. No transport or OLED dependencies.
 * JSON on phone links today; later UART/SPI uses the same logical fields.
 */
#ifndef RC_APP_MESSAGE_H_
#define RC_APP_MESSAGE_H_
#include <stdint.h>
#include <stdbool.h>

/* Includes terminating NUL; use newline-delimited JSON on TCP and BLE. */
#define RC_JSON_LINE_MAX 512U

typedef enum { RC_APP_FROM_WIFI = 1, RC_APP_FROM_BLE = 2 } rc_app_source_t;
typedef enum { RC_APP_NAV = 1, RC_APP_NOTIFICATION = 2, RC_APP_MEDIA = 3 } rc_app_kind_t;
typedef enum {
    RC_NAV_LEFT, RC_NAV_RIGHT, RC_NAV_STRAIGHT, RC_NAV_ARRIVED
} rc_nav_maneuver_t;
typedef enum { RC_MEDIA_PLAY, RC_MEDIA_PAUSE, RC_MEDIA_STOP } rc_media_state_t;

typedef struct {
    rc_nav_maneuver_t maneuver;
    uint32_t distance_m;
    uint16_t speed_kmh;
    uint32_t remaining_m;
    uint32_t eta_sec;
    bool gps_valid;
    int32_t lat_e7;        /* Degrees * 1e7: no floating-point JSON needed. */
    int32_t lon_e7;
    char road[64];
} rc_app_nav_t;

typedef struct {
    char app[24];
    char sender[48];
    char title[64];
    char text[144];
} rc_app_notification_t;

typedef struct {
    rc_media_state_t state;
    char app[24];
    char title[80];
    char artist[64];
    char album[64];
    uint32_t position_ms;
    uint32_t duration_ms;
} rc_app_media_t;

typedef struct {
    uint8_t version;
    rc_app_source_t source;
    rc_app_kind_t kind;
    uint32_t seq;
    union {
        rc_app_nav_t nav;
        rc_app_notification_t notification;
        rc_app_media_t media;
    } payload;
} rc_app_message_t;

/* Parse a mutable, NUL-terminated, complete JSON object; ingress is only
 * invoked AFTER transport authentication. Returns 0 or negative errno.
 * The roles accepted for each authenticated source are defined in
 * ridecast_config.h (the caller must enforce authentication).
 */
int rc_app_json_ingest(char *line, rc_app_source_t source);
#endif
