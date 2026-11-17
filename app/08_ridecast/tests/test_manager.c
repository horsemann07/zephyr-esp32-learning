#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rc_demo_manager.h"
#include "rc_demo_store.h"
int64_t fake_uptime;
static bool have;
static uint8_t saved[16];
rc_err_sts_t rc_demo_store_init(void) { return RC_OK; }
bool rc_demo_store_has_wifi_token(void) { return have; }
bool rc_demo_store_wifi_token_equal(const uint8_t t[16]) {
    return have && memcmp(t,saved,16)==0;
}
rc_err_sts_t rc_demo_store_save_wifi_token(const uint8_t t[16]) {
    memcpy(saved,t,16); have=true; return RC_OK;
}
int main(void) {
    rc_demo_manager_init(false);
    rc_demo_manager_wifi_radio(true);
    assert(rc_demo_manager_wifi_peer()==RC_OK);
    assert(rc_demo_manager_ble_peer()==RC_ERR_BUSY);
    assert(rc_demo_manager_get().pin == 223456U);
    uint8_t token[16]={0};
    assert(rc_demo_manager_wifi_pin("12345",token)==RC_ERR_AUTH_FAILED);
    assert(rc_demo_manager_wifi_pin("000000",token)==RC_ERR_AUTH_FAILED);
    assert(rc_demo_manager_wifi_pin("223456",token)==RC_OK);
    assert(have);
    assert(rc_demo_manager_get().active == RC_DEMO_LINK_WIFI);
    assert(rc_demo_manager_ble_peer()==RC_ERR_BUSY);
    rc_demo_manager_wifi_lost();
    assert(rc_demo_manager_get().active == RC_DEMO_LINK_NONE);
    assert(rc_demo_manager_wifi_peer()==RC_OK);
    assert(rc_demo_manager_get().stage == RC_DEMO_WIFI_TOKEN);
    assert(rc_demo_manager_wifi_token(token)==RC_OK);
    rc_demo_manager_wifi_lost();
    assert(rc_demo_manager_ble_peer()==RC_OK);
    rc_demo_manager_ble_passkey(987654U);
    assert(rc_demo_manager_get().pin==987654U);
    assert(rc_demo_manager_wifi_peer()==RC_ERR_BUSY);
    assert(rc_demo_manager_ble_authenticated()==RC_OK);
    rc_demo_manager_ble_lost();
    assert(rc_demo_manager_get().stage==RC_DEMO_WAIT);
    /* A rejected Wi-Fi peer must not erase an in-progress BLE pairing. */
    rc_demo_manager_init(false);
    assert(rc_demo_manager_ble_peer()==RC_OK);
    rc_demo_manager_wifi_lost();
    assert(rc_demo_manager_get().stage==RC_DEMO_BLE_PAIR);
    rc_demo_manager_ble_lost();
    /* And a rejected BLE connection must not erase a Wi-Fi pairing. */
    assert(rc_demo_manager_wifi_peer()==RC_OK);
    rc_demo_manager_ble_lost();
    assert(rc_demo_manager_get().stage==RC_DEMO_WIFI_TOKEN);
    rc_demo_manager_wifi_lost();
    rc_demo_manager_init(false);
    have=false;
    fake_uptime=0;
    assert(rc_demo_manager_wifi_peer()==RC_OK);
    fake_uptime=61000;
    assert(rc_demo_manager_wifi_pin("223456",token)==RC_ERR_TIMEOUT);
    /* Expired pending Wi-Fi owner must be released without further packets. */
    assert(rc_demo_manager_ble_peer() == RC_OK);
    assert(rc_demo_manager_get().stage == RC_DEMO_BLE_PAIR);
    rc_demo_manager_ble_lost();

    /* A silent, unauthenticated client expires automatically. */
    fake_uptime = 0;
    have = false;
    rc_demo_manager_init(false);
    assert(rc_demo_manager_wifi_peer() == RC_OK);
    fake_uptime = 60000;
    assert(rc_demo_manager_get().stage == RC_DEMO_FAILED);
    assert(rc_demo_manager_get().error == RC_ERR_TIMEOUT);
    rc_demo_manager_wifi_lost();
    fake_uptime = 63001;
    assert(rc_demo_manager_get().stage == RC_DEMO_WAIT);
    assert(rc_demo_manager_ble_peer() == RC_OK);
    rc_demo_manager_ble_lost();

    /* Three wrong codes end enrollment and release the owner. */
    fake_uptime = 0;
    rc_demo_manager_init(false);
    assert(rc_demo_manager_wifi_peer() == RC_OK);
    assert(rc_demo_manager_wifi_pin("000000", token) == RC_ERR_AUTH_FAILED);
    assert(rc_demo_manager_wifi_pin("000000", token) == RC_ERR_AUTH_FAILED);
    assert(rc_demo_manager_wifi_pin("000000", token) == RC_ERR_AUTH_FAILED);
    assert(rc_demo_manager_get().stage == RC_DEMO_FAILED);
    assert(rc_demo_manager_ble_peer() == RC_OK);
    rc_demo_manager_ble_lost();

    /* Authenticated sessions live indefinitely until explicit disconnect. */
    fake_uptime = 0;
    rc_demo_manager_init(false);
    assert(rc_demo_manager_wifi_peer() == RC_OK);
    assert(rc_demo_manager_wifi_pin("223456", token) == RC_OK);
    fake_uptime = 100000000;
    assert(rc_demo_manager_get().stage == RC_DEMO_ACTIVE);
    assert(rc_demo_manager_get().active == RC_DEMO_LINK_WIFI);
    assert(rc_demo_manager_ble_peer() == RC_ERR_BUSY);
    /* A delayed BLE callback must not mark the Wi-Fi session failed. */
    assert(!rc_demo_manager_link_failure(RC_DEMO_LINK_BLE, RC_ERR_AUTH_FAILED));
    assert(rc_demo_manager_get().active == RC_DEMO_LINK_WIFI);
    rc_demo_manager_radio_failure(RC_DEMO_LINK_BLE, RC_ERR_RADIO);
    assert(rc_demo_manager_get().active == RC_DEMO_LINK_WIFI);
    assert(rc_demo_manager_get().ble_radio_error == RC_ERR_RADIO);
    rc_demo_manager_wifi_station_count(1);
    rc_demo_manager_wifi_tcp(true);
    assert(rc_demo_manager_get().wifi_stations == 1);
    assert(rc_demo_manager_get().wifi_tcp_connected);
    rc_demo_manager_wifi_lost();
    assert(rc_demo_manager_get().stage == RC_DEMO_WAIT);
    assert(!rc_demo_manager_get().wifi_tcp_connected);
    /* With no TCP owner the user may reconnect, even if AP station remains. */
    assert(rc_demo_manager_wifi_peer() == RC_OK);
    rc_demo_manager_wifi_lost();

    /* Once a BLE owner fails, a Wi-Fi attempt may acquire the free slot. */
    rc_demo_manager_init(false);
    assert(rc_demo_manager_ble_peer() == RC_OK);
    assert(rc_demo_manager_link_failure(RC_DEMO_LINK_BLE, RC_ERR_AUTH_FAILED));
    assert(rc_demo_manager_wifi_peer() == RC_OK);
    rc_demo_manager_wifi_lost();

    puts("PASS: connection state, lifecycle, timeout, isolation tests");
    return 0;
}
