/* ============================================================================
 * File        : rc_err_sts.h
 * Project     : RideCast
 * Description : Single source of truth for shared API error/status codes.
 * Standard    : ISO C11; no RTOS, HAL, or heap dependency.
 *
 * Contract:
 *   RC_OK = 0              Success
 *   RC_ERR_* < 0           Failure (including expected/recoverable errors)
 *   Positive integers     NOT used for rc_err_sts_t.
 *
 * Existing error values -1 through -29 are retained for source compatibility.
 * Do not renumber published values. Append only new unused negative values.
 * These are LOCAL API results, not automatically network/wire error codes.
 * Do not log authentication secrets when reporting statuses.
 *
 * This X-macro master list drives both enum generation and string conversion.
 * ========================================================================== */
#ifndef RC_ERR_STS_H_
#define RC_ERR_STS_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Stable existing status values: do not renumber. */
#define RC_ERR_STS_BASE_LIST(X) \
    X(RC_OK                                  ,    0) \
    X(RC_ERR_INVALID_ARG                     ,   -1) \
    X(RC_ERR_NO_SPACE                        ,   -2) \
    X(RC_ERR_INVALID_FRAME                   ,   -3) \
    X(RC_ERR_UNSUPPORTED_VERSION             ,   -4) \
    X(RC_ERR_CRC_MISMATCH                    ,   -5) \
    X(RC_ERR_INVALID_TLV                     ,   -6) \
    X(RC_ERR_OVERFLOW                        ,   -7) \
    X(RC_ERR_INVALID_UTF8                    ,   -8) \
    X(RC_ERR_NOT_FOUND                       ,   -9) \
    X(RC_ERR_DUPLICATE                       ,  -10) \
    X(RC_ERR_TIMEOUT                         ,  -11) \
    X(RC_ERR_BUSY                            ,  -12) \
    X(RC_ERR_NOT_READY                       ,  -13) \
    X(RC_ERR_NOT_SUPPORTED                   ,  -14) \
    X(RC_ERR_IO                              ,  -15) \
    X(RC_ERR_DISCONNECTED                    ,  -16) \
    X(RC_ERR_AUTH_FAILED                     ,  -17) \
    X(RC_ERR_PERMISSION_DENIED               ,  -18) \
    X(RC_ERR_REPLAY_DETECTED                 ,  -19) \
    X(RC_ERR_INVALID_STATE                   ,  -20) \
    X(RC_ERR_NO_MEMORY                       ,  -21) \
    X(RC_ERR_CANCELED                        ,  -22) \
    X(RC_ERR_STORAGE                         ,  -23) \
    X(RC_ERR_STORAGE_CORRUPT                 ,  -24) \
    X(RC_ERR_RADIO                           ,  -25) \
    X(RC_ERR_NOT_REGISTERED                  ,  -26) \
    X(RC_ERR_ALREADY_EXISTS                  ,  -27) \
    X(RC_ERR_RETRY_LIMIT                     ,  -28) \
    X(RC_ERR_INTERNAL                        ,  -29)

/* Additional general errors (-30 to -49). */
#define RC_ERR_STS_COMMON_LIST(X) \
    X(RC_ERR_FAIL                            ,  -30) \
    X(RC_ERR_UNKNOWN                         ,  -31) \
    X(RC_ERR_INVALID_SIZE                    ,  -32) \
    X(RC_ERR_INVALID_LENGTH                  ,  -33) \
    X(RC_ERR_NOT_EXIST                       ,  -34) \
    X(RC_ERR_ALREADY_INITIALIZED             ,  -35) \
    X(RC_ERR_NOT_INITIALIZED                 ,  -36) \
    X(RC_ERR_INIT_FAILED                     ,  -37) \
    X(RC_ERR_DEINIT_FAILED                   ,  -38) \
    X(RC_ERR_CONFIG_FAILED                   ,  -39) \
    X(RC_ERR_OPERATION_FAILED                ,  -40) \
    X(RC_ERR_OPERATION_ABORTED               ,  -41) \
    X(RC_ERR_RESOURCE_UNAVAILABLE            ,  -42) \
    X(RC_ERR_RESOURCE_EXHAUSTED              ,  -43) \
    X(RC_ERR_BUFFER_UNDERRUN                 ,  -44) \
    X(RC_ERR_BUFFER_EMPTY                    ,  -45) \
    X(RC_ERR_BUFFER_FULL                     ,  -46) \
    X(RC_ERR_OUT_OF_BOUNDS                   ,  -47) \
    X(RC_ERR_INVALID_HANDLE                  ,  -48) \
    X(RC_ERR_INVALID_ID                      ,  -49)

