# RideCast 08 — Connection lifecycle fixes

This is a revised source package from the supplied `08_ridecast (2).zip`.
No hardware run or Zephyr firmware build has been performed.

## Requested behavior preserved

- Authorized Wi-Fi sessions have **no inactivity timeout**.
- The app may close TCP or send `DISCONNECT` (server replies `OK BYE`).
- The AP SSID remains advertised while a client is connected.
- After TCP disconnect, the manager releases ownership, clears navigation,
  the OLED returns to the AP broadcast animation, and TCP accepts a new client.

## Corrected

- A Wi-Fi PIN/token authentication in progress expires automatically after 60s;
  a silent unauthenticated client cannot monopolize the manager forever.
- Three wrong codes release ownership, close the connection, and allow retries.
- Failed state resets after its short on-screen error period without waiting
  for another authentication message or another transport to disconnect.
- TCP reads and authorization policy use a 500ms socket poll; nonblocking TCP
  sends have a 2s deadline, so a slow reader cannot block sends indefinitely.
- TCP listener stays bound through successive clients, reducing reconnect
  failures from rebinding port 5050 after every disconnect.
- BLE failure and disconnect callbacks are scoped to the accepted BLE peer;
  stale callbacks do not overwrite another radio's authorization state.
- AP station count and physical TCP presence are distinct from app authorization.
  Disconnecting the last observed AP station closes the stale socket promptly.
- Radio startup errors are tracked per radio and reported on OLED (`ER`).
- The leaked `local.conf` has been removed from the shared archive;
  `local.conf.example` is now included. The original `TESTING` PSK was only
  7 characters and invalid for WPA2. Kconfig no longer supplies a hardcoded PSK.
- Host manager tests now cover silent handshake expiry, 3 failures, long-lived
  authorized sessions, late callback isolation, session teardown and reconnect.

## Verification completed

- `gcc -Wall -Wextra -Werror -std=c11` manager tests: **PASS**.
- `gcc -Wall -Wextra -Werror -std=c11` nav parser tests: **PASS**.

## Unverified / requires next design step

- Zephyr target build/ESP32 flashing and real RF coexistence behavior.
- TCP socket offload driver support for nonblocking `zsock_send` and `zsock_poll`
  (these are Zephyr socket APIs, but test on the actual selected driver).
- TLS/application-layer encryption, cryptographic phone identity, secure token
  handling/rotation and production protocol. Do not use for real sensitive data.

Read `docs/CONNECTION_LIFECYCLE.md` and `docs/VALIDATION_PLAN.md` before testing.
