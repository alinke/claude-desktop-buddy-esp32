// Elecrow CrowPanel 2.1" round rotary display (PCB silkscreen "ESP32 Display
// 2.1 V1.0"): ESP32-S3 (16MB flash, octal PSRAM), native USB-Serial-JTAG
// (0x303A:0x1001), 480x480 round IPS panel driven by an ST7701 over
// RGB-parallel, CST8xx capacitive touch (I2C 0x15). The first round panel in
// this catalog -- the corners of the 480x480 frame are simply not visible.
//
// Pins/timing from Elecrow's own factory firmware
// (github.com/Elecrow-RD/CrowPanel-2.1inch-HMI-ESP32-Rotary-Display-480-480-
// IPS-Round-Touch-Knob-Screen, factory_soucecode/RotaryScreen_2_1/
// RotaryScreen_2_1.ino). NOT from the wiki's pin table, which lists GPIO39
// as both HSYNC and I2C SCL and is wrong.
//
// The LCD's power and reset and the touch controller's reset go through a
// PCF8574 I2C expander (0x21, same bus as touch: SDA 38 / SCL 39) -- see
// resetPanelAndTouch() in main.cpp, which must run before lcd.init().
#pragma once

#include <LovyanGFX.hpp>
// Not pulled in by the umbrella header -- see elecrow-crowpanel-5-0-v3's
// LGFX_Config.hpp.
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>

// ST7701 with Elecrow's init table: Arduino_GFX's st7701_type5_init_operations
// (the table Elecrow's factory firmware passes to Arduino_RGB_Display),
// transcribed byte-for-byte into LovyanGFX's command-list format. The base
// class sends the SPI 3-wire init (CS 16 / SCK 2 / SDA 1, set in
// config_detail below) after starting the RGB scan-out, and writes LNSET and
// RGBCTRL from the panel/bus config before this list; type5's own C0 below
// then overrides LNSET with Elecrow's value.
struct Panel_ST7701_ElecrowRound : public lgfx::Panel_ST7701_Base
{
protected:
    const uint8_t* getInitCommands(uint8_t listno) const override
    {
        static constexpr const uint8_t list0[] =
        {
            0xFF,  5, 0x77, 0x01, 0x00, 0x00, 0x10,
            0xC0,  2, 0x3B, 0x00,
            0xC1,  2, 0x0B, 0x02,
            0xC2,  2, 0x00, 0x02,
            0xCC,  1, 0x10,
            0xCD,  1, 0x08,
            0xB0, 16, 0x02, 0x13, 0x1B, 0x0D, 0x10, 0x05, 0x08, 0x07,
                      0x07, 0x24, 0x04, 0x11, 0x0E, 0x2C, 0x33, 0x1D,
            0xB1, 16, 0x05, 0x13, 0x1B, 0x0D, 0x11, 0x05, 0x08, 0x07,
                      0x07, 0x24, 0x04, 0x11, 0x0E, 0x2C, 0x33, 0x1D,

            0xFF,  5, 0x77, 0x01, 0x00, 0x00, 0x11,
            0xB0,  1, 0x5D,
            0xB1,  1, 0x43,
            0xB2,  1, 0x81,
            0xB3,  1, 0x80,
            0xB5,  1, 0x43,
            0xB7,  1, 0x85,
            0xB8,  1, 0x20,
            0xC1,  1, 0x78,
            0xC2,  1, 0x78,
            0xD0,  1, 0x88,
            0xE0,  3, 0x00, 0x00, 0x02,
            0xE1, 11, 0x03, 0xA0, 0x00, 0x00, 0x04, 0xA0, 0x00, 0x00,
                      0x00, 0x20, 0x20,
            0xE2, 13, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x00, 0x00,
            0xE3,  4, 0x00, 0x00, 0x11, 0x00,
            0xE4,  2, 0x22, 0x00,
            0xE5, 16, 0x05, 0xEC, 0xA0, 0xA0, 0x07, 0xEE, 0xA0, 0xA0,
                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0xE6,  4, 0x00, 0x00, 0x11, 0x00,
            0xE7,  2, 0x22, 0x00,
            0xE8, 16, 0x06, 0xED, 0xA0, 0xA0, 0x08, 0xEF, 0xA0, 0xA0,
                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0xEB,  7, 0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00,
            0xED, 16, 0xFF, 0xFF, 0xFF, 0xBA, 0x0A, 0xBF, 0x45, 0xFF,
                      0xFF, 0x54, 0xFB, 0xA0, 0xAB, 0xFF, 0xFF, 0xFF,
            0xEF,  6, 0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F,

            0xFF,  5, 0x77, 0x01, 0x00, 0x00, 0x13,
            0xEF,  1, 0x08,

            0xFF,  5, 0x77, 0x01, 0x00, 0x00, 0x00,
            // MADCTL BGR bit: Elecrow sets it (their setup() re-sends 0x36=0x08
            // after gfx->begin() too). Red/blue are swapped at the data pins
            // below instead of in software like Elecrow's flush callback.
            0x36,  1, 0x08,
            0x3A,  1, 0x60,  // RGB666 interface (16 wired lines)
            0x11, CMD_INIT_DELAY, 100,  // Sleep Out
            0x29, CMD_INIT_DELAY, 50,   // Display On
            0xFF, 0xFF,
        };
        switch (listno)
        {
        case 0: return list0;
        default: return nullptr;
        }
    }
};

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_RGB _bus_instance;
    Panel_ST7701_ElecrowRound _panel_instance;
    lgfx::Light_PWM _light_instance;
    lgfx::Touch_CST816S _touch_instance;

