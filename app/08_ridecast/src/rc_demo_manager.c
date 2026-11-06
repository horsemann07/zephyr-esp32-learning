#include "rc_demo_manager.h"
#include "rc_demo_store.h"
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <string.h>
#include <stdio.h>

#define RC_PAIR_TTL_MS 60000
K_MUTEX_DEFINE(manager_lock);
static rc_demo_snapshot_t state;
static rc_demo_link_t pending;
static int64_t deadline;
static unsigned int wrong_attempts;
static int64_t failed_until;

void rc_demo_manager_init(bool wifi_registered)
{
    k_mutex_lock(&manager_lock, K_FOREVER);
    memset(&state, 0, sizeof(state));
    state.wifi_registered = wifi_registered;
    state.stage = RC_DEMO_WAIT;
    state.error = RC_OK;
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
    k_mutex_unlock(&manager_lock);
}

void rc_demo_manager_ble_radio(bool up)
{
    k_mutex_lock(&manager_lock, K_FOREVER);
    state.ble_ready = up;
    k_mutex_unlock(&manager_lock);
}

rc_err_sts_t rc_demo_manager_wifi_peer(void)
{
    k_mutex_lock(&manager_lock, K_FOREVER);
    if (state.active != RC_DEMO_LINK_NONE || pending != RC_DEMO_LINK_NONE) {
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
    if (pin == NULL || new_token == NULL) {
        return RC_ERR_INVALID_ARG;
    }
    k_mutex_lock(&manager_lock, K_FOREVER);
    if (pending != RC_DEMO_LINK_WIFI || state.stage != RC_DEMO_WIFI_ENROLL) {
        k_mutex_unlock(&manager_lock);
        return RC_ERR_INVALID_STATE;
    }
    if (k_uptime_get() > deadline || wrong_attempts >= 3) {
        state.stage = RC_DEMO_FAILED;
        failed_until = k_uptime_get() + 3000;
        state.error = RC_ERR_TIMEOUT;
        state.pin_visible = false;
        k_mutex_unlock(&manager_lock);
        return RC_ERR_TIMEOUT;
    }
    if (strlen(pin) != 6U) {
        wrong_attempts++;
        state.error = RC_ERR_AUTH_FAILED;
        if (wrong_attempts >= 3) {
            state.stage = RC_DEMO_FAILED;
            failed_until = k_uptime_get() + 3000;
            state.pin_visible = false;
        }
        k_mutex_unlock(&manager_lock);
        return RC_ERR_AUTH_FAILED;
    }
    char expected[7];
    (void)snprintf(expected, sizeof(expected), "%06u", (unsigned)state.pin);
    uint8_t diff = 0;
    for (size_t i = 0; i < 6; ++i) {
        diff |= (uint8_t)pin[i] ^ (uint8_t)expected[i];
        diff |= (uint8_t)(pin[i] < '0' || pin[i] > '9');
    }
    diff |= (uint8_t)(pin[6] != '\0');
    if (diff != 0) {
        wrong_attempts++;
        state.error = RC_ERR_AUTH_FAILED;
        if (wrong_attempts >= 3) {
            state.stage = RC_DEMO_FAILED;
            failed_until = k_uptime_get() + 3000;
            state.pin_visible = false;
        }
        k_mutex_unlock(&manager_lock);
        return RC_ERR_AUTH_FAILED;
    }
    /* Never persist the six-digit PIN. Store a random-looking dev token. */
    sys_rand_get(new_token, 16);
    rc_err_sts_t r = rc_demo_store_save_wifi_token(new_token);
    if (r == RC_OK) {
        state.wifi_registered = true;
        state.pin_visible = false;
        state.active = RC_DEMO_LINK_WIFI;
        state.stage = RC_DEMO_ACTIVE;
        state.error = RC_OK;
    } else {
        state.error = r;
        state.stage = RC_DEMO_FAILED;
        failed_until = k_uptime_get() + 3000;
        state.pin_visible = false;
    }
    pending = RC_DEMO_LINK_NONE;
    k_mutex_unlock(&manager_lock);
    return r;
}

rc_err_sts_t rc_demo_manager_wifi_token(const uint8_t token[16])
{
    if (token == NULL) return RC_ERR_INVALID_ARG;
    k_mutex_lock(&manager_lock, K_FOREVER);
    if (pending != RC_DEMO_LINK_WIFI || state.stage != RC_DEMO_WIFI_TOKEN) {
        k_mutex_unlock(&manager_lock);
        return RC_ERR_INVALID_STATE;
    }
    if (k_uptime_get() > deadline || wrong_attempts >= 3) {
        state.stage = RC_DEMO_FAILED;
        failed_until = k_uptime_get() + 3000;
        state.error = RC_ERR_TIMEOUT;
        k_mutex_unlock(&manager_lock);
        return RC_ERR_TIMEOUT;
    }
    if (!rc_demo_store_wifi_token_equal(token)) {
        wrong_attempts++;
        state.error = RC_ERR_AUTH_FAILED;
        if (wrong_attempts >= 3) {
            state.stage = RC_DEMO_FAILED;
            failed_until = k_uptime_get() + 3000;
        }
        k_mutex_unlock(&manager_lock);
        return RC_ERR_AUTH_FAILED;
    }
    state.active = RC_DEMO_LINK_WIFI;
    state.stage = RC_DEMO_ACTIVE;
    state.error = RC_OK;
    pending = RC_DEMO_LINK_NONE;
    k_mutex_unlock(&manager_lock);
    return RC_OK;
}

void rc_demo_manager_wifi_lost(void)
{
    k_mutex_lock(&manager_lock, K_FOREVER);
    bool owned = (state.active == RC_DEMO_LINK_WIFI ||
                  pending == RC_DEMO_LINK_WIFI);
    if (state.active == RC_DEMO_LINK_WIFI) state.active = RC_DEMO_LINK_NONE;
    if (pending == RC_DEMO_LINK_WIFI) pending = RC_DEMO_LINK_NONE;
    if (owned && state.active == RC_DEMO_LINK_NONE &&
        pending == RC_DEMO_LINK_NONE && state.stage != RC_DEMO_FAILED) {
        state.stage = RC_DEMO_WAIT;
        state.pin_visible = false;
    }
    k_mutex_unlock(&manager_lock);
}

rc_err_sts_t rc_demo_manager_ble_peer(void)
{
    k_mutex_lock(&manager_lock, K_FOREVER);
    if (state.active != RC_DEMO_LINK_NONE || pending != RC_DEMO_LINK_NONE) {
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
    if (pending != RC_DEMO_LINK_BLE) {
        k_mutex_unlock(&manager_lock);
        return RC_ERR_BUSY;
    }
    state.active = RC_DEMO_LINK_BLE;
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
    bool owned = (state.active == RC_DEMO_LINK_BLE ||
                  pending == RC_DEMO_LINK_BLE);
    if (state.active == RC_DEMO_LINK_BLE) state.active = RC_DEMO_LINK_NONE;
    if (pending == RC_DEMO_LINK_BLE) pending = RC_DEMO_LINK_NONE;
    if (owned && state.active == RC_DEMO_LINK_NONE &&
        pending == RC_DEMO_LINK_NONE && state.stage != RC_DEMO_FAILED) {
        state.stage = RC_DEMO_WAIT;
        state.pin_visible = false;
    }
    k_mutex_unlock(&manager_lock);
}

void rc_demo_manager_failure(rc_err_sts_t reason)
{
    k_mutex_lock(&manager_lock, K_FOREVER);
    state.stage = RC_DEMO_FAILED;
    state.error = reason;
    state.pin_visible = false;
    failed_until = k_uptime_get() + 3000;
    k_mutex_unlock(&manager_lock);
}

rc_demo_snapshot_t rc_demo_manager_get(void)
{
    k_mutex_lock(&manager_lock, K_FOREVER);
    if (state.stage == RC_DEMO_FAILED && pending == RC_DEMO_LINK_NONE &&
        state.active == RC_DEMO_LINK_NONE && k_uptime_get() >= failed_until) {
        state.stage = RC_DEMO_WAIT;
        state.error = RC_OK;
    }
    rc_demo_snapshot_t result = state;
    k_mutex_unlock(&manager_lock);
    return result;
}
