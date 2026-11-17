/* Developer-only single-client registration. Production must use a secure
 * credential store and a mutually authenticated encrypted transport.
 */
#include "rc_demo_store.h"
#include <zephyr/settings/settings.h>
#include <errno.h>
#include <string.h>

static uint8_t saved[RC_WIFI_TOKEN_BYTES];
static bool have_saved;

static int token_set(const char *name, size_t len,
                     settings_read_cb read_cb, void *cb_arg)
{
    if (strcmp(name, "wifi_token") != 0) {
        return -ENOENT;
    }
    if (len != sizeof(saved)) {
        return -EINVAL;
    }
    int n = read_cb(cb_arg, saved, sizeof(saved));
    if (n == (int)sizeof(saved)) {
        have_saved = true;
        return 0;
    }
    memset(saved, 0, sizeof(saved));
    have_saved = false;
    return (n < 0) ? n : -EIO;
}

SETTINGS_STATIC_HANDLER_DEFINE(rc_demo_store, "ridecast", NULL,
                               token_set, NULL, NULL);

rc_err_sts_t rc_demo_store_init(void)
{
    int err = settings_subsys_init();
    if (err < 0) {
        return RC_ERR_STORAGE;
    }
    err = settings_load_subtree("ridecast");
    return err == 0 ? RC_OK : RC_ERR_STORAGE;
}

bool rc_demo_store_has_wifi_token(void)
{
    return have_saved;
}

bool rc_demo_store_wifi_token_equal(const uint8_t token[RC_WIFI_TOKEN_BYTES])
{
    if (!have_saved || token == NULL) {
        return false;
    }
    uint8_t difference = 0U;
    for (size_t i = 0; i < sizeof(saved); i++) {
        difference |= saved[i] ^ token[i];
    }
    return difference == 0U;
}

rc_err_sts_t rc_demo_store_save_wifi_token(const uint8_t token[RC_WIFI_TOKEN_BYTES])
{
    if (token == NULL) {
        return RC_ERR_INVALID_ARG;
    }
    /* Commit to flash BEFORE treating an enrollment as successful. */
    int err = settings_save_one("ridecast/wifi_token", token, RC_WIFI_TOKEN_BYTES);
    if (err < 0) {
        return RC_ERR_STORAGE;
    }
    memcpy(saved, token, sizeof(saved));
    have_saved = true;
    return RC_OK;
}
