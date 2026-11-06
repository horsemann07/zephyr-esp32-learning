/* ============================================================================
 * File        : rc_err_sts.c
 * Project     : RideCast
 * Description : Symbolic error-name lookup generated from rc_err_sts.h.
 * ========================================================================== */
#include "rc_err_sts.h"

const char *rc_err_sts_to_str(rc_err_sts_t status)
{
    switch (status) {
#define RC_ERR_STS_CASE(name, value) case name: return #name;
        RC_ERR_STS_LIST(RC_ERR_STS_CASE)
#undef RC_ERR_STS_CASE
    default:
        return "RC_ERR_UNRECOGNIZED";
    }
}
