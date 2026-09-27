// LovyanGFX panel configuration for the 7" 800x480 RGB-parallel panel on
// Elecrow's "CrowPanel 7.0 HMI ESP32 Display" (sold on Amazon under the
// "IoTeikXgo" brand name -- same physical board, different reseller
// branding). Display driver EK9716BD3/EK73002ACGB, GT911 capacitive touch,
// CH340K USB-to-serial bridge (see boards/esp32-8048s070c.json for the
// CH340-related build config).
//
// IMPORTANT: this board is NOT the Sunton ESP32-8048S070C this config was
// originally (incorrectly) based on, despite matching specs (7", 800x480,
// same driver ICs, "8048" naming convention). That Sunton-sourced pinout
// produced a fully initialized panel (correct backlight, successful
// lcd.drawJpg() calls) but a permanently black screen -- confirmed on real
// hardware to be a wrong PCLK pin (GPIO42 vs the real GPIO0) plus an
// R/B channel swap plus different timing porches, none of which error out
// in software, they just never produce a valid picture.
//
// Values below are pulled directly from Elecrow's own official repo:
// https://github.com/Elecrow-RD/CrowPanel-7.0-HMI-ESP32-Display-800x480
// (readme.md's "Pin definition" section, cross-confirmed against
// example/V3.0/Arduino/Course/LVGL_Arduino7.0/LVGL_Arduino7.0.ino -- both
// agree on every pin and timing value except freq_write, where the readme
// says 15MHz and the LVGL example uses 24MHz; started with the more
// conservative 15MHz given this unit's already-flaky CH340 signal
// integrity, bump to 24000000 if the picture is dim/unstable but visible).
//
// This repo also documents 3 hardware revisions (V1.0, V2.0, V3.0) with
// identical display pinout across all of them -- V3.0 only adds a PCA9557
// I/O-expander-driven touch reset sequence "to prevent touch failures",
// not a display change. Not yet implemented here; if touch is unreliable,
// see that repo's touch.h V3.0 example for the PCA9557 reset dance.
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

            // RGB565 data lines. Note the order: B0-B4, G0-G5, R0-R4 -- NOT
            // R-first like jc8048w550c/the AMOLED board. Confirmed directly
            // from Elecrow's own source (both the readme pin table and the
            // LVGL example's `dataPins[16]` array agree byte-for-byte).
            cfg.pin_d0 = 15;   // B0
            cfg.pin_d1 = 7;    // B1
            cfg.pin_d2 = 6;    // B2
            cfg.pin_d3 = 5;    // B3
            cfg.pin_d4 = 4;    // B4
            cfg.pin_d5 = 9;    // G0
            cfg.pin_d6 = 46;   // G1
            cfg.pin_d7 = 3;    // G2
            cfg.pin_d8 = 8;    // G3
            cfg.pin_d9 = 16;   // G4
            cfg.pin_d10 = 1;   // G5
            cfg.pin_d11 = 14;  // R0
            cfg.pin_d12 = 21;  // R1
            cfg.pin_d13 = 47;  // R2
            cfg.pin_d14 = 48;  // R3
            cfg.pin_d15 = 45;  // R4

            cfg.pin_henable = 41;  // DE
            cfg.pin_vsync = 40;
            cfg.pin_hsync = 39;
            cfg.pin_pclk = 0;  // GPIO0 -- yes, the boot-strapping pin; confirmed
                               // from two independent official Elecrow sources.
            cfg.freq_write = 15000000;  // see file header re: 15MHz vs 24MHz

            cfg.hsync_polarity = 0;
            cfg.hsync_front_porch = 40;
            cfg.hsync_pulse_width = 48;
            cfg.hsync_back_porch = 40;
            cfg.vsync_polarity = 0;
            cfg.vsync_front_porch = 1;
            cfg.vsync_pulse_width = 31;
            cfg.vsync_back_porch = 13;
            cfg.pclk_active_neg = 1;
            cfg.de_idle_high = 0;
            cfg.pclk_idle_high = 0;

            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        {
            auto cfg = _panel_instance.config();
            cfg.memory_width = 800;
            cfg.memory_height = 480;
            cfg.panel_width = 800;
            cfg.panel_height = 480;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            _panel_instance.config(cfg);
        }

        {
            // Backlight pin (TFT_BL=2 in Elecrow's own example) -- this part
            // was already confirmed correct on real hardware even with the
            // wrong RGB pinout (backlight came on fine), so left unchanged.
            auto cfg = _light_instance.config();
            cfg.pin_bl = 2;
            cfg.invert = false;
            cfg.freq = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        {
            // GT911 capacitive touch, I2C SDA=19/SCL=20 -- matches what this
            // file already had. INT/RST are BOTH -1 (unused/not wired) per
            // Elecrow's own touch.h (`TOUCH_GT911_INT -1`, `TOUCH_GT911_RST
            // -1`) -- this file previously guessed INT=18/RST=38 from the
            // wrong Sunton source; corrected here. I2C address not given
            // explicitly by Elecrow's example (they use the TAMC_GT911
            // library, which differs from LovyanGFX's own Touch_GT911 class
            // used here); kept at the common default 0x5D, try 0x14 if touch
            // doesn't respond.
            auto cfg = _touch_instance.config();
            cfg.i2c_port = 0;  // I2C_NUM_0
            cfg.pin_sda = 19;
            cfg.pin_scl = 20;
            cfg.pin_int = -1;
            cfg.pin_rst = -1;
            cfg.freq = 400000;
            cfg.i2c_addr = 0x5D;
            cfg.x_min = 0;
            cfg.x_max = 799;
            cfg.y_min = 0;
            cfg.y_max = 479;
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
