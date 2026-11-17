#include "rc_demo_manager.h"
#include "rc_demo_store.h"
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <string.h>
#include <stdio.h>

#define RC_PAIR_TTL_MS 60000
#define RC_FAILURE_DISPLAY_MS 3000
K_MUTEX_DEFINE(manager_lock);
static rc_demo_snapshot_t state;
static rc_demo_link_t pending;
static int64_t deadline;
static unsigned int wrong_attempts;
static int64_t failed_until;

/* UI represents one foreground state, but both transports can be authorized.
 * Only one *pending* pairing operation is supported at a time. */
static void update_active_locked(void)
{
    state.active = state.wifi_authenticated ? RC_DEMO_LINK_WIFI :
                   state.ble_authenticated ? RC_DEMO_LINK_BLE : RC_DEMO_LINK_NONE;
}

static void refresh_locked(void)
{
    const int64_t now = k_uptime_get();
    if (pending == RC_DEMO_LINK_WIFI && now >= deadline) {
        pending = RC_DEMO_LINK_NONE;
        deadline = 0;
        wrong_attempts = 0;
        state.stage = RC_DEMO_FAILED;
        state.error = RC_ERR_TIMEOUT;
        state.pin_visible = false;
        failed_until = now + RC_FAILURE_DISPLAY_MS;
    }
    if (state.stage == RC_DEMO_FAILED && pending == RC_DEMO_LINK_NONE &&
        now >= failed_until) {
        state.stage = state.active != RC_DEMO_LINK_NONE ? RC_DEMO_ACTIVE : RC_DEMO_WAIT;
        state.error = RC_OK;
        state.pin_visible = false;
    }
}

static void fail_owned_locked(rc_demo_link_t owner, rc_err_sts_t reason)
{
    if (pending == owner) pending = RC_DEMO_LINK_NONE;
    if (owner == RC_DEMO_LINK_WIFI) {
        state.wifi_authenticated = false;
        deadline = 0;
        wrong_attempts = 0;
    } else if (owner == RC_DEMO_LINK_BLE) {
        state.ble_authenticated = false;
    }
    update_active_locked();
    state.stage = RC_DEMO_FAILED;
    state.error = reason;
    state.pin_visible = false;
    failed_until = k_uptime_get() + RC_FAILURE_DISPLAY_MS;
}

void rc_demo_manager_init(bool wifi_registered)
{
    k_mutex_lock(&manager_lock, K_FOREVER);
    memset(&state, 0, sizeof(state));
    state.wifi_registered = wifi_registered;
    state.stage = RC_DEMO_WAIT;
    state.error = RC_OK;
    state.wifi_radio_error = RC_OK;
    state.ble_radio_error = RC_OK;
    pending = RC_DEMO_LINK_NONE;
    deadline = 0;
    wrong_attempts = 0;
    failed_until = 0;
    k_mutex_unlock(&manager_lock);
}

void rc_demo_manager_wifi_radio(bool up)
{
    k_mutex_lock(&manager_lock, K_FOREVER);
    state.wifi_ready = up;
    if (up) state.wifi_radio_error = RC_OK;
    k_mutex_unlock(&manager_lock);
}

void rc_demo_manager_ble_radio(bool up)
{
    k_mutex_lock(&manager_lock, K_FOREVER);
    state.ble_ready = up;
    if (up) state.ble_radio_error = RC_OK;
    k_mutex_unlock(&manager_lock);
}

void rc_demo_manager_radio_failure(rc_demo_link_t radio, rc_err_sts_t reason)
{
    k_mutex_lock(&manager_lock, K_FOREVER);
    if (radio == RC_DEMO_LINK_WIFI) {
        state.wifi_ready = false;
        state.wifi_radio_error = reason;
    } else if (radio == RC_DEMO_LINK_BLE) {
        state.ble_ready = false;
        state.ble_radio_error = reason;
    }
    /* A failure on an unused radio must never overwrite an active session. */
    k_mutex_unlock(&manager_lock);
}

void rc_demo_manager_wifi_station_count(uint8_t stations)
{
    k_mutex_lock(&manager_lock, K_FOREVER);
    state.wifi_stations = stations;
    k_mutex_unlock(&manager_lock);
}

void rc_demo_manager_wifi_tcp(bool connected)
{
    k_mutex_lock(&manager_lock, K_FOREVER);
    state.wifi_tcp_connected = connected;
    k_mutex_unlock(&manager_lock);
}

