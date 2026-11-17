/* 08_ridecast: first dual-radio enrollment/connection demo (Zephyr + Heltec
 * V2). Wi-Fi: broadcast WPA2 RideCast AP, receive OTP/saved token on TCP :5050.
 * BLE: advertise, phone OS enters native pairing passkey, bond persists.
 * Two independent authorized content sessions can run together.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "rc_demo_manager.h"
#include "rc_demo_store.h"
#include "heltec_graphic.h"
#include "rc_demo_nav.h"
#include "rc_demo_content.h"
#include "rc_h7_bridge.h"
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
    rc_h7_bridge_init();
    printk("RideCast 08: starting radios (WIFI=%d BLE=%d)\n",
           IS_ENABLED(CONFIG_RC_ENABLE_WIFI), IS_ENABLED(CONFIG_RC_ENABLE_BLE));

#ifdef CONFIG_RC_ENABLE_BLE
    r = rc_demo_ble_start();
    if(r != RC_OK)
    {
        printk("RideCast BLE start error: %d\n", (int)r);
        rc_demo_manager_radio_failure(RC_DEMO_LINK_BLE, r);
    }
#endif
#ifdef CONFIG_RC_ENABLE_WIFI
    r = rc_demo_wifi_start();
    if(r != RC_OK)
    {
        printk("RideCast Wi-Fi start error: %d\n", (int)r);
        rc_demo_manager_radio_failure(RC_DEMO_LINK_WIFI, r);
    }
#endif

    while(true)
    {
        rc_h7_bridge_pump(); /* simulated ESP32 -> H7 -> OLED data path */
        rc_demo_snapshot_t now = rc_demo_manager_get();
        heltec_nav_screen_t nav;
        const bool nav_active = now.wifi_authenticated && rc_demo_nav_latest(&nav);
        rc_demo_content_t content;
        /* Never obscure a six-digit pairing code with incoming content. */
        if(now.pin_visible || now.stage == RC_DEMO_WIFI_TOKEN || now.stage == RC_DEMO_FAILED)
        {
            r = heltec_graphic_render_connection(&now);
        }
        else if(now.ble_authenticated && rc_demo_content_latest(&content, nav_active))
        {
            r = heltec_graphic_render_content(&content);
        }
        else if(nav_active)
        {
            r = heltec_graphic_render(&nav);
        }
        else
        {
            r = heltec_graphic_render_connection(&now);
        }
        if(RC_IS_FAILURE(r))
        {
            printk("RideCast OLED render failed: %d, %s\n", (int)r, rc_err_sts_to_str(r));
        }
        else if(r != RC_OK)
        {
            printk("RideCast OLED invalid positive status: %d\n", (int)r);
        }
        k_msleep(250);
    }
}
