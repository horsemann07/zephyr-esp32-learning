# 08_ridecast — Heltec V2 Wi-Fi + BLE connection experiment

**Status: development prototype / UNVERIFIED on the physical board.** Targets Zephyr 4.4 and `heltec_wifi_lora32_v2/esp32/procpu`.

This project is self-contained: copy the **whole `08_ridecast` folder** into the Zephyr ESP32 learning repository under `app/`. This stage tests connectivity and authorization only. Navigation, RideCast production wire protocol, host routing, and CC3351 ports are later milestones.

## 1. What starts at boot

- SSD1306 OLED 128 × 64, with no horizontal separator lines.
- Wi-Fi **station** (STA) connects to your configured **2.4 GHz WPA2 router/hotspot**. This is *not* a RideCast Wi-Fi hotspot, and the six-digit OLED code is **not** the router's Wi-Fi password.
- TCP listener on port **5050** accepts a client on the same trusted LAN.
- UDP announcement `RIDECAST08,AUTH_REQUIRED,TCP=5050` is broadcast periodically on port **39801**, without a PIN or private data. Broadcast may be blocked by the router or Windows firewall.
- BLE advertises **`RideCast-08`** and exposes a custom encrypted/authenticated GATT PING/PONG characteristic.
- **Both radios are attempted independently**. A Wi-Fi failure does not prevent BLE initialization and vice versa. **Only one *authorized application session* is active.** The radio hardware may remain enabled on both paths.

**BLE/Wi-Fi coexistence on this specific Zephyr ESP32 build has NOT been verified.** The ESP32 SoC supports coexistence, but Zephyr's driver, memory use and RF sharing still need bench validation. Try the Wi-Fi-only and BLE-only profiles if the dual build fails.

## 2. Screen layout (physical OLED, not a screenshot)

No borders/lines are drawn by firmware. Four independent CFB text rows stay within x=4..111 and y=14..57:

```text
RC
WIFI PAIR
PIN: 583204
ENTER IN APP
W:ON B:ON
```

When connecting over BLE, the display changes to `BLE PAIR` and a *native BLE stack-generated* `PIN: 123456` (actual digits vary). After successful authorization:

```text
RC
CONN OK
WIFI
AUTH SAVED
W:ON B:ON
```

On failure, the display uses `CONN FAIL`. `W:ON` means Wi-Fi associated with router, not the app authenticated; `B:ON` means BLE advertising initialized, not phone authorized. The PIN stays only while enrollment/pairing is pending; **never** save or broadcast the six digits.

## 3. Test Wi-Fi enrollment (first time)

1. Put your SSID and WPA2 passphrase in **`local.conf`** (copy `local.conf.example`). Keep it out of Git.
2. Flash the board; wait until Wi-Fi connects and an IP is assigned by DHCP. Check the router/serial monitor; optionally run `python app/08_ridecast/tools/find_ridecast.py` on your PC to discover the IP.
3. Run `python app/08_ridecast/tools/wifi_app_test.py 192.168.x.y`.
4. ESP32 receives the TCP connection, generates a **six-digit app enrollment PIN** and displays `WIFI PAIR / PIN: NNNNNN`.
5. Enter this PIN in the Windows client. The app receives `OK TOKEN <32 hex digits>` on successful registration. ESP32 persists this 128-bit demo token in Zephyr Settings/NVS; the Python client saves a local copy, excluded from Git.
6. The OLED displays `CONN OK / WIFI`. A `PING` returns `PONG`.
7. Disconnect/reconnect. On the next connection, the client sends the saved token, and **no new PIN appears**.
8. A wrong PIN fails; after three bad attempts or a 60-second timeout, restart the TCP connection. When a client is already authorized, another radio cannot become active without disconnecting it.

The Wi-Fi token is one-device, one-client demo state. **It is NOT secure authentication:** TCP is unencrypted, the token can be intercepted or replayed, tokens are stored in plaintext flash, and generating tokens with `sys_rand_get()` is not guaranteed cryptographic. **Use only a trusted isolated lab network and no personal/location data.** Production needs TLS/mutual authentication, secure storage, identity-bound enrollment and stronger replay protection. Don't interpret this demonstration as production Wi-Fi security.

## 4. Test BLE enrollment (first time)

1. From Android, open a BLE explorer such as **nRF Connect**. Scan for `RideCast-08` and connect.
2. The peripheral requests BLE security level 4. The phone OS should initiate pairing; the ESP32's **Bluetooth security stack chooses a six-digit passkey** and the OLED displays `BLE PAIR / PIN: NNNNNN` when a passkey is required.
3. Type the code into the phone's **native Bluetooth pairing dialog**, not into an arbitrary plaintext GATT characteristic.
4. Once authenticated, the display shows `CONN OK / BLE`. Zephyr `CONFIG_BT_SETTINGS` persists the Bluetooth bond in the Settings NVS backend.
5. Reconnect the same bonded phone: stored Bluetooth keys can be reused without displaying a fresh PIN.
6. Optional GATT test: service UUID `4ef13480-7330-49ce-9f17-524343303830`, characteristic UUID `4ef13481-7330-49ce-9f17-524343303830`. After authenticated pairing, **write ASCII `PING`**, then **read `PONG`**. Not all BLE explorer apps initiate OS pairing automatically, so trigger a characteristic read/write if needed.

