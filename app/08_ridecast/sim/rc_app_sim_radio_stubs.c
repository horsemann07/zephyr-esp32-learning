/* This file is ONLY built with RC_APP_SIM_MODE=1 in rc_app_sim_config.h.
 * It replaces actual radio start functions without touching main.c.
 * It NEVER starts a physical Wi-Fi AP, BLE advertiser, TCP or GATT server.
 */
#include "rc_demo_manager.h"
#include "rc_demo_wifi.h"
#include "rc_demo_ble.h"
#include <zephyr/sys/printk.h>

#if defined(CONFIG_RC_ENABLE_WIFI)
rc_err_sts_t rc_demo_wifi_start(void)
{
    rc_demo_manager_wifi_radio(true);
    printk("[RC SIM] Wi-Fi radio is VIRTUAL (no real AP)\n");
    return RC_OK;
}
#endif

#if defined(CONFIG_RC_ENABLE_BLE)
rc_err_sts_t rc_demo_ble_start(void)
{
    rc_demo_manager_ble_radio(true);
    printk("[RC SIM] BLE radio is VIRTUAL (no real advertising)\n");
    return RC_OK;
}
#endif