public:
    LGFX(void) {
        {
            auto cfg = _panel_instance.config();
            cfg.memory_width = 480;
            cfg.memory_height = 480;
            cfg.panel_width = 480;
            cfg.panel_height = 480;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            _panel_instance.config(cfg);
        }
        {
            // ST7701 SPI 3-wire init bus (bit-banged by Panel_ST7701_Base).
            auto cfg = _panel_instance.config_detail();
            cfg.pin_cs = 16;
            cfg.pin_sclk = 2;
            cfg.pin_mosi = 1;
            _panel_instance.config_detail(cfg);
        }
        {
            auto cfg = _bus_instance.config();
            cfg.panel = &_panel_instance;

            // Elecrow's firmware passes 5/45/48/47/21 as "B0-B4" and
            // 46/3/8/18/17 as "R0-R4", then swaps red and blue in software
            // on every flush ("ESP-IDF 5's RGB path on this ST7701 board
            // presents the red and blue 5-bit fields in the opposite
            // order"). Swapped here at the pins instead, which costs nothing.
            cfg.pin_d0 = 46;   // B0
            cfg.pin_d1 = 3;    // B1
            cfg.pin_d2 = 8;    // B2
            cfg.pin_d3 = 18;   // B3
            cfg.pin_d4 = 17;   // B4
            cfg.pin_d5 = 14;   // G0
            cfg.pin_d6 = 13;   // G1
            cfg.pin_d7 = 12;   // G2
            cfg.pin_d8 = 11;   // G3
            cfg.pin_d9 = 10;   // G4
            cfg.pin_d10 = 9;   // G5
            cfg.pin_d11 = 5;   // R0
            cfg.pin_d12 = 45;  // R1
            cfg.pin_d13 = 48;  // R2
            cfg.pin_d14 = 47;  // R3
            cfg.pin_d15 = 21;  // R4

            cfg.pin_henable = 40;  // DE
            cfg.pin_vsync = 7;
            cfg.pin_hsync = 15;
            cfg.pin_pclk = 41;
            // 10MHz = 160MHz PLL / 16, an exact divide. Elecrow's 12MHz
            // (160/13.33, fractional) showed display noise with WiFi up --
            // same PSRAM-bandwidth problem as the other RGB boards (see
            // pixelcadeusbtools CLAUDE.md "RGB panels + WiFi: display
            // noise"); QIO flash alone wasn't enough. ~38Hz refresh.
            cfg.freq_write = 10000000;

            // Polarity 1 = sync idle high; both drivers map it the same way
            // (hsync_idle_low = !polarity).
            cfg.hsync_polarity = 1;
            cfg.hsync_front_porch = 10;
            cfg.hsync_pulse_width = 4;
            cfg.hsync_back_porch = 20;
            cfg.vsync_polarity = 1;
            cfg.vsync_front_porch = 10;
            cfg.vsync_pulse_width = 4;
            cfg.vsync_back_porch = 20;
            cfg.pclk_active_neg = 0;
            cfg.de_idle_high = 0;
            cfg.pclk_idle_high = 0;

            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = 6;
            cfg.invert = false;
            cfg.freq = 5000;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }
        {
            // CST8xx (Elecrow uses Adafruit_CST8XX at 0x15); LovyanGFX's
            // CST816S driver speaks the same registers. INT and RST are on
            // the PCF8574, so neither is wired to a GPIO here.
            auto cfg = _touch_instance.config();
            cfg.i2c_port = 0;
            cfg.pin_sda = 38;
            cfg.pin_scl = 39;
            cfg.pin_int = -1;
            cfg.pin_rst = -1;
            cfg.freq = 400000;
            cfg.i2c_addr = 0x15;
            cfg.x_min = 0;
            cfg.x_max = 479;
            // Raw Y reads ~20px low on this panel -- Elecrow's factory
            // firmware subtracts 20 (`p.y - 20`); confirmed here with a
            // one-spot-at-a-time tap test (taps landed 15-45px below their
            // targets, X correct). Shifting the range by 20 does the same.
            cfg.y_min = 20;
            cfg.y_max = 499;
            cfg.bus_shared = false;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }

        setPanel(&_panel_instance);
    }

    // See elecrow-crowpanel-5-0-v3's LGFX_Config.hpp -- used by main.cpp's
    // double-buffered frame push.
    lgfx::Bus_RGB* rgbBus() { return &_bus_instance; }
    lgfx::Panel_RGB* rgbPanel() { return &_panel_instance; }
};