/* Transport and wire-protocol errors (-50 to -79). */
#define RC_ERR_STS_COMM_LIST(X) \
    X(RC_ERR_TX_FAILED                       ,  -50) \
    X(RC_ERR_RX_FAILED                       ,  -51) \
    X(RC_ERR_TX_TIMEOUT                      ,  -52) \
    X(RC_ERR_RX_TIMEOUT                      ,  -53) \
    X(RC_ERR_TX_BUSY                         ,  -54) \
    X(RC_ERR_RX_BUSY                         ,  -55) \
    X(RC_ERR_NO_DATA                         ,  -56) \
    X(RC_ERR_NO_RESPONSE                     ,  -57) \
    X(RC_ERR_FRAME_ERROR                     ,  -58) \
    X(RC_ERR_PROTOCOL_ERROR                  ,  -59) \
    X(RC_ERR_CHECKSUM_MISMATCH               ,  -60) \
    X(RC_ERR_ACK_FAILED                      ,  -61) \
    X(RC_ERR_BUS_ERROR                       ,  -62) \
    X(RC_ERR_LINK_ERROR                      ,  -63) \
    X(RC_ERR_CONNECTION_FAILED               ,  -64) \
    X(RC_ERR_DATA_OVERFLOW                   ,  -65) \
    X(RC_ERR_DATA_UNDERRUN                   ,  -66) \
    X(RC_ERR_SYNC_FAILED                     ,  -67) \
    X(RC_ERR_TRANSPORT_NOT_OPEN              ,  -68) \
    X(RC_ERR_TRANSPORT_NOT_CONFIGURED        ,  -69) \
    X(RC_ERR_PARTIAL_WRITE                   ,  -70) \
    X(RC_ERR_FRAMING_DESYNC                  ,  -71) \
    X(RC_ERR_INVALID_MESSAGE_ID              ,  -72) \
    X(RC_ERR_INVALID_MESSAGE_KIND            ,  -73) \
    X(RC_ERR_INVALID_FLAGS                   ,  -74) \
    X(RC_ERR_INVALID_SEQUENCE                ,  -75) \
    X(RC_ERR_UNSUPPORTED_ENCODING            ,  -76) \
    X(RC_ERR_MESSAGE_TOO_LARGE               ,  -77) \
    X(RC_ERR_STREAM_RESYNC_FAILED            ,  -78) \
    X(RC_ERR_FLOW_CONTROL                    ,  -79)

/* Network, Wi-Fi, BLE and channel errors (-80 to -109). */
#define RC_ERR_STS_NETWORK_LIST(X) \
    X(RC_ERR_ALREADY_CONNECTED               ,  -80) \
    X(RC_ERR_ALREADY_DISCONNECTED            ,  -81) \
    X(RC_ERR_CONNECTION_LOST                 ,  -82) \
    X(RC_ERR_CONNECTION_TIMEOUT              ,  -83) \
    X(RC_ERR_PEER_UNAVAILABLE                ,  -84) \
    X(RC_ERR_NETWORK_UNAVAILABLE             ,  -85) \
    X(RC_ERR_NOT_CONNECTED                   ,  -86) \
    X(RC_ERR_CHANNEL_BUSY                    ,  -87) \
    X(RC_ERR_LINK_NOT_READY                  ,  -88) \
    X(RC_ERR_LINK_UNSTABLE                   ,  -89) \
    X(RC_ERR_DISCOVERY_FAILED                ,  -90) \
    X(RC_ERR_HANDSHAKE_FAILED                ,  -91) \
    X(RC_ERR_ADDRESS_INVALID                 ,  -92) \
    X(RC_ERR_ENDPOINT_UNAVAILABLE            ,  -93) \
    X(RC_ERR_WIFI_UNAVAILABLE                ,  -94) \
    X(RC_ERR_BLE_UNAVAILABLE                 ,  -95) \
    X(RC_ERR_RADIO_INIT_FAILED               ,  -96) \
    X(RC_ERR_RADIO_START_FAILED              ,  -97) \
    X(RC_ERR_RADIO_STOP_FAILED               ,  -98) \
    X(RC_ERR_RADIO_DISABLED                  ,  -99) \
    X(RC_ERR_NETWORK_CONFIG_INVALID          , -100) \
    X(RC_ERR_NETWORK_AUTH_REQUIRED           , -101) \
    X(RC_ERR_PHONE_IDENTITY_MISMATCH         , -102) \
    X(RC_ERR_WIFI_BLE_BINDING_FAILED         , -103) \
    X(RC_ERR_BLE_BOND_MISSING                , -104) \
    X(RC_ERR_PHONE_NOT_DISCOVERABLE          , -105) \
    X(RC_ERR_ROUTE_UNAVAILABLE               , -106) \
    X(RC_ERR_NOT_SUBSCRIBED                  , -107) \
    X(RC_ERR_CHANNEL_LOST                    , -108) \
    X(RC_ERR_PHONE_APP_UNAVAILABLE           , -109)

