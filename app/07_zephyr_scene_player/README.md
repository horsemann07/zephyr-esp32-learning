# Example 07 — OLED Scene Player (Heltec WiFi LoRa 32 V2)


## Features

Six procedural black-and-white animations drawn directly with Zephyr CFB:

1. Rocket launch
2. Car driving (scrolling road)
3. Rainstorm (falling rain)
4. Sunrise (rising sun)
5. Bouncing ball
6. Space adventure (spaceship, stars, asteroid)

No MP4 decoder, SD card or image conversion is required. The drawing thread runs
at approximately **5 frames per second** (200 ms frame interval) over I2C 100 kHz.
This is a frame animation player, not a real video decoder.

## Files

```
app/07_scene_player/
├── CMakeLists.txt
├── prj.conf
├── app.overlay
└── src/
    ├── main.c            # I2C recovery, OLED/CFB initialization, queue, threads
    ├── app_events.h      # Shared event and thread interfaces
    ├── input_thread.c    # Joystick ADC/buttons -> events
    ├── display_thread.c  # Help/browser/play/pause and frame scheduling
    ├── scene_draw.h      # Six scenes + category API
    └── scene_draw.c      # Procedural scene implementations
```

## Hardware wiring

| KY-023 pin | Heltec WiFi LoRa 32 V2 pin |
|-----------|---------------------------|
| VCC | **3V3** (never 5V with direct ADC wiring) |
| GND | GND |
| VRx | GPIO36 (ADC1 channel 0) |
| VRy | GPIO39 (ADC1 channel 3) |
| SW | GPIO13 (input with internal pull-up) |

Onboard PRG is GPIO0, active-low, no external wire needed. Avoid holding PRG
while resetting/powering on the board: GPIO0 is an ESP32 boot-strapping pin.
Onboard OLED uses GPIO4 SDA and GPIO15 SCL, already configured by the board.

## Controls

| Input | Behavior |
|-------|----------|
| Boot | Help / controls screen |
| PRG | Toggle help and scene browser; from playback returns to help |
| Joystick UP/DOWN | Previous/next category: SPACE, MOTION, NATURE |
| Joystick LEFT/RIGHT | Previous/next scene within category |
| Joystick SW in browser | Play selected scene |
| Joystick SW in playback | Pause / resume |

Scene category mapping: SPACE (rocket, space adventure), MOTION (car, bouncing
ball), NATURE (rainstorm, sunrise).

Joystick movement generates one event until returned to center; buttons use a
3-sample / 50 ms debounce. If left/right or up/down are reversed on your
physical joystick, flip `JOY_REVERSE_X` / `JOY_REVERSE_Y` in `input_thread.c`.
If direction detection is unreliable, print ADC readings temporarily and
calibrate `JOY_LOW`, `JOY_HIGH`, and center thresholds in that file.

## Build and flash using your project helper (from repo root)

```powershell
python .\build.py -b --app 07_scene_player --pristine -j 12
python .\build.py -f --app 07_scene_player --port COM13
python .\build.py -m --port COM13
```

Or with west (from a configured Zephyr workspace):

```powershell
west build -b heltec_wifi_lora32_v2/esp32/procpu app/07_scene_player -p always
west flash
```

## Design notes

- `main.c` owns the single `K_MSGQ_DEFINE` and creates both static threads.
- Input thread produces events; display thread owns the CFB, selected scene and playback state.
- `scene_draw()` **only draws into the existing framebuffer**. It never clears or finalizes.
- The display thread calls `cfb_framebuffer_clear()` before drawing and
  `cfb_framebuffer_finalize()` after each frame.
- The display thread waits for queue messages for at most 50ms or until the
  next frame deadline, so the UI remains responsive during playback.
- The example uses 2 threads, not a notification thread, because it is a
  self-contained scene player; the notification thread remains in Example 06.
- Analog joystick + OLED and all boards' wiring are physical hardware-dependent;
  verify actual wiring/pin labels before powering on.

This code has not been built on the user's Windows Zephyr SDK or tested on
physical hardware. Report any build output to resolve toolchain/board differences.
