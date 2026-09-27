// LovyanGFX panel configuration for the Elecrow CrowPanel Advance 5.0" HMI
// (ESP32-S3-WROOM-1-N16R8, ST7262 RGB-parallel driver IC, 800x480 IPS,
// GT911 capacitive touch) -- genuinely different hardware from this
// project's existing "esp32-5" (elecrow-crowpanel-5-0, actually Sunton
// ESP32-8048S050C-I silicon under an earlier, incorrect Elecrow
// assumption -- see that board's own LGFX_Config.hpp header). This one is
// real, factory-confirmed Elecrow hardware: Elecrow's own official GitHub
// repo (Elecrow-RD/CrowPanel-Advance-5-HMI-ESP32-S3-AI-Powered-IPS-Touch-
// Screen-800x480) ships a working PlatformIO example whose
// include/LovyanGFX_Driver.h uses this exact same Bus_RGB/Panel_RGB/
// Touch_GT911 combination -- pin numbers, timings, and touch config below
// are taken directly from that file (the repo's example/V1.2_and_V1.3/
// folder specifically), not guessed or ported from an unrelated board.
// Confirmed the physical unit this project has in hand IS actually
// hardware v1.3 (silkscreen), matching that reference exactly -- v1.3 is
// the same source as v1.2 per the repo's own version table (only the
// touch FPC connector packaging changed between them), so no need to
// second-guess against the older v1.0/v1.1 example folders -- for THIS
// file's panel/touch config, that is: a v1.1 customer report later
// prompted diffing v1.1's own vendor driver header directly, and confirmed
// the RGB pins/timing and GT911 touch config here ARE identical across
// v1.1 and v1.2/v1.3, so this file is unchanged for that revision. Its
// backlight co-processor protocol is NOT the same across revisions,
// though -- see Backlight_STC8H1K28.hpp and Backlight_STC8H1K28_V1_1.hpp.
//
// CAVEAT worth re-checking if the picture ever looks wrong: that vendor
// reference's OWN main.cpp never actually calls lcd.begin()/pushImage() on
// the LGFX _panel_instance/_bus_instance configured here -- it drives real
// pixels through a separate, raw esp_lcd_new_rgb_panel() call instead
// (LVGL's own flush callback), and only ever uses this LGFX class for its
// _touch_instance. So the Bus_RGB polarity/timing fields below (hsync/
// vsync_polarity, pclk_idle_high) are the vendor's OWN attempt at an LGFX
// translation, not something their factory firmware's real render path
// ever exercised. NOT yet independently confirmed on real hardware for
// this project's own render path (which DOES actually push pixels through
// _panel_instance) -- if the boot splash renders garbled/blank/shifted,
// this translation (rather than the pin assignments, which came from BOTH
// the vendor's raw-ESP-IDF code AND this LGFX header, so are corroborated
// twice) is the first thing to revisit.
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
    lgfx::Touch_GT911 _touch_instance;

