// LovyanGFX panel configuration for the Elecrow CrowPanel AI 5.0" HMI
// Display (SKU DHE04005D/DHE0400SD) -- ESP32-P4, ST7262 RGB-parallel
// driver IC, 800x480 IPS capacitive touch (GT911), onboard
// ESP32-C6-MINI-1 module for WiFi6/BT5.3 (unused here, same as every
// other board's onboard wireless being irrelevant to a wired Sidekick
// display), onboard STC8H1KXX co-processor for backlight PWM + GPIO
// expansion (see Backlight_STC8HAI.hpp). Source:
// Elecrow-RD/-CrowPanel-Advanced-5inch-ESP32-P4-HMI-AI-Display-800x480-
// IPS-Touch-Screen- (GitHub), example/V1.0/Arduino_Code_5inch/
// Lesson07-Turn_on_the_screen/board_config.h.
//
// Genuinely different hardware from every other board in this catalog
// with "Advance"/"AI" in its name or description so far:
//   - elecrow-crowpanel-5-0-advance is ESP32-S3, the SAME ST7262 driver
//     IC, but a DIFFERENT backlight co-processor protocol (STC8H1K28 @
//     0x30, single-command-byte) -- see that board's own
//     Backlight_STC8H1K28.hpp.
//   - jc8012p4a1c / m5stack-tab5 are ESP32-P4 like this board, but
//     MIPI-DSI panels, not RGB-parallel.
// LovyanGFX ships ZERO RGB-parallel support for the ESP32-P4 target at
// all (only DSI, under platforms/esp32p4/Panel_DSI.*/Bus_DSI.*) --
// confirmed via a direct filesystem search before writing any of this.
// So unlike every other RGB-parallel board here (which patches an
// existing-but-incomplete ESP32-S3 Bus_RGB.cpp), this board needed a
// genuinely new Bus_RGB/Panel_RGB port written from scratch for the P4
// target -- see apply_lovyangfx_rgb_patch.py's "ESP32-P4 RGB-parallel
// port" section for the full chip-agnostic verification this was based
// on (confirmed against real ESP-IDF source: SOC_LCD_RGB_SUPPORTED,
// esp_lcd_rgb_panel_config_t, the rgb565sig_tbl GPIO remap table, and
// LCD_CLK_SRC_PLL160M are all chip-agnostic, not ESP32-S3-specific) --
// before adapting this project's own already-proven ESP32-S3
// double-buffer/bounce-buffer fix rather than designing a new driver from
// nothing.
#pragma once

#include <LovyanGFX.hpp>
// This project's OWN from-scratch ESP32-P4 RGB-parallel port, written
// into LovyanGFX's esp32p4 platform folder by
// apply_lovyangfx_rgb_patch.py after every fresh library fetch (same
// ephemeral .pio/libdeps rationale as the ESP32-S3 boards' own patched
// Bus_RGB.cpp -- see that script's module doc comment) -- NOT upstream
// LovyanGFX content, which has no RGB-parallel support for this target
// at all.
#include <lgfx/v1/platforms/esp32p4/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32p4/Panel_RGB.hpp>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_RGB _bus_instance;
    lgfx::Panel_RGB _panel_instance;
    lgfx::Touch_GT911 _touch_instance;