/* Persistent storage and settings errors (-110 to -139). */
#define RC_ERR_STS_STORAGE_LIST(X) \
    X(RC_ERR_STORAGE_FULL                    , -110) \
    X(RC_ERR_STORAGE_NOT_READY               , -111) \
    X(RC_ERR_STORAGE_BUSY                    , -112) \
    X(RC_ERR_READ_FAILED                     , -113) \
    X(RC_ERR_WRITE_FAILED                    , -114) \
    X(RC_ERR_DELETE_FAILED                   , -115) \
    X(RC_ERR_OPEN_FAILED                     , -116) \
    X(RC_ERR_CLOSE_FAILED                    , -117) \
    X(RC_ERR_CREATE_FAILED                   , -118) \
    X(RC_ERR_READ_ONLY                       , -119) \
    X(RC_ERR_END_OF_DATA                     , -120) \
    X(RC_ERR_STORAGE_INIT_FAILED             , -121) \
    X(RC_ERR_STORAGE_MOUNT_FAILED            , -122) \
    X(RC_ERR_STORAGE_FORMAT_FAILED           , -123) \
    X(RC_ERR_STORAGE_RESET_FAILED            , -124) \
    X(RC_ERR_DATA_NOT_FOUND                  , -125) \
    X(RC_ERR_DATA_NOT_AVAILABLE              , -126) \
    X(RC_ERR_DATA_EXPIRED                    , -127) \
    X(RC_ERR_DATA_LOCKED                     , -128) \
    X(RC_ERR_NVM_COMMIT_FAILED               , -129) \
    X(RC_ERR_NVM_ROLLBACK_FAILED             , -130) \
    X(RC_ERR_SETTINGS_INVALID                , -131) \
    X(RC_ERR_SETTINGS_CONFLICT               , -132) \
    X(RC_ERR_SETTINGS_VERSION_MISMATCH       , -133) \
    X(RC_ERR_SETTINGS_RECOVERY_FAILED        , -134) \
    X(RC_ERR_PERSISTENCE_INCOMPLETE          , -135) \
    X(RC_ERR_CREDENTIAL_STORE_FAILED         , -136) \
    X(RC_ERR_CREDENTIAL_LOAD_FAILED          , -137) \
    X(RC_ERR_CREDENTIAL_DELETE_FAILED        , -138) \
    X(RC_ERR_PERSISTENT_ID_MISSING           , -139)

