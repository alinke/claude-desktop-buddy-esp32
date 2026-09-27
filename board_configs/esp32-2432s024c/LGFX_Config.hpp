// LovyanGFX configuration for the Sunton ESP32-2432S024C: 2.4" 320x240
// ILI9341 over SPI, CST820 capacitive touch, classic ESP32-WROOM-32 (no
// PSRAM). Display pins match esp32-2432s028r (same Sunton 2432 family)
// except the backlight, which is GPIO27 here (GPIO21 on the 2.8"). Pins
// taken from github.com/edmasini/esp32-2432S024-Capacitive
// (hal/esp32/displays/LGFX_ESP32_2432024C.hpp and hal/esp32/app_hal.h).
//
// Touch: the CST820 speaks the same register protocol as the CST816S
// (I2C address 0x15, touch count at 0x02, X/Y from 0x03), so LovyanGFX's
// Touch_CST816S driver is used. pin_int left at -1 (polling), matching
// esp32-3248s035c's GT911 setup, rather than relying on the INT line (21).
#pragma once

#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_SPI _bus_instance;
    lgfx::Panel_ILI9341 _panel_instance;
    lgfx::Light_PWM _light_instance;
    lgfx::Touch_CST816S _touch_instance;

public:
    LGFX(void) {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 24000000;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = 14;
            cfg.pin_mosi = 13;
            cfg.pin_miso = 12;
            cfg.pin_dc = 2;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs = 15;
            cfg.pin_rst = -1;
            cfg.pin_busy = -1;
            cfg.memory_width = 240;
            cfg.memory_height = 320;
            cfg.panel_width = 240;
            cfg.panel_height = 320;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.rgb_order = false;
            cfg.invert = false;
            _panel_instance.config(cfg);
        }
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = 27;
            cfg.invert = false;
            cfg.freq = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }
        {
            auto cfg = _touch_instance.config();
            cfg.i2c_port = 0;  // I2C_NUM_0
            cfg.pin_sda = 33;
            cfg.pin_scl = 32;
            cfg.pin_int = -1;
            cfg.pin_rst = 25;
            cfg.freq = 400000;
            cfg.i2c_addr = 0x15;
            cfg.x_min = 0;
            cfg.x_max = 239;
            cfg.y_min = 0;
            cfg.y_max = 319;
            // 180 degrees: CONFIRMED on real hardware via a one-corner-at-a-
            // time tap test -- without it every point came back at
            // (319-x, 239-y) (both axes mirrored) in landscape.
            cfg.offset_rotation = 2;
            cfg.bus_shared = false;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }
        setPanel(&_panel_instance);
    }
};
