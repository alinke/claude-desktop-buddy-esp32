#pragma once
// ============================================================
// board.h — per-board identity and capabilities.
//
// Each PlatformIO env sets exactly one BUDDY_BOARD_<X> define and puts that
// board's board_configs/<dir>/ on the include path, which supplies the
// LovyanGFX `LGFX` class (panel, bus, backlight and touch wiring). This
// header adds what LovyanGFX doesn't describe: the name shown on screen,
// the default rotation, the speaker / LED / battery pins, the touch type,
// and whether the board has the PSRAM + RAM headroom for the WiFi
// "Ask Claude" feature.
//
// Defaults (overridden per board below):
//   BUDDY_ROTATION        0     LovyanGFX setRotation() value used at boot
//                               when the user hasn't picked one in settings
//   BUDDY_TOUCH_RESISTIVE 0     1 = XPT2046, needs 4-corner calibration
//   BUDDY_WAKE_PIN        -1    active-low touch IRQ that can wake deep sleep
//   BUDDY_SPK_PIN         -1    LEDC tone output (speaker amp input)
//   BUDDY_AMP_EN_PIN      -1    amp enable, active low
//   BUDDY_LED_PIN         -1    attention LED, active low
//   BUDDY_BAT_ADC_PIN     -1    VBAT through a 2:1 divider
//   BUDDY_RGB_PANEL       0     RGB-parallel panel scanned out of PSRAM —
//                               frames are pushed through the patched
//                               double buffer (see canvas.cpp)
//   BUDDY_ROUND_PANEL     0     circular glass — UI keeps to the inscribed circle
//   BUDDY_ASK_CLAUDE      0     compile in the WiFi + HTTPS "Ask Claude" modal
// ============================================================

#include "LGFX_Config.hpp"

#if defined(BUDDY_BOARD_CYD)
  #define BUDDY_BOARD_NAME      "ESP32-2432S028R 2.8in (CYD)"
  #define BUDDY_TOUCH_RESISTIVE 1
  #define BUDDY_WAKE_PIN        36
  #define BUDDY_SPK_PIN         26
  #define BUDDY_AMP_EN_PIN      4
  #define BUDDY_LED_PIN         17     // blue leg — red (GPIO4) doubles as amp enable
  #define BUDDY_BAT_ADC_PIN     34

#elif defined(BUDDY_BOARD_SUNTON_2432S024C)
  #define BUDDY_BOARD_NAME      "ESP32-2432S024C 2.4in"
  #define BUDDY_SPK_PIN         26
  #define BUDDY_AMP_EN_PIN      4
  #define BUDDY_LED_PIN         17

#elif defined(BUDDY_BOARD_SUNTON_3248S035R)
  #define BUDDY_BOARD_NAME      "ESP32-3248S035R 3.5in"
  #define BUDDY_TOUCH_RESISTIVE 1
  #define BUDDY_WAKE_PIN        36
  #define BUDDY_SPK_PIN         26
  #define BUDDY_AMP_EN_PIN      4
  #define BUDDY_LED_PIN         17

#elif defined(BUDDY_BOARD_SUNTON_3248S035C)
  #define BUDDY_BOARD_NAME      "ESP32-3248S035C 3.5in"
  #define BUDDY_SPK_PIN         26
  #define BUDDY_AMP_EN_PIN      4
  #define BUDDY_LED_PIN         17

#elif defined(BUDDY_BOARD_ELECROW_ADVANCE_3_5)
  #define BUDDY_BOARD_NAME      "CrowPanel Advance 3.5in"
  // Panel config uses offset_rotation=3, so rotation 0 is landscape 480x320.

#elif defined(BUDDY_BOARD_SUNTON_4827S043C)
  #define BUDDY_BOARD_NAME      "ESP32-4827S043C 4.3in"
  #define BUDDY_RGB_PANEL       1
  #define BUDDY_ASK_CLAUDE      1

#elif defined(BUDDY_BOARD_SUNTON_8048S050C)
  #define BUDDY_BOARD_NAME      "ESP32-8048S050C 5in"
  #define BUDDY_RGB_PANEL       1
  #define BUDDY_ASK_CLAUDE      1