/* Authentication, pairing and cryptography errors (-140 to -169). */
#define RC_ERR_STS_SECURITY_LIST(X) \
    X(RC_ERR_KEY_INVALID                     , -140) \
    X(RC_ERR_KEY_EXPIRED                     , -141) \
    X(RC_ERR_AUTH_TAG_FAILED                 , -142) \
    X(RC_ERR_ENCRYPT_FAILED                  , -143) \
    X(RC_ERR_DECRYPT_FAILED                  , -144) \
    X(RC_ERR_HASH_INVALID                    , -145) \
    X(RC_ERR_SIGNATURE_INVALID               , -146) \
    X(RC_ERR_CERT_INVALID                    , -147) \
    X(RC_ERR_SECURITY_VIOLATION              , -148) \
    X(RC_ERR_SECURITY_LOCKED                 , -149) \
    X(RC_ERR_SECURE_BOOT_FAILED              , -150) \
    X(RC_ERR_AUTHORIZATION_FAILED            , -151) \
    X(RC_ERR_SECURITY_NOT_READY              , -152) \
    X(RC_ERR_SECURITY_NOT_SUPPORTED          , -153) \
    X(RC_ERR_PAIRING_FAILED                  , -154) \
    X(RC_ERR_PAIRING_TIMEOUT                 , -155) \
    X(RC_ERR_PAIR_CODE_INVALID               , -156) \
    X(RC_ERR_PAIR_CODE_EXPIRED               , -157) \
    X(RC_ERR_PAIRING_WINDOW_CLOSED           , -158) \
    X(RC_ERR_BONDING_FAILED                  , -159) \
    X(RC_ERR_BOND_REVOKED                    , -160) \
    X(RC_ERR_SESSION_EXPIRED                 , -161) \
    X(RC_ERR_SESSION_NOT_AUTHENTICATED       , -162) \
    X(RC_ERR_SESSION_ID_MISMATCH             , -163) \
    X(RC_ERR_PERMISSION_REVOKED              , -164) \
    X(RC_ERR_SECURE_STORAGE_UNAVAILABLE      , -165) \
    X(RC_ERR_REPLAY_WINDOW_EXCEEDED          , -166) \
    X(RC_ERR_IDENTITY_NOT_TRUSTED            , -167) \
    X(RC_ERR_HOST_NOT_AUTHENTICATED          , -168) \
    X(RC_ERR_SECURITY_POLICY_DENIED          , -169)

/* RideCast product policy and session errors (-170 to -199). */
#define RC_ERR_STS_RIDECAST_LIST(X) \
    X(RC_ERR_HOST_NOT_SYNCED                 , -170) \
    X(RC_ERR_HOST_UNAVAILABLE                , -171) \
    X(RC_ERR_POLICY_NOT_APPLIED              , -172) \
    X(RC_ERR_OEM_POLICY_LOCKED               , -173) \
    X(RC_ERR_NO_REGISTERED_PHONES            , -174) \
    X(RC_ERR_LAST_PHONE_NOT_SET              , -175) \
    X(RC_ERR_LAST_PHONE_UNAVAILABLE          , -176) \
    X(RC_ERR_PHONE_NOT_ACTIVE                , -177) \
    X(RC_ERR_PHONE_NOT_SELECTED              , -178) \
    X(RC_ERR_PHONE_ALREADY_ACTIVE            , -179) \
    X(RC_ERR_DEVICE_LIMIT_REACHED            , -180) \
    X(RC_ERR_ENROLLMENT_NOT_ALLOWED          , -181) \
    X(RC_ERR_ENROLLMENT_INCOMPLETE           , -182) \
    X(RC_ERR_ENROLLMENT_CANDIDATE_MISSING    , -183) \
    X(RC_ERR_ENROLLMENT_CANDIDATE_REJECTED   , -184) \
    X(RC_ERR_ENROLLMENT_COMMIT_FAILED        , -185) \
    X(RC_ERR_ACTIVE_PHONE_SWITCH_FAILED      , -186) \
    X(RC_ERR_WIRELESS_SESSION_UNAVAILABLE    , -187) \
    X(RC_ERR_CONTENT_NOT_ALLOWED             , -188) \
    X(RC_ERR_CONTENT_CHANNEL_MISMATCH        , -189) \
    X(RC_ERR_CHANNEL_BINDING_MISSING         , -190) \
    X(RC_ERR_CHANNEL_BINDING_MISMATCH        , -191) \
    X(RC_ERR_FEATURE_DISABLED                , -192) \
    X(RC_ERR_FEATURE_UNAVAILABLE             , -193) \
    X(RC_ERR_SHUTDOWN_IN_PROGRESS            , -194) \
    X(RC_ERR_RECONNECT_TIMEOUT               , -195) \
    X(RC_ERR_RECONNECT_DENIED                , -196) \
    X(RC_ERR_REGISTERED_PHONE_REVOKED        , -197) \
    X(RC_ERR_FACTORY_RESET_DENIED            , -198) \
    X(RC_ERR_FIRMWARE_INCOMPATIBLE           , -199)

