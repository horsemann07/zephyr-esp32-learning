/* RideCast compile-time message routing policy.
 * These options control app data roles, not the Wi-Fi/BLE radio power state.
 * Change here and rebuild; no Kconfig or .conf simulator setting required.
 */
#ifndef RIDECAST_CONFIG_H_
#define RIDECAST_CONFIG_H_

#include <stdbool.h>
#include "rc_app_message.h"

#ifndef RC_WIFI_NAV_ENABLE
    #define RC_WIFI_NAV_ENABLE 1
#endif
#ifndef RC_WIFI_NOTIFY_ENABLE
    #define RC_WIFI_NOTIFY_ENABLE 0
#endif
#ifndef RC_WIFI_MEDIA_ENABLE
    #define RC_WIFI_MEDIA_ENABLE 0
#endif

#ifndef RC_BLE_NAV_ENABLE
    #define RC_BLE_NAV_ENABLE 0
#endif
#ifndef RC_BLE_NOTIFY_ENABLE
    #define RC_BLE_NOTIFY_ENABLE 1
#endif
#ifndef RC_BLE_MEDIA_ENABLE
    #define RC_BLE_MEDIA_ENABLE 1
#endif

/* If both transports hold fresh NAV updates, prefer Wi-Fi by default. */
#ifndef RC_NAV_PREFER_WIFI
    #define RC_NAV_PREFER_WIFI 1
#endif

#if (RC_WIFI_NAV_ENABLE != 0 && RC_WIFI_NAV_ENABLE != 1) ||   \
(RC_WIFI_NOTIFY_ENABLE != 0 && RC_WIFI_NOTIFY_ENABLE != 1) || \
(RC_WIFI_MEDIA_ENABLE != 0 && RC_WIFI_MEDIA_ENABLE != 1) ||   \
(RC_BLE_NAV_ENABLE != 0 && RC_BLE_NAV_ENABLE != 1) ||         \
(RC_BLE_NOTIFY_ENABLE != 0 && RC_BLE_NOTIFY_ENABLE != 1) ||   \
(RC_BLE_MEDIA_ENABLE != 0 && RC_BLE_MEDIA_ENABLE != 1) ||     \
(RC_NAV_PREFER_WIFI != 0 && RC_NAV_PREFER_WIFI != 1)
    #error "RideCast transport policy macros must be 0 or 1"
#endif

/* Central, transport-independent allowlist. This is not authentication;
 * ingress paths MUST check their current authenticated session first.
 */
static inline bool rc_transport_allows(rc_app_source_t source, rc_app_kind_t kind)
{
    switch(source)
    {
        case RC_APP_FROM_WIFI:
            switch(kind)
            {
                case RC_APP_NAV:
                    return RC_WIFI_NAV_ENABLE != 0;
                case RC_APP_NOTIFICATION:
                    return RC_WIFI_NOTIFY_ENABLE != 0;
                case RC_APP_MEDIA:
                    return RC_WIFI_MEDIA_ENABLE != 0;
                default:
                    return false;
            }
        case RC_APP_FROM_BLE:
            switch(kind)
            {
                case RC_APP_NAV:
                    return RC_BLE_NAV_ENABLE != 0;
                case RC_APP_NOTIFICATION:
                    return RC_BLE_NOTIFY_ENABLE != 0;
                case RC_APP_MEDIA:
                    return RC_BLE_MEDIA_ENABLE != 0;
                default:
                    return false;
            }
        default:
            return false;
    }
}

#endif /* RIDECAST_CONFIG_H_ */
