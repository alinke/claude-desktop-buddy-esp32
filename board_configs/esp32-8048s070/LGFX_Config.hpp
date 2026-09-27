// LovyanGFX panel configuration for the Sunton "ESP32-8048S070" 7" 800x480
// RGB-parallel panel -- a DIFFERENT physical board from this project's
// existing esp32-8048s070c (Elecrow CrowPanel 7.0 / IoTeikXgo), despite the
// near-identical name. See esp32-8048s070c/LGFX_Config.hpp's own header
// comment for the full history: that file WAS originally based on a Sunton
// pinout under the assumption it was the same board, which produced a
// permanently black screen on the (actually Elecrow) hardware it was
// really being flashed to -- wrong PCLK pin, different timing porches, no
// error in software, just no picture. This file is that same Sunton
// pinout, finally given its own board entry now that a genuine Sunton unit
// (silkscreen "ESP32-8048S070", no trailing C) is confirmed to exist and
// need it -- confirmed distinct from the Elecrow unit by USB enumeration
// alone before any firmware change: this board enumerates as native
// USB-CDC (no CH340 vendor/product ID), while esp32-8048s070c's physical
// unit is CH340-bridged.
//
// Values below are LovyanGFX's own bundled community reference for this
// exact board name (lgfx_user/LGFX_Sunton_ESP32-8048S070.h, shipped inside
// the LovyanGFX library itself -- not hand-sourced from a third-party repo
// the way esp32-8048s070c's Elecrow values were). Pin assignment (B0-B4/
// G0-G5/R0-R4 data line order) is IDENTICAL to esp32-8048s070c's -- only
// pin_pclk, the sync timing porches, pclk_idle_high, and the touch
// controller's I2C bus/reset pin/address differ. NOT YET CONFIRMED on real
// hardware by this project -- this is the first attempt, sourced from the
// most authoritative reference available (LovyanGFX's own maintainers)
// rather than a guess, but still needs an actual flash+visual check before
// being trusted the way esp32-8048s070c's config now is.
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

            // RGB565 data lines -- same B0-B4, G0-G5, R0-R4 order as
            // esp32-8048s070c, confirmed identical against LovyanGFX's own
            // Sunton reference.
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
            cfg.pin_pclk = 42;  // NOT GPIO0 -- this is the actual difference
                                // vs esp32-8048s070c/Elecrow's PCLK pin.
            cfg.freq_write = 14000000;

            cfg.hsync_polarity = 0;
            cfg.hsync_front_porch = 80;
            cfg.hsync_pulse_width = 4;
            cfg.hsync_back_porch = 16;
            cfg.vsync_polarity = 0;
            cfg.vsync_front_porch = 22;
            cfg.vsync_pulse_width = 4;
            cfg.vsync_back_porch = 4;
            cfg.pclk_idle_high = 1;
            // pclk_active_neg/de_idle_high deliberately left at their
            // LovyanGFX defaults (both false) -- the Sunton reference this
            // file is sourced from doesn't set them either, unlike
            // esp32-8048s070c's Elecrow config which explicitly sets
            // pclk_active_neg=1/de_idle_high=0. If the picture is present
            // but garbled/shifted, this is the first place to check.

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
            // use_psram=1 -- explicitly set in LovyanGFX's own Sunton
            // reference (unlike esp32-8048s070c's config, which doesn't
            // set this field at all). Kept since this board's reference
            // config relies on it.
            auto cfg = _panel_instance.config_detail();
            cfg.use_psram = 1;
            _panel_instance.config_detail(cfg);
        }

        {
            // Backlight pin -- same GPIO2 as esp32-8048s070c.
            //
            // 1000Hz, not 44100 -- same fix as jc8048w550c's own
            // LGFX_Config.hpp (see that file's comment for the full
            // writeup). Confirmed on real esp32-8048s070 (7" Sunton)
            // hardware: same symptom as the 5" board at 44100 (100%-70%
            // duty all read as identically full-bright, hard cutoff to
            // black partway down), same fix at 1000Hz.
            auto cfg = _light_instance.config();
            cfg.pin_bl = 2;
            cfg.invert = false;
            cfg.freq = 1000;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        {
            // GT911 capacitive touch -- I2C bus, reset pin, and address all
            // differ from esp32-8048s070c's Elecrow config (I2C_NUM_0,
            // pin_rst=-1, addr=0x5D there vs I2C_NUM_1, pin_rst=38,
            // addr=0x14 here), per LovyanGFX's own Sunton reference.
            // pin_int=-1 (GPIO_NUM_NC) matches Elecrow's value, so this one
            // wasn't a difference.
            auto cfg = _touch_instance.config();
            cfg.i2c_port = 1;  // I2C_NUM_1
            cfg.pin_sda = 19;
            cfg.pin_scl = 20;
            cfg.pin_int = -1;
            cfg.pin_rst = 38;
            cfg.freq = 400000;
            cfg.i2c_addr = 0x14;
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

    // Pixelcade patch -- exposes the private Bus_RGB/Panel_RGB
    // instances so main.cpp's handleCompleteFrame() can reach
    // getBackBuffer()/commitBackBuffer()/setActiveFrameBuffer() for
    // tear-free, rotation-correct double-buffered frame pushes. See
    // apply_lovyangfx_rgb_patch.py's module doc comment ("SECOND
    // FIX"/"THIRD FIX") for the full mechanism.
    lgfx::Bus_RGB* rgbBus() { return &_bus_instance; }
    lgfx::Panel_RGB* rgbPanel() { return &_panel_instance; }
};
