#ifndef RC_DEMO_MANAGER_H_
#define RC_DEMO_MANAGER_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rc_err_sts.h"

/* Only one authorized application-content session; both radios may be on. */
typedef enum { RC_DEMO_LINK_NONE, RC_DEMO_LINK_WIFI, RC_DEMO_LINK_BLE } rc_demo_link_t;
typedef enum {
    RC_DEMO_WAIT,
    RC_DEMO_WIFI_ENROLL,
    RC_DEMO_WIFI_TOKEN,
    RC_DEMO_BLE_PAIR,
    RC_DEMO_ACTIVE,
    RC_DEMO_FAILED
} rc_demo_stage_t;

typedef struct {
    rc_demo_link_t active;
    rc_demo_stage_t stage;
    bool wifi_ready;
    bool ble_ready;
    bool wifi_registered;
    bool ble_connected;
    uint32_t pin;     /* Only valid in *_ENROLL / BLE_PAIR stages. */
    bool pin_visible;
    rc_err_sts_t error;
} rc_demo_snapshot_t;

void rc_demo_manager_init(bool wifi_registered);
void rc_demo_manager_wifi_radio(bool up);
void rc_demo_manager_ble_radio(bool up);
rc_err_sts_t rc_demo_manager_wifi_peer(void);
/* Exactly 6 numeric ASCII digits. Enrollment is valid for 60 seconds. */
rc_err_sts_t rc_demo_manager_wifi_pin(const char *pin, uint8_t new_token[16]);
rc_err_sts_t rc_demo_manager_wifi_token(const uint8_t token[16]);
void rc_demo_manager_wifi_lost(void);
rc_err_sts_t rc_demo_manager_ble_peer(void);
void rc_demo_manager_ble_passkey(uint32_t pin);
rc_err_sts_t rc_demo_manager_ble_authenticated(void);
void rc_demo_manager_ble_lost(void);
void rc_demo_manager_failure(rc_err_sts_t reason);
rc_demo_snapshot_t rc_demo_manager_get(void);
#endif
