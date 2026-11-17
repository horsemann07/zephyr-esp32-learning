# RideCast 08 — Direct Wi-Fi Access Point + navigation test

**Board:** Heltec WiFi LoRa 32 V2 (`heltec_wifi_lora32_v2/esp32/procpu`), Zephyr **4.4**.  
**Profile:** `prj_wifi.conf` initially (BLE disabled until Wi-Fi AP is confirmed on hardware).  
**Development build**, not production authentication. No router or hotspot is required.

## 1. What changes from the previous STA demo?

| Item | Previous STA | Current AP |
|---|---|---|
| Wi-Fi owner | Home router | **RideCast Heltec** |
| Who broadcasts SSID? | Router | **Heltec** |
| IP address on Heltec | Assigned by router | **192.168.4.1** |
| Phone receives address | Router DHCP | **Heltec DHCP (192.168.4.10+)** |
| TCP endpoint | Board IP:5050 | **192.168.4.1:5050** |
| App PIN | OLED PIN after TCP connect | **Unchanged** |
| BLE | Separate, optional | Separate, optional |

### Connection diagram

```text
     Heltec ESP32 / Zephyr / RideCast
          Wi-Fi SoftAP broadcasts
            SSID: RideCast-08
            WPA2 password: PRIVATE LOCAL CONFIG
              IP: 192.168.4.1
                     |
                Phone / Laptop
            1. Join RideCast-08 SSID
            2. TCP connect 192.168.4.1:5050
            3. Enter 6-digit PIN shown on OLED (first time)
            4. Send NAV direction/distance/speed over TCP
                     |
            Heltec OLED shows turn arrow
               distance, speed, GPS
```

**The 6-digit code is NOT the Wi-Fi password.** The phone first joins the WPA2 AP with the AP passphrase, then authenticates the RideCast application session with the OLED code. A client that already has a saved demo token reconnects with `TOKEN <32 hex chars>`. This is not a production-grade identity system; the TCP messages and saved token are not protected by TLS.

## 2. Files / roles

- `src/rc_connection_ap.h/.c`: starts AP, sets static IPv4, runs DHCP server, watches AP-station events.
- `src/rc_connection_tcp.h/.c`: same single-client TCP listener on port 5050; now gated by AP-ready state.
- `src/rc_demo_wifi.c`: existing 6-digit PIN + persistent token authorization, UDP discovery, `PING`, `STATUS`, `NAV` dispatch.
- `src/rc_demo_nav.h/.c`: parses bounded navigation test messages and stores the latest state.
- `src/main.c`: switches OLED between connection/enrollment and authorized navigation screens.
- `src/heltec_graphic.h/.c`: 128×64 OLED renderer (no horizontal divider lines).
- `src/rc_demo_ble.c`: existing optional BLE service; code included but not required for first AP build.
- `local.conf.example`: template for private AP SSID and WPA2 password.
- `docs/CONNECTION_LIFECYCLE.md`: exact owner/pairing/timeout/disconnect behavior.
- `docs/VALIDATION_PLAN.md`: regression/board tests and production gaps.
- `tools/wifi_app_test.py`: Wi-Fi AP test client (a Windows laptop on the RideCast network).

## 3. Configure AP password locally

From repository root:

```powershell
Copy-Item app/08_ridecast/local.conf.example app/08_ridecast/local.conf
notepad app/08_ridecast/local.conf
```

Set a **unique 8–63 character WPA2 development password**. Do not commit `local.conf` or paste it in chat. The template placeholder must be replaced before using the AP with others.

Example (replace the placeholder with your own secret):

```ini
CONFIG_RC_WIFI_AP_SSID="RideCast-08"
CONFIG_RC_WIFI_AP_PSK="YOUR_OWN_8_TO_63_CHAR_SECRET"
CONFIG_RC_WIFI_AP_CHANNEL=6
```

The `RC_WIFI_SSID` and `RC_WIFI_PSK` variables used by the previous STA firmware are **not used** by this AP build. There is intentionally **no built-in WPA2 passphrase** in Kconfig; create `local.conf` from the template before building Wi-Fi AP. `local.conf` is excluded from the shared ZIP and ignored by Git.

## 4. Build and flash — Wi-Fi AP only

In your Zephyr workspace root, with your existing virtual environment active:

```powershell
west build -b heltec_wifi_lora32_v2/esp32/procpu `
    app/08_ridecast `
    -d build/08_ridecast_ap `
    -p always -- `
    -DCONF_FILE=prj_wifi.conf `
    -DEXTRA_CONF_FILE=local.conf

