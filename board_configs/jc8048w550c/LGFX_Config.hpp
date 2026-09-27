// LovyanGFX panel configuration for the 5" 800x480 ST7262 RGB-parallel panel.
//
// Pin values below come from the community board definition at
// boards/jc8048w550c.json (rzeldent/platformio-espressif32-sunton). That repo
// documents a Guition-manufactured board resold under several names,
// including the "ESP32-8048S050" family referenced in this project's design
// notes. VERIFY against your actual board's schematic if the display doesn't
// come up cleanly -- vendors have been known to silently revise pinouts
// under the same model number.
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

            // RGB565 data lines: R0-R4, G0-G5, B0-B4
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
            // Sunton's original 16MHz (160MHz PLL / 10, exact). Confirmed on
            // real hardware that this panel doesn't tolerate nearby values:
            // 20MHz glitched (swipe menu), 13.33MHz showed no image at all,
            // and 10/13MHz broke rendering in an earlier, pre-bounce-buffer test.
            cfg.freq_write = 16000000;

            // Widened to match esp32-8048s070c's (7", same panel family)
            // more generous hsync margins. Tested for right-edge column noise
            // on two physical units of this board and confirmed NOT the cause
            // (no change on either); kept since it's harmless.
            cfg.hsync_polarity = 0;
            cfg.hsync_front_porch = 40;
            cfg.hsync_pulse_width = 48;
            cfg.hsync_back_porch = 40;
            cfg.vsync_polarity = 0;
            cfg.vsync_front_porch = 8;
            cfg.vsync_pulse_width = 4;
            cfg.vsync_back_porch = 8;
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
            auto cfg = _light_instance.config();
            cfg.pin_bl = 2;
            cfg.invert = false;
            // 1000Hz, not the 44100 most other direct-PWM boards use --
            // confirmed on real hardware (this board) that 44100 made 100%-70%
            // duty all look identically full-bright then cut hard to
            // black at 65%, while 1000Hz gives a smooth 100%-~10% fade
            // (10% and below reads as off, a normal minimum-duty-cycle
            // floor for this style of boost-driven backlight). See that
            // board's own LGFX_Config.hpp comment for the full writeup.
            cfg.freq = 1000;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        {
            // GT911 capacitive touch, on its own I2C bus (SDA=19, SCL=20),
            // not shared with the RGB display bus. RST=38, address 0x5D per
            // the board definition (the other common GT911 address is 0x14
            // -- if touch doesn't respond, try that instead).
            //
            // pin_int = -1 (NOT 18, this file's own original value, pulled
            // unmodified from Sunton's generic community board JSON --
            // GT911_TOUCH_CONFIG_INT=18): confirmed via the real physical
            // unit's own schematic (silkscreen "ESP32-8048S050C-I") that the
            // "Capacitive touch" FPC connector
            // block has no ESP32 pin wired to INT at all -- just a bare net
            // name (TP_INT), unconnected. LovyanGFX's Touch_GT911 driver
            // gates every touch read on pin_int's level whenever pin_int >=
            // 0, so pointing it at GPIO18 -- a real, genuinely unrelated pin
            // on this board -- left every touch check reading whatever
            // that floating pin happened to be doing electrically at the
            // moment, not real finger contact. Strong suspected root cause
            // of this session's real, repeatedly-reproduced "touch works at
            // 921600 baud, goes completely dead at 460800" finding (see
            // platformio.ini's own history comment on that): a floating
            // pin's read is exactly the kind of thing that could shift with
            // nearby, seemingly-unrelated electrical activity (baud rate,
            // timing, anything touching the UART lines a few pins over) --
            // not a genuine UART-vs-I2C HAL interaction, which is what that
            // investigation could never find a real code path for.
            auto cfg = _touch_instance.config();
            cfg.i2c_port = 0;  // I2C_NUM_0
            cfg.pin_sda = 19;
            cfg.pin_scl = 20;
            cfg.pin_int = -1;
            cfg.pin_rst = 38;
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
