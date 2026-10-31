# Zephyr ESP32 Learning

A step-by-step **Zephyr RTOS 4.4** learning repository using the **Heltec WiFi LoRa 32 V2** (ESP32 + SX1276/SX1278 + 0.96-inch OLED).

The goal is to learn embedded development by **writing, building, flashing, and modifying one small application at a time**. Zephyr is kept as a **Git submodule**; our application source is outside the Zephyr source tree.

> **Learning rule:** We do not jump directly to a finished project. We first get one feature working, understand it, change it, and then proceed to the next feature.

## 1. Repository layout

```text
zephyr-esp32-learning/
├── .gitmodules                # Zephyr submodule reference
├── .west/                     # West workspace metadata (generated)
├── zephyr/                    # Zephyr RTOS v4.4.0 (Git submodule)
├── modules/                   # West-managed dependencies (generated)
├── bootloader/                # West-managed dependencies, if fetched
├── tools/                     # West-managed tools, if fetched
├── .venv/                     # Python virtual environment (generated)
├── app/
│   └── 00_blinky/
│       ├── CMakeLists.txt
│       ├── prj.conf
│       ├── app.overlay
│       └── src/
│           └── main.c
├── build/                     # Build output (generated)
├── build.py                   # Reusable build / flash / monitor helper
└── README.md
```

**Important:** The `modules/`, `bootloader/`, and `tools/` directories are created by `west update` according to Zephyr's manifest. Downloading them does not mean every module is compiled into your firmware. Keep these generated dependencies out of this application's Git commits. The `zephyr/` directory is tracked as a Git submodule.

## 2. Hardware and tools

- **Board:** Heltec WiFi LoRa 32 V2 (ESP32).
- **Zephyr:** v4.4.0, pinned by the `zephyr/` Git submodule.
- **Host:** Windows PowerShell.
- **Python:** Python 3 with virtual-environment support.
- **Other tools:** Git, West, Zephyr SDK/toolchain, CMake/Ninja, and the required Espressif HAL dependencies.
- **Connection:** USB data cable and the board's CP2102 USB-to-UART interface.

**Default board target used by `build.py`:**

```text
heltec_wifi_lora32_v2/esp32/procpu
```

Always verify that the board target is available in your installed Zephyr checkout. OLED, LoRa, and LED support may require board-specific configuration.

## 3. Clone the repository

For a fresh clone, initialize the Zephyr submodule as well:

```powershell
git clone --recurse-submodules https://github.com/horsemann07/zephyr-esp32-learning.git
cd .\zephyr-esp32-learning
```

If the repository was cloned without submodules:

```powershell
git submodule update --init
```

> If you already have this repository locally, skip this section.

## 4. The `build.py` helper

Run all commands from the **repository root**, the directory containing `build.py`.

```powershell
python .\build.py --help
```

The helper uses a project-local Python environment at `.venv/` for build-related subprocesses. **Activating the environment is optional for normal build commands.**

### Supported command-line arguments

| Argument | What it does |
|---|---|
| `--setup` | Create `.venv/`, install West, PySerial and Zephyr Python requirements; initialize West if needed. |
| `--fetch-deps` | With `--setup`, also run `west update` to download dependencies. |
| `--fetch-blobs` | With `--setup`, fetch Espressif HAL binary blobs. |
| `-a`, `--activate` | Open a **new PowerShell session** with `.venv` activated. |
| `--configure` | Run Zephyr/CMake configuration without compiling the firmware. |
| `-b`, `--build` | Build the selected application for the selected board. |
| `-c`, `--clean` | Delete the selected application's build directory. |
| `-f`, `--flash` | Flash firmware from an existing build to the ESP32. |
| `-m`, `--monitor` | Open a USB serial console using PySerial. |
| `--ports` | List detected serial ports. |
| `-r`, `--run` | Run an existing QEMU build (QEMU targets only). |
| `--app NAME` | Select an application folder under `app/` (default: `00_blinky`). |
| `--board BOARD` | Select a Zephyr target (default: Heltec V2 PROCPU). |
| `--port COM5` | Select a serial port for flash or monitor. Replace `COM5` with yours. |
| `--baud 115200` | Set serial-monitor baud rate (default: `115200`). |
| `--pristine` | When used with configure/build, force a fresh CMake configuration. |

**Note:** `--setup`, `--configure`, `--build`, `--flash`, etc. are separate actions. Run one primary action per invocation. Options such as `--app`, `--board`, and `--pristine` modify the relevant action.

## 5. First-time setup

From the repository root:

```powershell
python .\build.py --setup --fetch-deps --fetch-blobs
```

This creates the local virtual environment, installs Python requirements, initializes the West workspace when necessary, updates Zephyr's external dependencies, and fetches Espressif blobs.

If you already ran `west update` and fetched the blobs, the shorter command is enough:

```powershell
python .\build.py --setup
```

**Optional:** Open another PowerShell session with `.venv` activated:

```powershell
python .\build.py -a
```

This opens a **new** shell. Python cannot activate the virtual environment in the PowerShell process that launched it.

To activate manually in the existing PowerShell window, use:

```powershell
.\.venv\Scripts\Activate.ps1
```

## 6. Build and flash `app/00_blinky`

### Step 1 — Configure