The app explicitly requires `BT_SECURITY_L4` before activating a BLE session. If your phone/controller cannot negotiate level 4, the app rejects the link; inspect the serial log rather than relaxing authentication silently. The ESP32 hardware is Bluetooth 4.2 capable, but security and coexistence must be checked on your actual Zephyr build.

## 5. Build and flash (PowerShell)

From `zephyr-esp32-learning` root (using your existing Zephyr venv and SDK):

```powershell
Copy-Item app/08_ridecast/local.conf.example app/08_ridecast/local.conf
# Edit local.conf with your own Wi-Fi network credentials.
west build -b heltec_wifi_lora32_v2/esp32/procpu app/08_ridecast -d build/08_ridecast -p always -- -DEXTRA_CONF_FILE=local.conf
west flash -d build/08_ridecast
```

If the dual-radio configuration fails or exceeds memory, isolate the radio first:

**Wi-Fi only**
```powershell
west build -b heltec_wifi_lora32_v2/esp32/procpu app/08_ridecast -d build/08_ridecast_wifi -p always -- -DCONF_FILE=prj_wifi.conf -DEXTRA_CONF_FILE=local.conf
west flash -d build/08_ridecast_wifi
```

**BLE only**
```powershell
west build -b heltec_wifi_lora32_v2/esp32/procpu app/08_ridecast -d build/08_ridecast_ble -p always -- -DCONF_FILE=prj_ble.conf
west flash -d build/08_ridecast_ble
```

> `CMakeLists.txt` sets the SSD1306 shield, so you don't need another `--shield` argument.
>
> The app uses flash Settings/NVS. The selected board must have a valid `storage_partition`. Check `build/08_ridecast/zephyr/zephyr.dts` before altering partitions. Do not copy a flash address from another board or erase production settings blindly.

## 6. APIs / files

| File | Purpose |
| --- | --- |
| `src/main.c` | Boot sequence, independent radio startup, UI thread |
| `src/heltec_graphic.h/.c` | OLED drawing and compact pairing/status screen (keeps previous navigation drawing API) |
| `src/rc_demo_manager.h/.c` | One active connection; pending radio, PIN, timeout, errors |
| `src/rc_demo_store.h/.c` | Zephyr NVS-backed Wi-Fi token; BLE bonds handled by Zephyr |
| `src/rc_connection_wifi.h/.c` | Existing transport-neutral Zephyr Wi-Fi connection manager |
| `src/rc_connection_tcp.h/.c` | Existing single-client Zephyr TCP listener |
| `src/rc_demo_wifi.h/.c` | Wi-Fi enrollment/test-line protocol and public discovery beacon |
| `src/rc_demo_ble.h/.c` | Zephyr BLE advertising, pairing callback, authenticated GATT PING/PONG |
| `src/rc_err_sts.h` | Portable common error enums (`RC_OK`=0, negative errors) |
| `tools/wifi_app_test.py` | Simple interactive Windows Wi-Fi client |
| `tools/find_ridecast.py` | Listen for Wi-Fi UDP discovery announcements |

## 7. Important implementation boundaries / TODO

- This prototype starts enrollment upon connection without a cluster approval action. **Production RideCast still requires explicit cluster authorization for new devices.** This is a constrained development exception, not a policy change.
- The **Wi-Fi WLAN SSID/passphrase** is a development setting, separate from the OLED **app enrollment code**. Future RideCast Wi-Fi AP provisioning has its own design.
- One active **logical** content session, not one hardware link. There is no automatic BLE-to-Wi-Fi handover; disconnect first.
- No real phone companion app, no location data, no notifications, no navigation routing and no shared Wi-Fi/BLE cryptographic phone identity yet.
- The Wi-Fi TCP test is deliberately not the finalized `04_RIDECAST_WIRE_PROTOCOL.md` framing. Integrate the common wire codec after the connection tests pass.
- BLE bonds and the Wi-Fi demo token can exist side-by-side. **We cannot yet prove that a saved Wi-Fi token and a BLE bond belong to the same physical phone.** Do not treat them as an interchangeable production identity.
- For a factory-reset/re-enroll experiment, deliberate Settings storage erase or an explicit authorized reset command will be needed; the app currently doesn't offer a reset option.
- Simultaneous Wi-Fi and BLE on the same board, settings backend, screen layout and phone pairing require real hardware testing. This code has not been validated in a physical Zephyr build here.

## 8. Acceptance checklist

- [ ] Display starts; no cut or crossed text
- [ ] Wi-Fi-only build connects to 2.4 GHz WPA2 router
- [ ] TCP client gets `PIN_REQUIRED` and OLED shows six digits
- [ ] Wrong PIN rejected, correct PIN saved across reboot
- [ ] Returning Wi-Fi client sends token; no new PIN
- [ ] BLE-only build advertises and shows native pairing passkey
- [ ] BLE bond survives reboot and GATT `PING`/`PONG` works
- [ ] Dual Wi-Fi + BLE build initializes both radios
- [ ] If Wi-Fi session active, BLE session rejected (and vice versa)
- [ ] Disconnection restores `WAIT APP` while leaving bond/token saved
- [ ] Later: navigation data reaches OLED

## References

- Zephyr board: https://docs.zephyrproject.org/latest/boards/heltec/heltec_wifi_lora32_v2/doc/index.html
- Zephyr BLE security: https://docs.zephyrproject.org/latest/services/connectivity/bluetooth/bluetooth-le-host.html
- Zephyr Settings: https://docs.zephyrproject.org/latest/services/storage/settings/index.html