/* System and platform port errors (-200 to -218). */
#define RC_ERR_STS_SYSTEM_LIST(X) \
    X(RC_ERR_WATCHDOG_FAILURE                , -200) \
    X(RC_ERR_THREAD_CREATE_FAILED            , -201) \
    X(RC_ERR_QUEUE_FULL                      , -202) \
    X(RC_ERR_QUEUE_EMPTY                     , -203) \
    X(RC_ERR_MUTEX_FAILED                    , -204) \
    X(RC_ERR_RESOURCE_LOCKED                 , -205) \
    X(RC_ERR_BOOT_FAILED                     , -206) \
    X(RC_ERR_FIRMWARE_UPDATE_FAILED          , -207) \
    X(RC_ERR_FIRMWARE_ROLLBACK_FAILED        , -208) \
    X(RC_ERR_POWER_STATE_INVALID             , -209) \
    X(RC_ERR_POWER_TRANSITION_FAILED         , -210) \
    X(RC_ERR_LOW_POWER_FAILED                , -211) \
    X(RC_ERR_CLOCK_FAILURE                   , -212) \
    X(RC_ERR_HARDWARE_FAULT                  , -213) \
    X(RC_ERR_DEVICE_NOT_PRESENT              , -214) \
    X(RC_ERR_DEVICE_INIT_FAILED              , -215) \
    X(RC_ERR_DMA_FAILED                      , -216) \
    X(RC_ERR_INTERRUPT_CONFIG_FAILED         , -217) \
    X(RC_ERR_MEMORY_TEST_FAILED              , -218)

/* Unrecoverable internal error (-255). */
#define RC_ERR_STS_FATAL_LIST(X) \
    X(RC_ERR_FATAL                           , -255)

/**
 * @brief Master X-macro status list. Use X(name, numeric_value).
 *
 * Example:
 *   #define EMIT_ENUM(name, value) name = value,
 *   enum { RC_ERR_STS_LIST(EMIT_ENUM) };
 *   #undef EMIT_ENUM
 */
#define RC_ERR_STS_LIST(X) \
    RC_ERR_STS_BASE_LIST(X) \
    RC_ERR_STS_COMMON_LIST(X) \
    RC_ERR_STS_COMM_LIST(X) \
    RC_ERR_STS_NETWORK_LIST(X) \
    RC_ERR_STS_STORAGE_LIST(X) \
    RC_ERR_STS_SECURITY_LIST(X) \
    RC_ERR_STS_RIDECAST_LIST(X) \
    RC_ERR_STS_SYSTEM_LIST(X) \
    RC_ERR_STS_FATAL_LIST(X)

#define RC_ERR_STS_ENUM_ENTRY(name, value) name = value,
typedef enum rc_err_sts {
    RC_ERR_STS_LIST(RC_ERR_STS_ENUM_ENTRY)
} rc_err_sts_t;
#undef RC_ERR_STS_ENUM_ENTRY

/* Number of defined statuses, NOT an error code or numeric upper bound. */
#define RC_ERR_STS_COUNT_ONE(name, value) + 1
enum { RC_ERR_STS_COUNT = 0 RC_ERR_STS_LIST(RC_ERR_STS_COUNT_ONE) };
#undef RC_ERR_STS_COUNT_ONE

#define RC_IS_SUCCESS(status) ((status) == RC_OK)
#define RC_IS_FAILURE(status) ((status) < RC_OK)

/**
 * @brief Symbolic status name, such as "RC_ERR_CRC_MISMATCH".
 * @returns "RC_ERR_UNRECOGNIZED" for unknown numeric values.
 * @note Does not allocate memory; returned static string is not to be freed.
 */
const char *rc_err_sts_to_str(rc_err_sts_t status);

#ifdef __cplusplus
}
#endif

#endif /* RC_ERR_STS_H_ */
