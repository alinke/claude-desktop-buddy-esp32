# CYD port notes

This tree is the upstream [`anthropics/claude-desktop-buddy`](https://github.com/anthropics/claude-desktop-buddy)
firmware ported to the **ESP32 CYD** (ESP32-2432S028R "Cheap Yellow
Display", USB-C variant). The original target was the M5StickC Plus —
a very different board.

## Build & flash

```powershell
# from the repo root, with PlatformIO Core on PATH
pio run -e cyd -t upload          # firmware
pio run -e cyd -t uploadfs        # LittleFS (only needed for GIF character packs)
pio device monitor -e cyd         # serial @ 115200, with esp32 exception decoder
```

`pio` lives at `C:\Users\jperich\.platformio\penv\Scripts\pio.exe` on this
machine if it isn't on `PATH`.

## Hardware delta the port handles

| Subsystem    | M5StickC Plus              | CYD reality                              | Port strategy                                  |
| ------------ | -------------------------- | ---------------------------------------- | ---------------------------------------------- |
| Display      | 135×240, `M5.Lcd`          | 320×240 ILI9341 (portrait 240×320)       | `TFT_eSPI` via build flags; UI reworked        |
| Touch / btns | A / B / power buttons      | XPT2046 resistive, **no buttons**        | Tap-zone layer → virtual `BtnA`/`BtnB`/power   |
| IMU          | MPU6886 (shake, face-down) | none                                     | Stubbed — `getAccelData()` returns flat        |
| PMIC         | AXP192                     | none — direct LDO + USB                  | LEDC PWM on GPIO21 for backlight               |
| RTC          | BM8563 backed by AXP coin  | none                                     | Software clock (settimeofday + millis delta)   |
| Buzzer       | Internal beep              | Speaker on GPIO26 behind amp (GPIO4 LOW) | LEDC tone (`HalBeep`)                          |
| Status LED   | Active-low red LED GPIO10  | RGB LED — red leg = GPIO4 = **amp pin**  | Use **blue** leg (GPIO17) to avoid conflict    |
| BLE          | Bluedroid                  | Bluedroid (same)                         | Unchanged                                      |
| Storage      | LittleFS in `no_ota.csv`   | LittleFS, custom partition table         | `partitions.csv`: ~1.94 MB app + ~1.97 MB FS   |

## Layout

```
src/
  hal_m5.h / hal_m5.cpp    — CYD-backed M5 API shim (the heart of the port)
  main.cpp                  — upstream loop + state machine, with 240×320 layout
  buddy.cpp / buddies/      — upstream ASCII species (canvas widened to 240)
  character.cpp             — upstream GIF renderer (already TFT_eSPI-portable)
  ble_bridge.cpp / .h       — upstream Bluedroid NUS
  data.h xfer.h stats.h     — upstream, untouched
platformio.ini              — `[env:cyd]` is the real target; `m5stickc-plus`
                              kept commented-out for upstream reference
partitions.csv              — 4 MB layout (no OTA, room for the 1.8 MB chr pack)
PORT.md                     — you are here
```

The shim's design: every `#include <M5StickCPlus.h>` line in upstream was
swapped for `#include "hal_m5.h"`, which provides a global `M5` whose
`.Lcd`, `.BtnA`, `.BtnB`, `.Axp`, `.Imu`, `.Beep`, `.Rtc`, `.begin()` and
`.update()` look enough like the M5 API that upstream code compiles and
runs unmodified. The IMU and PMIC paths are stubbed to no-ops; behaviour
that depends on them (shake → dizzy, face-down → nap, landscape clock
rotation) simply never triggers.

## On-device controls

CYD has no physical A/B buttons, so the touchscreen is split into three
invisible zones:

| Touch zone                  | Acts as             |
| --------------------------- | ------------------- |
| Left ~62 % of the screen    | **A** (next / approve) |
| Right ~38 % strip           | **B** (page / scroll / deny) |
| Top-right corner (~46×46 px)| Power — short tap toggles backlight off |
| Long-press (≥600 ms) on the left | Opens the menu (= "Hold A") |

The wake-from-screen-off, "swallow first press" guard from upstream
carries over: any touch wakes the panel without also actioning.

The on-device **Info → Controls** page documents this for the user.

## Things to double-check on first boot

1. **Touch axis orientation.** Resistive panels vary unit-to-unit. If
   the zones feel mirrored or rotated, flip the macros at the top of
   `src/hal_m5.cpp`:
   ```cpp
   #define HAL_TOUCH_SWAP_XY   1
   #define HAL_TOUCH_INVERT_X  0
   #define HAL_TOUCH_INVERT_Y  1
   ```
   Set `HAL_TOUCH_DEBUG 1` in the same file to log raw + mapped points
   over serial while you tune.

2. **Sprite allocation.** 240×320 @ 16 bpp is ~150 KB; on plain ESP32
   with Bluedroid resident the free heap at allocation time is in the
   200–250 KB range. The setup path tries 16 bpp first and falls back
   to 8 bpp (RGB332-quantised, ~76 KB) if it fails — watch serial for
   `[main] 16bpp sprite alloc failed`.

3. **Battery readout.** Many CYD variants ship without the GPIO34
   battery-voltage divider populated; the Info → Device page will then
   show `0.00V battery` while everything else works. USB power is
   hard-coded as "5 V present" so the clock-when-idle behaviour stays
   enabled.

4. **Pairing.** The first BLE connection prompts the desktop for a
   6-digit passkey that the device displays large on screen. Type it on
   the desktop. The link is AES-CCM encrypted from then on.

## Known limitations / deliberate omissions

- **Shake → dizzy** and **face-down → nap** are inert (no IMU). The
  Pet "energy" stat still exists but only refills with passive idle time.
- **Landscape clock** is disabled (`clockOrient = 0` is forced); the
  setting still cycles auto/port/land but the value is a no-op.
- **"Turn off"** in the menu deep-sleeps the chip; pressing the touch
  IRQ (i.e. any touch) wakes it via reset. There's no PMIC to truly
  cut power.
- **Backlight only.** The "screen off" path drops the LEDC duty to 0
  and treats the panel as logically off; the panel itself remains
  powered (no LDO2 to gate).
- **Per-board calibration not persisted.** Touch axis flips live in
  source rather than NVS — a once-and-done tweak, not user-facing.