public:
    LGFX(void) {
        {
            auto cfg = _bus_instance.config();
            cfg.panel = &_panel_instance;

            // RGB565 data lines -- straight from Elecrow's own
            // include/LovyanGFX_Driver.h AND readme.md pin table (the two
            // agree byte-for-byte, plus the raw ESP-IDF main.cpp's own
            // rgb_pins[16] array a third time).
            cfg.pin_d0 = 21;   // B0
            cfg.pin_d1 = 47;   // B1
            cfg.pin_d2 = 48;   // B2
            cfg.pin_d3 = 45;   // B3
            cfg.pin_d4 = 38;   // B4
            cfg.pin_d5 = 9;    // G0
            cfg.pin_d6 = 10;   // G1
            cfg.pin_d7 = 11;   // G2
            cfg.pin_d8 = 12;   // G3
            cfg.pin_d9 = 13;   // G4
            cfg.pin_d10 = 14;  // G5
            cfg.pin_d11 = 7;   // R0
            cfg.pin_d12 = 17;  // R1
            cfg.pin_d13 = 18;  // R2
            cfg.pin_d14 = 3;   // R3
            cfg.pin_d15 = 46;  // R4

            cfg.pin_henable = 42;
            cfg.pin_vsync = 41;
            cfg.pin_hsync = 40;
            cfg.pin_pclk = 39;

            // Vendor-confirmed via their raw ESP-IDF example (the code path
            // they actually ship/test, unlike the LGFX translation this
            // file's header comment flags as unverified) -- 18MHz, narrow
            // 4/8/8 porches, pclk sampled on the falling edge with the idle
            // line held high (clock later lowered to 16MHz, see below).
            // Deliberately NOT reusing elecrow-crowpanel-5-0's
            // own 16MHz/wide-porch values here -- that board is different
            // (Sunton) silicon behind a different driver IC (ST7262 here vs
            // that board's actual chip), so its timing tuning has no reason
            // to transfer.
            // 16MHz, not the vendor's 18MHz: with WiFi carrying cards, 18MHz
            // showed display noise even on QIO flash; 16MHz (~11% less PSRAM
            // read bandwidth for scan-out, same as the Sunton 5") is clean
            // with WiFi on, confirmed on real hardware. See pixelcadeusbtools'
            // CLAUDE.md, "RGB panels + WiFi: display noise".
            // Lower was tried and didn't help: 13.33MHz (160/12) doesn't lock
            // at all (cycles solid red/green/blue); 14.55MHz (160/11) locks
            // but still showed short streaks mid-session with cards over
            // WiFi, same as 16MHz, at a lower refresh rate (35.5 vs 39Hz).
            // See SIDEKICK_BOARDS.md, esp32-5-elecrow-advance, "open issue".
            cfg.freq_write = 16000000;
            cfg.hsync_polarity = 1;
            cfg.hsync_pulse_width = 4;
            cfg.hsync_back_porch = 8;
            cfg.hsync_front_porch = 8;
            cfg.vsync_polarity = 1;
            cfg.vsync_pulse_width = 4;
            cfg.vsync_back_porch = 8;
            cfg.vsync_front_porch = 8;
            cfg.pclk_active_neg = 1;
            cfg.pclk_idle_high = 1;

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
            auto cfg = _panel_instance.config_detail();
            cfg.use_psram = 1;
            _panel_instance.config_detail(cfg);
        }

        // No Light_PWM here -- unlike every other RGB-parallel board in
        // this catalog, this one has NO backlight GPIO at all. Backlight
        // (and the buzzer/speaker) are commanded over I2C to an onboard
        // STC8H1K28 microcontroller at address 0x30 (0=brightest,
        // 245=off) -- see Backlight_STC8H1K28.hpp, driven separately from
        // main.cpp since it shares the same I2C bus as touch but isn't a
        // LovyanGFX concept.

        {
            // GT911 capacitive touch -- SDA/SCL straight from Elecrow's
            // pin table and LGFX header (both agree); no dedicated RST or
            // INT pin at all (pin_rst=-1, pin_int=-1 in the vendor's own
            // LGFX header, not a "we didn't bother wiring it" guess like
            // some other boards in this catalog needed correcting to --
            // this board genuinely has neither pin broken out for touch).
            // Address 0x5D confirmed both in the vendor's LGFX header
            // comment ("0x5D, 0x14") and empirically in their raw main.cpp
            // startup scan (i2cScanForAddress(0x5D) alongside the backlight
            // co-processor's 0x30).
            auto cfg = _touch_instance.config();
            cfg.i2c_port = I2C_NUM_0;
            cfg.pin_sda = 15;
            cfg.pin_scl = 16;
            cfg.pin_int = -1;
            cfg.pin_rst = -1;
            cfg.freq = 400000;
            cfg.i2c_addr = 0x5D;
            cfg.x_min = 0;
            cfg.x_max = 800;
            cfg.y_min = 0;
            cfg.y_max = 480;
            cfg.bus_shared = false;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }

        setPanel(&_panel_instance);
    }

    // Pixelcade patch -- exposes the private Bus_RGB instance so
    // main.cpp's handleCompleteFrame() can reach getBackBuffer()/
    // commitBackBuffer() for tear-free double-buffered frame pushes. See
    // apply_lovyangfx_rgb_patch.py's module doc comment ("SECOND FIX") for
    // the full mechanism -- same pattern every RGB-parallel board here uses.
    lgfx::Bus_RGB* rgbBus() { return &_bus_instance; }
    lgfx::Panel_RGB* rgbPanel() { return &_panel_instance; }
};
