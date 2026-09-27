// Elecrow CrowPanel Advance 3.5" HMI (ESP32-S3-WROOM-1-N16R8, SKU DIS01835A,
// PCB V1.2-V1.4 share all I/O). 480x320 IPS, ILI9488 over SPI, GT911
// capacitive touch.
//
// Pin/panel values from Elecrow's own PlatformIO example
// (github.com/Elecrow-RD/CrowPanel-Advance-3.5-HMI-ESP32-S3-AI-Powered-IPS-
// Touch-Screen-480x320, example/V1.2_and_V1.3_and_V1.4/PlatformIO/include/
// LovyanGFX_Driver.h), trusted over Elecrow's wiki page, whose pin table
// mixes in the TF card's SPI pins (MOSI 6 / MISO 4 / CLK 5).
#pragma once
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_SPI _bus_instance;
    lgfx::Panel_ILI9488 _panel_instance;
    lgfx::Light_PWM _light_instance;
    lgfx::Touch_GT911 _touch_instance;
public:
    LGFX(void) {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 40000000;  // vendor example
            cfg.freq_read = 16000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk = 42;
            cfg.pin_mosi = 39;
            cfg.pin_miso = -1;
            cfg.pin_dc = 41;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs = 40;
            cfg.pin_rst = 2;
            cfg.pin_busy = -1;
            cfg.memory_width = 320;
            cfg.memory_height = 480;
            cfg.panel_width = 320;
            cfg.panel_height = 480;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 3;  // vendor example
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = false;
            cfg.invert = true;        // IPS panel, per vendor example
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = true;
            _panel_instance.config(cfg);
        }
        {
            // Backlight on GPIO38 (the vendor example just drives it HIGH).
            auto cfg = _light_instance.config();
            cfg.pin_bl = 38;
            cfg.invert = false;
            cfg.freq = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }
        {
            auto cfg = _touch_instance.config();
            cfg.i2c_port = 0;  // I2C_NUM_0
            cfg.pin_sda = 15;
            cfg.pin_scl = 16;
            cfg.pin_int = 47;
            cfg.pin_rst = 48;
            cfg.freq = 400000;
            cfg.i2c_addr = 0x14;
            cfg.x_min = 0;
            cfg.x_max = 319;
            cfg.y_min = 0;
            cfg.y_max = 479;
            cfg.offset_rotation = 0;  // vendor example -- verify with a one-corner-at-a-time tap test
            cfg.bus_shared = false;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }
        setPanel(&_panel_instance);
    }
};