#elif defined(BUDDY_BOARD_SUNTON_8048S070)
  #define BUDDY_BOARD_NAME      "ESP32-8048S070 7in"
  #define BUDDY_RGB_PANEL       1
  #define BUDDY_ASK_CLAUDE      1

#elif defined(BUDDY_BOARD_ELECROW_7_0)
  #define BUDDY_BOARD_NAME      "CrowPanel 7.0in"
  #define BUDDY_RGB_PANEL       1
  #define BUDDY_ASK_CLAUDE      1

#elif defined(BUDDY_BOARD_ELECROW_5_0)
  #define BUDDY_BOARD_NAME      "CrowPanel 5.0in"
  #define BUDDY_RGB_PANEL       1
  #define BUDDY_ASK_CLAUDE      1

#elif defined(BUDDY_BOARD_ELECROW_ADVANCE_5_0)
  #if defined(BUDDY_ELECROW_ADVANCE_4_3)
    #define BUDDY_BOARD_NAME    "CrowPanel Advance 4.3in"
  #elif defined(PIXELCADE_STC8H1K28_V1_1)
    #define BUDDY_BOARD_NAME    "CrowPanel Advance 5.0in v1.1"
  #else
    #define BUDDY_BOARD_NAME    "CrowPanel Advance 5.0in"
  #endif
  #define BUDDY_RGB_PANEL       1
  #define BUDDY_ASK_CLAUDE      1

#elif defined(BUDDY_BOARD_WAVESHARE_P4_4_3)
  // ESP32-P4: no radio of its own. BLE runs on the onboard ESP32-C6 over
  // ESP-Hosted (SDIO), through the Arduino core's BLE library — see
  // ble_bridge_hosted.cpp. Panel is 480x800 MIPI-DSI; rotation 1 = landscape.
  #define BUDDY_BOARD_NAME      "Waveshare ESP32-P4 4.3in"
  #define BUDDY_ROTATION        1
  #define BUDDY_HOSTED_SDIO_PINS 18, 19, 14, 15, 16, 17, 54   // CLK CMD D0-D3 RST

#elif defined(BUDDY_BOARD_ELECROW_ROUND_2_1)
  #define BUDDY_BOARD_NAME      "CrowPanel 2.1in Round"
  #define BUDDY_RGB_PANEL       1
  #define BUDDY_ROUND_PANEL     1

#else
  #error "No BUDDY_BOARD_* define — pick an env from platformio.ini"
#endif

#ifndef BUDDY_ROTATION
#define BUDDY_ROTATION 0
#endif
#ifndef BUDDY_TOUCH_RESISTIVE
#define BUDDY_TOUCH_RESISTIVE 0
#endif
#ifndef BUDDY_WAKE_PIN
#define BUDDY_WAKE_PIN -1
#endif
#ifndef BUDDY_SPK_PIN
#define BUDDY_SPK_PIN -1
#endif
#ifndef BUDDY_AMP_EN_PIN
#define BUDDY_AMP_EN_PIN -1
#endif
#ifndef BUDDY_LED_PIN
#define BUDDY_LED_PIN -1
#endif
#ifndef BUDDY_BAT_ADC_PIN
#define BUDDY_BAT_ADC_PIN -1
#endif
#ifndef BUDDY_RGB_PANEL
#define BUDDY_RGB_PANEL 0
#endif
#ifndef BUDDY_ROUND_PANEL
#define BUDDY_ROUND_PANEL 0
#endif
#ifndef BUDDY_ASK_CLAUDE
#define BUDDY_ASK_CLAUDE 0
#endif

// Board bring-up that has to happen around lcd.init(): I2C expanders that
// hold the panel/touch in reset, backlight co-processors, native-USB start.
void boardPreInit();                 // before lcd.init()
void boardPostInit(LGFX& lcd);       // after lcd.init()
// 0..100. Routes to LovyanGFX's Light_PWM or the board's backlight chip.
void boardSetBrightness(LGFX& lcd, uint8_t percent);
