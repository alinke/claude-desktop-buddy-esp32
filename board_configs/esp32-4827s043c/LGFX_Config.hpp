// LovyanGFX panel configuration for the Sunton ESP32-4827S043C -- a 4.3"
// 480x272 RGB-parallel panel, GT911 capacitive touch, ESP32-S3 (16MB
// flash / 8MB PSRAM), CH340 USB-to-serial bridge. NOT the same board as
// esp32-8048s070/esp32-8048s070c despite the similar "Sunton 4.3"/7""
// naming family -- this is a lower-resolution 480x272 panel, not 800x480.
// There is also an "R" variant of this exact board (ESP32-4827S043R) with
// resistive XPT2046 touch instead of GT911 capacitive -- this config is
// for the "C" (capacitive) variant only, confirmed by the user's own
// hardware ("ESP32-4827S043C ... capacity touch").
//
// CONFIRMED on real hardware: boot splash renders correctly (stable, no
// flicker, correct colors, "Not connected yet" text in its intended warm
// amber/gold, not swapped). Two real bugs were found and fixed getting
// here, neither in the values below:
//   1. R/G/B data pin order was initially sourced from two informal
//      community threads that agreed with each other but had R0-R4/B0-B4
//      swapped relative to the authoritative source below (see that
//      source's own note in this file).
//   2. A genuinely different bug, NOT specific to this board's pinout:
//      apply_lovyangfx_rgb_patch.py's shared Bus_RGB.cpp hardcoded
//      `bounce_buffer_size_px = width * 10`, which silently assumes the
//      panel height is divisible by 10 (true for every 800x480 board
//      already in this catalog, false for this 480x272 one -- 272/10 is
//      not an integer). ESP-IDF's esp_lcd_new_rgb_panel() aborts when
//      that's violated ("frame buffer size must be multiple of bounce
//      buffer size"), which crash-looped this board hard enough to look
//      exactly like a flickering/pulsing white screen -- indistinguishable
//      from a real signal-integrity problem without reading the actual
//      crash log over serial. Fixed in apply_lovyangfx_rgb_patch.py
//      itself (now computes a safe divisor of the real panel height at
//      runtime) -- this fix applies to every RGB-parallel board in this
//      catalog, not just this one; see that script's own comment.
//
// Values below are from
// rzeldent/platformio-espressif32-sunton
// (raw.githubusercontent.com/rzeldent/platformio-espressif32-sunton/main/
// esp32-4827S043C.json), a dedicated, actively-maintained per-board
// PlatformIO board-definitions repo for Sunton's exact boards -- cross-
// confirmed byte-for-byte identical against its own esp32-4827S043R.json
// (same physical panel, only touch controller differs between R/C). This
// REPLACED an earlier attempt sourced from two informal community
// threads (esp3d.io's hardware reference page, an Arduino_GFX GitHub
// discussion) which agreed with EACH OTHER but had the R0-R4/B0-B4 data
// pins genuinely swapped relative to this more authoritative source --
// confirmed the hard way on real hardware: that earlier config produced
// a flickering/pulsing white screen (backlight+some signal, no real
// image), and swapping neither PCLK frequency alone (8MHz->10MHz) nor
// anything else fixed it before this pin-order correction was found. This
// project has been burned by a similar-looking-but-wrong Sunton pinout
// once already (see board_configs/esp32-8048s070c/LGFX_Config.hpp's own
// header) -- prefer a dedicated board-definitions repo over informal
// community threads when one exists, even if the threads independently
// agree with each other.
#pragma once

#include <LovyanGFX.hpp>
// Not pulled in by the umbrella header above -- ESP32-S3's RGB-parallel bus
// and panel classes are only auto-included for boards using Bus_Parallel8/16,
// not Bus_RGB, so they need including directly.
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_RGB _bus_instance;
    lgfx::Panel_RGB _panel_instance;
    lgfx::Light_PWM _light_instance;
    lgfx::Touch_GT911 _touch_instance;

