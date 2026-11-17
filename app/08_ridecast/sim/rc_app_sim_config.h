
#ifndef RC_APP_SIM_CONFIG_H_
#define RC_APP_SIM_CONFIG_H_

/*
 * RideCast Application Simulator
 *
 * 0 - Normal firmware
 *     Real Wi-Fi + BLE
 *     No simulated app data
 *
 * 1 - Standalone simulator
 *     Fake Wi-Fi + BLE connections
 *     Simulated navigation, notification, music
 *
 * 2 - Live simulator
 *     Real Wi-Fi + BLE connections
 *     Inject sample app data after authentication
 */

#define RC_APP_SIM_MODE 1

#if RC_APP_SIM_MODE < 0 || RC_APP_SIM_MODE > 2
#error "Invalid RideCast simulator mode"
#endif

#endif
