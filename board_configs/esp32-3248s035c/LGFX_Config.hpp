// LovyanGFX panel configuration for the 3.5" 320x480 ST7796 SPI panel with
// GT911 CAPACITIVE touch (Sunton ESP32-3248S035C) -- the capacitive-touch
// sibling of esp32-3248s035r (same panel/SPI bus, XPT2046 resistive touch
// there instead). Classic ESP32, NOT ESP32-S3 -- no native USB, no
// RGB-parallel LCD peripheral, CH340 USB-serial bridge (same VID:PID as
// every other CH340 board in this catalog, 0x1A86:0x7523).
//
// SPI bus/panel section below is copied verbatim from esp32-3248s035r's
// own CONFIRMED-working config (same ST7796 driver, same SPI pins, same
// BGR color order, same backlight pin) -- that part of the hardware is
// identical between the R/C variants per both boards' own official source.
// Only the touch block differs (GT911 I2C instead of XPT2046 SPI).
//
// Pin values come from the community board definition at
// https://github.com/rzeldent/platformio-espressif32-sunton/blob/main/esp32-3248S035C.json
// -- same source repo already proven authoritative for esp32-4827s043c
// this session (its R/C variant pair there was cross-confirmed correct on
// real hardware after an earlier, different-source pinout turned out
// wrong). NOT YET CONFIRMED on real hardware for THIS board specifically.
#pragma once

#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_SPI _bus_instance;
    lgfx::Panel_ST7796 _panel_instance;
    lgfx::Light_PWM _light_instance;
    lgfx::Touch_GT911 _touch_instance;

public:
    LGFX(void) {
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = 0;
            cfg.freq_write = 24000000;  // matches Sunton's ST7796_SPI_CONFIG_PCLK_HZ
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
            cfg.pin_rst = -1;  // Sunton's ST7796_DEV_CONFIG_RESET=GPIO_NUM_NC
            cfg.pin_busy = -1;
            // Portrait-native (320x480), same convention as esp32-3248s035r
            // -- landscape reached via BOOT_ROTATION=1 in platformio.ini,
            // not by pre-swapping these dimensions.
            cfg.memory_width = 320;
            cfg.memory_height = 480;
            cfg.panel_width = 320;
            cfg.panel_height = 480;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.rgb_order = false;  // BGR, per Sunton's ESP_LCD_COLOR_SPACE_BGR
            cfg.invert = false;
            _panel_instance.config(cfg);
        }

        {
            // Backlight -- Sunton's DISPLAY_BCKL=27, same as esp32-3248s035r.
            auto cfg = _light_instance.config();
            cfg.pin_bl = 27;
            cfg.invert = false;
            cfg.freq = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        {
            // GT911 capacitive touch, I2C SDA=33/SCL=32/RST=25 per Sunton's
            // own source. pin_int LEFT AT -1 (polling) even though the
            // source lists a real GPIO (21) for INT -- this project has
            // been burned once already (jc8048w550c) by trusting a
            // source's claimed INT pin that turned out not to actually
            // matter/work; polling is always safe, a wrong real-but-
            // unrelated INT pin is not. Revisit only if touch responsiveness
            // genuinely needs the interrupt-driven path.
            auto cfg = _touch_instance.config();
            cfg.i2c_port = 0;  // I2C_NUM_0
            cfg.pin_sda = 33;
            cfg.pin_scl = 32;
            cfg.pin_int = -1;
            cfg.pin_rst = 25;
            cfg.freq = 400000;
            cfg.i2c_addr = 0x5D;
            cfg.x_min = 0;
            cfg.x_max = 319;
            cfg.y_min = 0;
            cfg.y_max = 479;
            cfg.bus_shared = false;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }

        setPanel(&_panel_instance);
    }
};