public:
    LGFX(void) {
        {
            auto cfg = _bus_instance.config();
            cfg.panel = &_panel_instance;

            // RGB565 data lines, R0-R4/G0-G5/B0-B4 order (LGFX pin_d0-d15
            // convention) -- verbatim from rzeldent's board-definitions repo,
            // cross-confirmed identical between the R/C variant JSONs. NOTE:
            // this is R-first, the OPPOSITE order from esp32-8048s070c's own
            // config (B-first) -- don't assume the two boards share a data
            // pin convention just because they're both Sunton/ST7262 RGB
            // panels.
            cfg.pin_d0 = 8;    // R0
            cfg.pin_d1 = 3;    // R1
            cfg.pin_d2 = 46;   // R2
            cfg.pin_d3 = 9;    // R3
            cfg.pin_d4 = 1;    // R4
            cfg.pin_d5 = 5;    // G0
            cfg.pin_d6 = 6;    // G1
            cfg.pin_d7 = 7;    // G2
            cfg.pin_d8 = 15;   // G3
            cfg.pin_d9 = 16;   // G4
            cfg.pin_d10 = 4;   // G5
            cfg.pin_d11 = 45;  // B0
            cfg.pin_d12 = 48;  // B1
            cfg.pin_d13 = 47;  // B2
            cfg.pin_d14 = 21;  // B3
            cfg.pin_d15 = 14;  // B4

            cfg.pin_henable = 40;  // DE
            cfg.pin_vsync = 41;
            cfg.pin_hsync = 39;
            cfg.pin_pclk = 42;
            // 8MHz -- ST7262_PANEL_CONFIG_TIMINGS_PCLK_HZ from the
            // authoritative source above (same value cross-confirmed in
            // both R/C variant JSONs).
            cfg.freq_write = 8000000;

            cfg.hsync_polarity = 0;
            cfg.hsync_front_porch = 8;
            cfg.hsync_pulse_width = 4;
            cfg.hsync_back_porch = 43;
            cfg.vsync_polarity = 0;
            cfg.vsync_front_porch = 8;
            cfg.vsync_pulse_width = 4;
            cfg.vsync_back_porch = 12;
            cfg.pclk_active_neg = 1;
            cfg.de_idle_high = 0;
            cfg.pclk_idle_high = 0;

            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        {
            auto cfg = _panel_instance.config();
            cfg.memory_width = 480;
            cfg.memory_height = 272;
            cfg.panel_width = 480;
            cfg.panel_height = 272;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            _panel_instance.config(cfg);
        }

        {
            // Backlight, GPIO2 -- consistent across both sources.
            auto cfg = _light_instance.config();
            cfg.pin_bl = 2;
            cfg.invert = false;
            cfg.freq = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        {
            // GT911 capacitive touch, I2C SDA=19/SCL=20 (matches
            // esp32-8048s070c's own SDA/SCL pins exactly, though that's a
            // coincidence of Sunton/Elecrow's common reference design, not
            // evidence this is the same board). RST=38 per both sources.
            // INT=-1 (NOT a real GPIO) even though both sources list a raw
            // number (18) for INT -- one source explicitly notes this pin
            // "requires hardware modification" to actually use, meaning the
            // board as shipped does NOT wire it to the GT911's INT line.
            // Starting at -1 (polling mode) deliberately, matching this
            // project's own established lesson from jc8048w550c: a
            // wrong/floating pin_int silently breaks every touch read, and
            // there's no upside to guessing a real pin here when the
            // sources themselves say it isn't actually wired by default.
            auto cfg = _touch_instance.config();
            cfg.i2c_port = 0;  // I2C_NUM_0
            cfg.pin_sda = 19;
            cfg.pin_scl = 20;
            cfg.pin_int = -1;
            cfg.pin_rst = 38;
            cfg.freq = 400000;
            cfg.i2c_addr = 0x5D;
            cfg.x_min = 0;
            cfg.x_max = 479;
            cfg.y_min = 0;
            cfg.y_max = 271;
            cfg.bus_shared = false;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }

        setPanel(&_panel_instance);
    }

    // Pixelcade patch -- exposes the private Bus_RGB instance so
    // main.cpp's handleCompleteFrame() can reach getBackBuffer()/
    // commitBackBuffer() for tear-free double-buffered frame pushes.
    // See apply_lovyangfx_rgb_patch.py's module doc comment
    // ("SECOND FIX") for the full mechanism.
    lgfx::Bus_RGB* rgbBus() { return &_bus_instance; }
    lgfx::Panel_RGB* rgbPanel() { return &_panel_instance; }
};