west flash -d build/08_ridecast_ap
```

If the CMake build errors, share the **first** compile/Kconfig error (rather than changing random settings).

For an alternate initial test without the CLI, the existing `build.py` wrapper needs an option to pass `EXTRA_CONF_FILE=local.conf` and to select `prj_wifi.conf`. Use `west build` above until your wrapper supports these arguments.

**Zephyr support:** The AP configuration follows the official Zephyr 4.4 `samples/net/wifi/apsta_mode` AP path (`net_if_get_wifi_sap`, `NET_REQUEST_WIFI_AP_ENABLE`, `net_dhcpv4_server_start`). It has not been compiled/flashed here on your exact board.

## 5. Verify AP broadcast and joining

After boot, serial should report approximately:

```text
RideCast 08: starting radios (WIFI=1 BLE=0)
[RC AP] DHCP pool starts at 192.168.4.10
[RC AP] Starting SSID=RideCast-08, IP=192.168.4.1
[RC AP] ON: SSID=RideCast-08 IP=192.168.4.1
[RC AP] Phone Wi-Fi joined (not app-authorized)
[RC TCP] listening on port 5050
```

The exact log prefix for TCP can differ. **AP enabled does not mean app connected**.

The OLED shows `WIFI READY / 192.168.4.1 / JOIN RIDECAST` (with an AP-broadcast animation) then `WIFI PAIR` and a 6-digit code upon a new TCP connection. When a station has joined but not opened the app, the detail becomes `OPEN APP`. Footer `S:n` reports associated Wi-Fi stations only. If radio initialization fails, the footer shows `ER`; see serial logs for the specific error.

On your phone/Windows Wi-Fi settings, join **RideCast-08** with the AP passphrase. Some phones report *No Internet* or prefer cellular; select **Stay connected / Use this network anyway**. An Android app should bind its socket to the Wi-Fi `Network` for reliable routing when cellular is also enabled.

On Windows, check the Wi-Fi connection's assigned address. It should be in the `192.168.4.x` range, with RideCast at `192.168.4.1`.

## 6. Test TCP enrollment and live navigation

Keep the Windows laptop joined to `RideCast-08`. Run:

```powershell
python app/08_ridecast/tools/wifi_app_test.py 192.168.4.1
```

First connection: `PIN_REQUIRED`, OLED shows a 6-digit PIN, enter it in script. Script saves a development token and sends `PING` then a series of `NAV` updates.

A returning client uses the saved token instead of asking for a new PIN. The token is saved as `tools/ridecast_wifi_token.txt` on the PC and in Zephyr Settings/NVS on the Heltec.

**Temporary ASCII test contract** (only after authentication):

```text
PING                       -> PONG
STATUS                     -> OK WIFI AP ACTIVE
DISCONNECT                 -> OK BYE (then socket closes)
NAV RIGHT 250 35 1         -> OK NAV
NAV STRAIGHT 160 30 1      -> OK NAV
NAV LEFT 80 24 1           -> OK NAV
NAV ARRIVED 0 0 1          -> OK NAV
```

Fields are: turn (`LEFT`, `RIGHT`, `STRAIGHT`, `ARRIVED`), meters `0..999`, speed `0..240 km/h`, GPS valid `0/1`. No images, tiles or complete GPS route are transported in this first acceptance test. The screen returns to connection status about 8 seconds after the last update.

**These ASCII test messages are deliberately not a replacement for `04_RIDECAST_WIRE_PROTOCOL.md`.** Later, the router will replace the temporary parser with our binary frame codec and full versioned navigation payloads, including coordinates and route metadata. Map rendering remains a Host/cluster responsibility.

## 7. Wi-Fi versus BLE

For first bring-up, use Wi-Fi AP only. BLE code remains available, and the overall logical session manager allows only one active authorized content source. Combined Wi-Fi/BLE on this ESP32 Zephyr target has **not been verified**. After AP and NAV work, we can test the existing dual-radio `prj.conf` profile independently.

## 8. Security and troubleshooting

- Use test navigation data only. TCP :5050 is plaintext at the app layer; WPA2 alone is insufficient as a production application identity/security layer.
- Wi-Fi association (entering WPA2 key) does not authorize sending navigation. A successful RideCast application PIN/token check is still necessary.
- The 6-digit development PIN is not cluster-approved first-time enrollment. This Heltec-only prototype temporarily uses its local OLED as the approval UI. Production will restore the OEM cluster permission flow.
- Bluetooth and Wi-Fi identity binding is not solved by this demo. Do not combine Wi-Fi content from one phone with BLE content from a different phone.
- If the SSID never appears, check the `NET_REQUEST_WIFI_AP_ENABLE` result, Kconfig resolved values and ESP32 blobs.
- If Wi-Fi joins but no IP is assigned, inspect Zephyr DHCP server log and confirm `CONFIG_NET_DHCPV4_SERVER=y`.
- If the phone joins and TCP fails, verify its IPv4 and that `192.168.4.1:5050` is listening; check for mobile routing to cellular.
- If TCP connects but PIN fails, check OLED code within 60 seconds (up to 3 tries). Incomplete PIN/token authentication times out and the socket is closed automatically; authorized sessions have **no inactivity timeout**. If previously registered, use the original token client or intentionally clear the demo enrollment state as documented in code.
- An authorized client stays connected until its TCP socket closes, it sends `DISCONNECT`, its Wi-Fi station leaves, or the transport fails. The AP continues broadcasting; after TCP cleanup the OLED returns to its animated broadcast/wait state, and the listener accepts new clients.
- If your old `local.conf` contains `CONFIG_RC_WIFI_AP_PSK="TESTING"`, replace it: that password is only 7 characters, below WPA2's 8-character minimum. The shared archive excludes `local.conf`; use `local.conf.example` to create a private one.
- **No hardware run or Zephyr target build was performed here.** Host-only parser/manager tests cannot establish actual AP driver compatibility.

## 9. Definition of done

1. Heltec broadcasts the `RideCast-08` SSID with WPA2 enabled.
2. Phone/laptop joins without an external router.
3. DHCP assigns the client an address in `192.168.4.x`.
4. `192.168.4.1:5050` responds to authenticated `PING`.
5. A 6-digit PIN appears for a new app client; wrong PIN does not authorize it.
6. `NAV RIGHT 250 35 1` updates the Heltec OLED and responds `OK NAV`.
7. Disconnect clears the live session; stored dev token allows a later connection without a new PIN.
8. An authorized TCP session stays alive while idle; incomplete enrollment expires after 60 seconds.
9. An interrupted connection or explicit `DISCONNECT` returns to broadcast/wait and supports reconnecting.

Production follow-ups: create Android companion UI, authenticated/encrypted app transport, secure phone identity, approved registration windows, complete navigation schema, TI CC3351 AP-port validation.
