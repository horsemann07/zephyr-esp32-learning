# RideCast 08 — Connection lifecycle (corrected)

**Application rule:** At most one authorized application source is active, although
both radio stacks can be enabled. A successful Wi-Fi session has **no inactivity
expiry**; it stays active until the TCP client closes, sends `DISCONNECT`, the AP
station disappears, the AP goes down, or an actual socket/transport error occurs.

## Wi-Fi stages

```text
Boot -> AP starting -> AP ready / SSID broadcast
                              |
                       WPA2 station joins
                              |
                       TCP client connects
                              |
                 PIN_REQUIRED / TOKEN_REQUIRED
                              |
                60-second AUTH/TOKEN deadline
                   /                     \
              success                expired / 3 wrong
                 |                          |
        Authorized Wi-Fi             FAILED for ~3 sec
        NO idle timeout                     |
                 |                   TCP socket closed
          NAV / PING / STATUS               |
                 |                          |
        Socket close / DISCONNECT / AP loss
                 | 
          Clear NAV and owner
                 |
          WAIT -> AP broadcast animation
                 |
           TCP listener accepts again
```

Note: The AP **continues broadcasting throughout** an authenticated TCP session.
After disconnection, the display returns to the broadcast/wait screen; the AP
SSID does not need to be restarted. Association is distinct from TCP connection
and application authorization. `S:n` on the footer is the number of associated
Wi-Fi stations, not the number of authorized sessions.

## BLE behavior

BLE accepts the phone OS L4 authenticated pairing/bonding path. The manager
claims the logical slot only after successful authentication. Callbacks for
connections that are no longer the current accepted BLE peer are ignored.
Failure handling is transport-scoped, so a stale BLE failure cannot fail an
active Wi-Fi session, and a rejected BLE disconnection cannot clear its owner.

## Explicit policies

- Incomplete Wi-Fi PIN/token authentication: 60 seconds, maximum 3 wrong attempts.
- Authenticated Wi-Fi connection: **no inactivity timeout**.
- TCP send: bounded to 2 seconds using nonblocking sends.
- TCP receiver/policy check: polled every 500 milliseconds.
- Wi-Fi station loss: when the current client's association is observed and
  all stations depart, close the previous TCP session and release the owner.
- BLE connection: until native disconnection or authentication failure.
- Any failed state: error displayed briefly then WAIT, unless a new owner is
  already authorized.

## Security boundary

This is a **development prototype**. TCP port 5050 and UDP discovery are not
TLS/cryptographically application-authenticated. The saved 16-byte Wi-Fi token
is a development credential and first-time enrollment is local-OLED approved.
Do not send production navigation/identity/vehicle data until the protocol,
app identity/certificate provisioning, and encrypted transport are implemented.