```powershell
python .\build.py --configure
```

This checks the application configuration, CMake setup, board configuration, and available toolchain **without compiling** the final firmware.

### Step 2 — Build

```powershell
python .\build.py -b
```

For a fresh configuration and build:

```powershell
python .\build.py -b --pristine
```

The build files are stored under a separate directory for the application and target, similar to:

```text
build/00_blinky/heltec_wifi_lora32_v2_esp32_procpu/
```

### Step 3 — Find the ESP32 serial port

Connect the board with a **USB data cable**, then run:

```powershell
python .\build.py --ports
```

Look for the CP210x USB-to-UART adapter, for example `COM5`. Your actual COM number may be different.

### Step 4 — Flash

After a successful build:

```powershell
python .\build.py -f --port COM5
```

If automatic port selection works on your PC, you can omit `--port`:

```powershell
python .\build.py -f
```

**Do not keep a serial monitor open during flashing.** Only one program should use the COM port at a time.

### Step 5 — Monitor output

```powershell
python .\build.py -m --port COM5
```

Exit the PySerial monitor with **Ctrl+]**. The default baud rate is 115200.

For the initial blinky application, observe the LED and any messages printed by `main.c`. The actual output depends on your application code and board console configuration.

### Step 6 — Clean

```powershell
python .\build.py -c
```

This deletes only the **selected app/board build output**, not the Zephyr sources, external modules, or other applications.

## 7. Reuse with future applications

Every lesson gets its own small application folder:

```text
app/
├── 00_blinky/
├── 01_uart/
├── 02_oled/
└── 03_lora/
```

For example, build `01_uart` using the same helper:

```powershell
python .\build.py -b --app 01_uart
```

Build a lesson for QEMU instead:

```powershell
python .\build.py -b --app 00_blinky --board qemu_x86
```

The command above requires that the selected application's hardware assumptions are valid for QEMU; an ESP32-specific LED overlay may not be compatible. For QEMU experiments, use an appropriate board-independent application or a separate board configuration.

To run an already-built QEMU application:

```powershell
python .\build.py -r --app SOME_QEMU_APP --board qemu_x86
```

## 8. How the build fits together

```text
Our source: app/00_blinky/src/main.c
                 |
                 v
        app/CMakeLists.txt + prj.conf + app.overlay
                 |
                 v
              build.py
                 |
                 v
              West build
                 |
                 v
      Zephyr (Git submodule) + HAL modules
                 |
                 v
          Firmware binary / ELF
                 |
                 v
        USB flash -> Heltec ESP32
                 |
                 v
       LED + USB serial output
```

Zephyr is **not** simply a prebuilt `.lib` dropped into the application. The Zephyr kernel, selected drivers, and our application are configured and built together, while the **source repositories remain separate**.

## 9. Troubleshooting

| Problem | What to check |
|---|---|
| `west is not installed in .venv` | Run `python .\build.py --setup`. |
| `West workspace missing` | Run `python .\build.py --setup`. |
| `Application missing: ... CMakeLists.txt` | Check `app/00_blinky/CMakeLists.txt` or specify the correct `--app`. |
| Missing Espressif modules | Run `python .\build.py --setup --fetch-deps`. |
| Missing ESP32 blobs | Run `python .\build.py --setup --fetch-blobs`. |
| `No configured build` during flash | Build first with `python .\build.py -b`. |
| Serial port not found | Check USB data cable, CP210x driver, Device Manager, and `--ports`. |
| Serial port is busy | Close PuTTY, VS Code serial monitor, or other terminal using that COM port. |
| Nothing printed | Verify board console configuration and correct COM port; press RESET after opening monitor. |
| LED does not blink | Verify the board revision, LED GPIO/polarity, overlay, and GPIO configuration. |

If a command fails, copy the **first meaningful error message** and the command you ran. We will debug that specific problem before adding more features.

## 10. Learning milestones

- [ ] Set up the project-local Python environment.
- [ ] Configure `00_blinky` without errors.
- [ ] Build Zephyr firmware successfully.
- [ ] Find the ESP32 COM port.
- [ ] Flash the Heltec V2.
- [ ] Observe LED behavior and/or serial output.
- [ ] Change the blink delay, predict the result, rebuild, and test.
- [ ] Explain what `prj.conf`, `app.overlay`, `CMakeLists.txt`, and `main.c` each do.

**Next:** After Blinky works and the code is understood, continue with UART, then OLED/I²C, SPI/LoRa, interrupts, and RTOS integration.

## 11. Git workflow

Commit only application code, documentation, tooling, and the Zephyr submodule reference. Ignore build products and West-downloaded dependencies.

A suitable root `.gitignore` is:

```gitignore
.west/
.venv/
/modules/
/bootloader/
/tools/
/build/
/build-*/
__pycache__/
*.pyc
```

Stage and push the README and helper:

```powershell
git add README.md build.py
git commit -m "docs: document Zephyr ESP32 build helper and learning workflow"
git push origin master
```

If your current branch is `main`, replace `master` with `main`.

---

**Reference:** [Zephyr documentation](https://docs.zephyrproject.org/latest/) · [Zephyr application development](https://docs.zephyrproject.org/latest/develop/application/index.html) · [West](https://docs.zephyrproject.org/latest/develop/west/index.html)