rc_err_sts_t rc_demo_manager_wifi_peer(void)
{
    k_mutex_lock(&manager_lock, K_FOREVER);
    refresh_locked();
    if (state.wifi_authenticated || pending != RC_DEMO_LINK_NONE) {
        k_mutex_unlock(&manager_lock);
        return RC_ERR_BUSY;
    }
    pending = RC_DEMO_LINK_WIFI;
    wrong_attempts = 0;
    deadline = k_uptime_get() + RC_PAIR_TTL_MS;
    state.error = RC_OK;
    if (rc_demo_store_has_wifi_token()) {
        state.stage = RC_DEMO_WIFI_TOKEN;
        state.pin_visible = false;
    } else {
        state.stage = RC_DEMO_WIFI_ENROLL;
        /* Developer-only six-digit code, NOT a WPA2 password. */
        state.pin = sys_rand32_get() % 900000U + 100000U;
        state.pin_visible = true;
    }
    k_mutex_unlock(&manager_lock);
    return RC_OK;
}

rc_err_sts_t rc_demo_manager_wifi_pin(const char *pin, uint8_t new_token[16])
{
    if (pin == NULL || new_token == NULL) return RC_ERR_INVALID_ARG;
    k_mutex_lock(&manager_lock, K_FOREVER);
    refresh_locked();
    if (pending != RC_DEMO_LINK_WIFI || state.stage != RC_DEMO_WIFI_ENROLL) {
        rc_err_sts_t result = state.error == RC_ERR_TIMEOUT ? RC_ERR_TIMEOUT : RC_ERR_INVALID_STATE;
        k_mutex_unlock(&manager_lock);
        return result;
    }
    /* The receiver supplies a NUL-terminated, bounded line. */
    char expected[7];
    (void)snprintf(expected, sizeof(expected), "%06u", (unsigned)state.pin);
    uint8_t diff = 0;
    size_t len = strlen(pin);
    if (len != 6U) {
        diff = 1;
    } else {
        for (size_t i = 0; i < 6U; ++i) {
            diff |= (uint8_t)pin[i] ^ (uint8_t)expected[i];
            diff |= (uint8_t)(pin[i] < '0' || pin[i] > '9');
        }
    }
    if (diff != 0) {
        wrong_attempts++;
        state.error = RC_ERR_AUTH_FAILED;
        if (wrong_attempts >= 3U) fail_owned_locked(RC_DEMO_LINK_WIFI, RC_ERR_AUTH_FAILED);
        k_mutex_unlock(&manager_lock);
        return RC_ERR_AUTH_FAILED;
    }
    /* Store the development token BEFORE declaring authentication complete. */
    sys_rand_get(new_token, 16);
    rc_err_sts_t r = rc_demo_store_save_wifi_token(new_token);
    if (r == RC_OK) {
        state.wifi_registered = true;
        state.pin_visible = false;
        state.wifi_authenticated = true;
        update_active_locked();
        state.stage = RC_DEMO_ACTIVE;
        state.error = RC_OK;
        pending = RC_DEMO_LINK_NONE;
        deadline = 0;
    } else {
        fail_owned_locked(RC_DEMO_LINK_WIFI, r);
    }
    k_mutex_unlock(&manager_lock);
    return r;
}

rc_err_sts_t rc_demo_manager_wifi_token(const uint8_t token[16])
{
    if (token == NULL) return RC_ERR_INVALID_ARG;
    k_mutex_lock(&manager_lock, K_FOREVER);
    refresh_locked();
    if (pending != RC_DEMO_LINK_WIFI || state.stage != RC_DEMO_WIFI_TOKEN) {
        rc_err_sts_t result = state.error == RC_ERR_TIMEOUT ? RC_ERR_TIMEOUT : RC_ERR_INVALID_STATE;
        k_mutex_unlock(&manager_lock);
        return result;
    }
    if (!rc_demo_store_wifi_token_equal(token)) {
        wrong_attempts++;
        state.error = RC_ERR_AUTH_FAILED;
        if (wrong_attempts >= 3U) fail_owned_locked(RC_DEMO_LINK_WIFI, RC_ERR_AUTH_FAILED);
        k_mutex_unlock(&manager_lock);
        return RC_ERR_AUTH_FAILED;
    }
    state.wifi_authenticated = true;
    update_active_locked();
    state.stage = RC_DEMO_ACTIVE;
    state.pin_visible = false;
    state.error = RC_OK;
    pending = RC_DEMO_LINK_NONE;
    deadline = 0;
    k_mutex_unlock(&manager_lock);
    return RC_OK;
}

void rc_demo_manager_wifi_lost(void)
{
    k_mutex_lock(&manager_lock, K_FOREVER);
    state.wifi_tcp_connected = false;
    bool owned = state.wifi_authenticated || pending == RC_DEMO_LINK_WIFI;
    state.wifi_authenticated = false;
    if (pending == RC_DEMO_LINK_WIFI) pending = RC_DEMO_LINK_NONE;
    deadline = 0;
    update_active_locked();
    if (owned && state.stage != RC_DEMO_FAILED) {
        state.stage = pending == RC_DEMO_LINK_BLE ? RC_DEMO_BLE_PAIR :
                      state.active != RC_DEMO_LINK_NONE ? RC_DEMO_ACTIVE : RC_DEMO_WAIT;
        state.error = RC_OK;
        if (pending != RC_DEMO_LINK_BLE) state.pin_visible = false;
    }
    k_mutex_unlock(&manager_lock);
}

