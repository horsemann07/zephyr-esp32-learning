#ifndef RC_DEMO_STORE_H_
#define RC_DEMO_STORE_H_
#include <stdbool.h>
#include <stdint.h>
#include "rc_err_sts.h"
#define RC_WIFI_TOKEN_BYTES 16U
rc_err_sts_t rc_demo_store_init(void);
bool rc_demo_store_has_wifi_token(void);
bool rc_demo_store_wifi_token_equal(const uint8_t token[RC_WIFI_TOKEN_BYTES]);
rc_err_sts_t rc_demo_store_save_wifi_token(const uint8_t token[RC_WIFI_TOKEN_BYTES]);
#endif