public:
    LGFX(void) {
        {
            auto cfg = _bus_instance.config();
            cfg.panel = &_panel_instance;

            // RGB565 data lines, B0-B4/G0-G5/R0-R4 -- straight from
            // board_config.h's own RGB_PIN_NUM_DATAxx defines.
            cfg.pin_d0 = 8;    // B0
            cfg.pin_d1 = 7;    // B1
            cfg.pin_d2 = 6;    // B2
            cfg.pin_d3 = 5;    // B3
            cfg.pin_d4 = 4;    // B4
            cfg.pin_d5 = 14;   // G0
            cfg.pin_d6 = 13;   // G1
            cfg.pin_d7 = 12;   // G2
            cfg.pin_d8 = 11;   // G3
            cfg.pin_d9 = 10;   // G4
            cfg.pin_d10 = 9;   // G5
            cfg.pin_d11 = 19;  // R0
            cfg.pin_d12 = 18;  // R1
            cfg.pin_d13 = 17;  // R2
            cfg.pin_d14 = 16;  // R3
            cfg.pin_d15 = 15;  // R4

            cfg.pin_henable = 2;   // DE
            cfg.pin_vsync = 41;
            cfg.pin_hsync = 40;
            cfg.pin_pclk = 3;
            // No panel reset pin (LCD_GPIO_RST=-1 in board_config.h) and
            // no separate display-enable pin (RGB_PIN_NUM_DISP_EN=-1) --
            // Bus_RGB's disp_gpio_num is already hardcoded to
            // GPIO_NUM_NC, so nothing further is needed here for either.

            // Timing from board_config.h's own #defines, TRUSTED OVER
            // the Elecrow wiki page's contradictory/stale porch prose --
            // the header's own values are internally self-consistent
            // (18MHz / (4+8+8+800) / (4+16+16+480) ~= 42.5Hz, matching
            // its own "Refresh Rate...=42Hz" comment), the wiki's numbers
            // don't check out the same way.
            cfg.freq_write = 18000000;
            cfg.hsync_pulse_width = 4;
            cfg.hsync_back_porch = 8;
            cfg.hsync_front_porch = 8;
            cfg.vsync_pulse_width = 4;
            cfg.vsync_back_porch = 16;
            cfg.vsync_front_porch = 16;
            // Polarity/idle flags are NOT independently specified for
            // THIS board's board_config.h (only pulse widths/porches
            // are) -- carried over from elecrow-crowpanel-5-0-advance's
            // own values since it drives the exact same ST7262 driver
            // IC. CONFIRMED correct on real hardware: boot splash and
            // real Sidekick card content both render right-side-up with
            // correct colors, no rolling/shifting/inversion.
            cfg.hsync_polarity = 1;
            cfg.vsync_polarity = 1;
            cfg.pclk_active_neg = 1;
            cfg.pclk_idle_high = 1;
            cfg.de_idle_high = 0;

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

        // No Light_PWM -- like elecrow-crowpanel-5-0-advance, this board
        // has no direct backlight GPIO. Brightness is commanded over I2C
        // to the onboard STC8H1KXX co-processor (0x2F) instead -- see
        // Backlight_STC8HAI.hpp, driven from main.cpp after lcd.init()
        // since it shares the touch controller's I2C bus/port.

        {
            // GT911 capacitive touch -- SDA=45/SCL=46/RST=36/INT=42,
            // straight from board_config.h. Address 0x5D default (0x14
            // alternate depending on INT level at reset) -- matches this
            // catalog's existing default-to-0x5D convention for GT911.
            // Unlike elecrow-crowpanel-5-0-advance (no RST/INT pins
            // wired at all on that board), this one has both broken out
            // directly to the ESP32-P4 -- no STC8 co-processor
            // involvement needed for touch reset. CONFIRMED working on
            // real hardware with this direct-GPIO config as-is (no TP_RST
            // fallback through the STC8 needed -- see Backlight_STC8HAI.hpp
            // for that documented-but-unused fallback path).
            auto cfg = _touch_instance.config();
            cfg.i2c_port = I2C_NUM_0;
            cfg.pin_sda = 45;
            cfg.pin_scl = 46;
            cfg.pin_int = 42;
            cfg.pin_rst = 36;
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
    // commitBackBuffer() for tear-free double-buffered frame pushes. Same
    // pattern every RGB-parallel board in this catalog uses -- see
    // apply_lovyangfx_rgb_patch.py's module doc comment ("SECOND FIX")
    // for the full mechanism.
    lgfx::Bus_RGB* rgbBus() { return &_bus_instance; }
    lgfx::Panel_RGB* rgbPanel() { return &_panel_instance; }
};
