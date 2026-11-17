# RideCast 08 — Verification plan and remaining production work

## Host tests (source level)

From `app/08_ridecast`, where GCC/Clang is available:

```sh
cc -Wall -Wextra -Werror -std=c11 -Isrc -Itests/stubs \
  tests/test_manager.c src/rc_demo_manager.c -o test_manager
./test_manager
cc -Wall -Wextra -Werror -std=c11 -Isrc -Itests/stubs \
  tests/test_nav.c src/rc_demo_nav.c -o test_nav
./test_nav
```

## Flash/device acceptance checklist

1. Fix/replace `local.conf` with a unique WPA2 PSK of **8–63 characters**.
2. Build `prj_wifi.conf` and `prj_ble.conf` independently before `prj.conf`.
3. Ensure AP is visible, phone joins, DHCP assigns `192.168.4.x`, OLED displays
   `WIFI READY` and broadcast pulse; `S:1` reflects association.
4. Connect TCP :5050, verify `PIN_REQUIRED` or `TOKEN_REQUIRED`, and authorize.
5. Keep a **fully authorized** TCP socket idle for >60 seconds: verify it remains
   open and can still issue `PING` and navigation messages.
6. Connect TCP, do **not authenticate** for >60 seconds: verify forced disconnect,
   owner released, and a subsequent new client can connect.
7. Fail authentication 3 times: verify client is removed and another client can
   connect; BLE can pair afterward.
8. After successful Wi-Fi auth, send `DISCONNECT` and verify `OK BYE`, TCP closes,
   OLED returns to broadcast/wait, and reconnect with saved token succeeds.
9. Close the phone's socket unexpectedly, or switch off Wi-Fi: verify NAV clears,
   the manager releases Wi-Fi, OLED returns to broadcast, and new clients work.
10. Try BLE while Wi-Fi authorized and Wi-Fi while BLE authorized: second logical
    session must be rejected without altering the first.
11. Cancel BLE pairing, fail passkey, then try Wi-Fi: BLE callbacks must not
    overwrite a Wi-Fi owner or prevent subsequent pairing.
12. Simulate an unresponsive TCP reader during status/navigation replies: send
    must not block indefinitely; the TCP worker should recover after timeout.
13. Disable radio startup in config and verify error is shown on OLED footer
    (`ER`) and boot logs, without overwriting the other radio's active session.
14. Finally test both radios enabled (`prj.conf`) for RF coexistence and memory
    pressure on the real Heltec ESP32 board.

## Not completed in this patch

- An actual Zephyr build/flash and radio coexistence test on the ESP32 board.
- TLS or another encrypted/authenticated production phone-to-device protocol.
- Production phone identity, certificate/credential rotation, and secure storage.
- Navigation over BLE or an integrated mobile application.

These require the target SDK/hardware and a defined production wire protocol.
