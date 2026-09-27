// Elecrow CrowPanel 5.0" HMI (the red, non-"Advance" board): SKU
// DIS07050H, PCB V3.0, ESP32-S3-WROOM-1-N4R8 (4MB flash, 8MB PSRAM), CH340
// (0x1A86:0x7523), 800x480 RGB-parallel, GT911 capacitive touch.
//
// A genuinely different board from the Sunton ESP32-8048S050 (esp32-5) --
// the "Elecrow CrowPanel 5.0" question left open in pixelcadeusbtools'
// CLAUDE.md. Same V3.0 family as the 7" Elecrow (esp32-8048s070c): the RGB
// panel's reset and the GT911's reset/address select go through a PCA9557
// I2C expander (0x18, on the touch bus SDA 19 / SCL 20), backlight GPIO2,
// pclk GPIO0 -- so it reuses that board's resetPanelAndTouch() (PCA9557.h is
// copied here). Its RGB data-pin order, DE/VSYNC pins and timing differ.
//
// Pin/timing values from Elecrow's own V3.0 example
// (github.com/Elecrow-RD/CrowPanel-5.0-HMI-ESP32-Display-800x480,
// example/V1.0_and_V2.0/Arduino/Arduino-Hardware-Version-3.0/
// crowpanel-esp32-5.0-3.0/crowpanel-esp32-5.0-3.0.ino).
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
            cfg.pin_d0 = 8;    // B0
            cfg.pin_d1 = 3;    // B1
            cfg.pin_d2 = 46;   // B2
            cfg.pin_d3 = 9;    // B3
            cfg.pin_d4 = 1;    // B4
            cfg.pin_d5 = 5;    // G0
            cfg.pin_d6 = 6;    // G1
            cfg.pin_d7 = 7;    // G2
            cfg.pin_d8 = 15;   // G3
            cfg.pin_d9 = 16;   // G4
            cfg.pin_d10 = 4;   // G5
            cfg.pin_d11 = 45;  // R0
            cfg.pin_d12 = 48;  // R1
            cfg.pin_d13 = 47;  // R2
            cfg.pin_d14 = 21;  // R3
            cfg.pin_d15 = 14;  // R4

            cfg.pin_henable = 40;  // DE
            cfg.pin_vsync = 41;
            cfg.pin_hsync = 39;
            cfg.pin_pclk = 0;  // GPIO0 -- yes, the boot-strapping pin; confirmed
                               // from two independent official Elecrow sources.
            // 13.33MHz (160MHz PLL / 12, an exact divider), not the vendor's 15MHz
            // (160/10.67, fractional): with WiFi up, 15MHz showed display noise
            // AND stalled WiFi transfers (5/30 cards, write timeouts); 13.33MHz
            // is clean on both -- confirmed on real hardware (288485804EF0).
            // Lower pixel clock = less PSRAM bandwidth for scan-out, the same
            // lever that fixed the 5" Advance.
            cfg.freq_write = 13333333;

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