rc_err_sts_t rc_demo_manager_ble_peer(void)
{
    k_mutex_lock(&manager_lock, K_FOREVER);
    refresh_locked();
    if (state.ble_authenticated || pending != RC_DEMO_LINK_NONE) {
        k_mutex_unlock(&manager_lock);
        return RC_ERR_BUSY;
    }
    pending = RC_DEMO_LINK_BLE;
    state.stage = RC_DEMO_BLE_PAIR;
    state.pin_visible = false;
    state.ble_connected = true;
    state.error = RC_OK;
    k_mutex_unlock(&manager_lock);
    return RC_OK;
}

void rc_demo_manager_ble_passkey(uint32_t pin)
{
    k_mutex_lock(&manager_lock, K_FOREVER);
    if (pending == RC_DEMO_LINK_BLE) {
        state.pin = pin % 1000000U;
        state.pin_visible = true;
        state.stage = RC_DEMO_BLE_PAIR;
    }
    k_mutex_unlock(&manager_lock);
}

rc_err_sts_t rc_demo_manager_ble_authenticated(void)
{
    k_mutex_lock(&manager_lock, K_FOREVER);
    if (state.ble_authenticated) {
        k_mutex_unlock(&manager_lock);
        return RC_OK; /* A later security update for the same accepted connection. */
    }
    if (pending != RC_DEMO_LINK_BLE) {
        k_mutex_unlock(&manager_lock);
        return RC_ERR_BUSY;
    }
    state.ble_authenticated = true;
    update_active_locked();
    state.stage = RC_DEMO_ACTIVE;
    state.pin_visible = false;
    state.error = RC_OK;
    pending = RC_DEMO_LINK_NONE;
    k_mutex_unlock(&manager_lock);
    return RC_OK;
}

void rc_demo_manager_ble_lost(void)
{
    k_mutex_lock(&manager_lock, K_FOREVER);
    state.ble_connected = false;
    bool owned = state.ble_authenticated || pending == RC_DEMO_LINK_BLE;
    state.ble_authenticated = false;
    if (pending == RC_DEMO_LINK_BLE) pending = RC_DEMO_LINK_NONE;
    update_active_locked();
    if (owned && state.stage != RC_DEMO_FAILED) {
        state.stage = pending == RC_DEMO_LINK_WIFI ?
                      (state.wifi_registered ? RC_DEMO_WIFI_TOKEN : RC_DEMO_WIFI_ENROLL) :
                      state.active != RC_DEMO_LINK_NONE ? RC_DEMO_ACTIVE : RC_DEMO_WAIT;
        state.error = RC_OK;
        if (pending != RC_DEMO_LINK_WIFI) state.pin_visible = false;
    }
    k_mutex_unlock(&manager_lock);
}

bool rc_demo_manager_link_failure(rc_demo_link_t link, rc_err_sts_t reason)
{
    k_mutex_lock(&manager_lock, K_FOREVER);
    bool owns = link == RC_DEMO_LINK_WIFI ?
                (pending == link || state.wifi_authenticated) :
                link == RC_DEMO_LINK_BLE ?
                (pending == link || state.ble_authenticated) : false;
    if (owns) fail_owned_locked(link, reason);
    k_mutex_unlock(&manager_lock);
    return owns;
}

rc_demo_snapshot_t rc_demo_manager_get(void)
{
    k_mutex_lock(&manager_lock, K_FOREVER);
    refresh_locked();
    rc_demo_snapshot_t result = state;
    k_mutex_unlock(&manager_lock);
    return result;
}



#if RC_APP_SIM_MODE == 1

void rc_demo_manager_simulate_links(bool wifi, bool ble)
{
    k_mutex_lock(&manager_lock, K_FOREVER);

    pending = RC_DEMO_LINK_NONE;
    deadline = 0;
    wrong_attempts = 0;
    failed_until = 0;

    state.wifi_ready = true;
    state.ble_ready = true;

    state.wifi_radio_error = RC_OK;
    state.ble_radio_error = RC_OK;

    state.wifi_authenticated = wifi;
    state.ble_authenticated = ble;

    state.wifi_tcp_connected = wifi;
    state.wifi_stations = wifi ? 1U : 0U;
    state.ble_connected = ble;

    state.active = wifi ? RC_DEMO_LINK_WIFI :
                   ble ? RC_DEMO_LINK_BLE :
                   RC_DEMO_LINK_NONE;

    state.stage = (wifi || ble)
        ? RC_DEMO_ACTIVE
        : RC_DEMO_WAIT;

    state.pin_visible = false;
    state.error = RC_OK;

    k_mutex_unlock(&manager_lock);
}

#endif
