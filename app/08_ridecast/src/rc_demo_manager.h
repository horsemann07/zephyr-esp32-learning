#ifndef RC_DEMO_MANAGER_H_
#define RC_DEMO_MANAGER_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rc_err_sts.h"
#include "rc_app_sim_config.h"

/* Wi-Fi NAV and BLE content are independent authorized sessions. */
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
    rc_demo_link_t active; /* UI preference: Wi-Fi, then BLE; use flags for authorization */
    bool wifi_authenticated;
    bool ble_authenticated;
    rc_demo_stage_t stage;
    bool wifi_ready;
    bool ble_ready;
    bool wifi_registered;
    bool ble_connected;
    uint8_t wifi_stations;     /* AP-associated stations, not app-authorized */
    bool wifi_tcp_connected;   /* Physical TCP socket, not app-authorized */
    uint32_t pin;              /* Only valid when pin_visible is true. */
    bool pin_visible;
    rc_err_sts_t error;        /* Application session failure */
    rc_err_sts_t wifi_radio_error;
    rc_err_sts_t ble_radio_error;
} rc_demo_snapshot_t;

void rc_demo_manager_init(bool wifi_registered);
void rc_demo_manager_wifi_radio(bool up);
void rc_demo_manager_ble_radio(bool up);
void rc_demo_manager_radio_failure(rc_demo_link_t radio, rc_err_sts_t reason);
void rc_demo_manager_wifi_station_count(uint8_t stations);
void rc_demo_manager_wifi_tcp(bool connected);
rc_err_sts_t rc_demo_manager_wifi_peer(void);
/* Exactly 6 numeric ASCII digits; only unauthenticated enrollment expires in 60s. */
rc_err_sts_t rc_demo_manager_wifi_pin(const char *pin, uint8_t new_token[16]);
rc_err_sts_t rc_demo_manager_wifi_token(const uint8_t token[16]);
void rc_demo_manager_wifi_lost(void);
rc_err_sts_t rc_demo_manager_ble_peer(void);
void rc_demo_manager_ble_passkey(uint32_t pin);
rc_err_sts_t rc_demo_manager_ble_authenticated(void);
void rc_demo_manager_ble_lost(void);
/* Ignored unless that transport owns the pending or active session. */
bool rc_demo_manager_link_failure(rc_demo_link_t link, rc_err_sts_t reason);
/* Refreshes pending-authentication expiry and clears transient failure display. */
rc_demo_snapshot_t rc_demo_manager_get(void);

void rc_demo_manager_simulate_links(bool wifi, bool ble);
#endif
