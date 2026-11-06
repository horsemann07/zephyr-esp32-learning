/* 08_ridecast: first dual-radio enrollment/connection demo (Zephyr + Heltec V2).
 * Wi-Fi: join trusted WPA2 AP, receive OTP / saved token on TCP :5050.
 * BLE: advertise, phone OS enters native pairing passkey, bond persists.
 * One active logical content session across the two radios.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "rc_demo_manager.h"
#include "rc_demo_store.h"
#include "heltec_graphic.h"
#ifdef CONFIG_RC_ENABLE_WIFI
    #include "rc_demo_wifi.h"
#endif
#ifdef CONFIG_RC_ENABLE_BLE
    #include "rc_demo_ble.h"
#endif

int main(void)
{
    rc_err_sts_t r = heltec_graphic_init();
    if(r != RC_OK)
    {
        printk("RideCast OLED init failed: %d\n", (int)r);
        return 0;
    }
    r = rc_demo_store_init();
    if(r != RC_OK)
    {
        printk("RideCast flash settings unavailable: %d\n", (int)r);
        /* Fail closed: show failure instead of starting unaudited pairing. */
        rc_demo_snapshot_t failed = { .stage = RC_DEMO_FAILED, .error = RC_ERR_STORAGE };
        while(true)
        {
            (void)heltec_graphic_render_connection(&failed);
            k_sleep(K_SECONDS(1));
        }
    }
    rc_demo_manager_init(rc_demo_store_has_wifi_token());
    printk("RideCast 08: starting radios (WIFI=%d BLE=%d)\n",
           IS_ENABLED(CONFIG_RC_ENABLE_WIFI), IS_ENABLED(CONFIG_RC_ENABLE_BLE));

#ifdef CONFIG_RC_ENABLE_BLE
    r = rc_demo_ble_start();
    if(r != RC_OK)
        printk("RideCast BLE start error: %d\n", (int)r);
#endif
#ifdef CONFIG_RC_ENABLE_WIFI
    r = rc_demo_wifi_start();
    if(r != RC_OK)
        printk("RideCast Wi-Fi start error: %d\n", (int)r);
#endif

    while(true)
    {
        rc_demo_snapshot_t now = rc_demo_manager_get();
        r                      = heltec_graphic_render_connection(&now);
        if(r != RC_OK)
            printk("RideCast OLED render failed: %d\n", (int)r);
        k_msleep(250);
    }
}
