// LovyanGFX panel configuration for the 3.5" 320x480 ST7796 SPI panel with
// XPT2046 resistive touch (Sunton ESP32-3248S035R). Classic ESP32 (module
// silkscreen: "ESP32-32E"), NOT ESP32-S3 -- no native USB, no RGB-parallel
// LCD peripheral, CH340 USB-serial bridge (confirmed via ioreg: VID 0x1A86,
// PID 0x7523, same as this project's other CH340 boards). Panel silkscreen:
// "3.5" LCD Display ESP32-32E 320x480 Resistance Touch"; LCD glass itself
// marked "HSD035577C1".
//
// Unlike the 2.8" CYD board (esp32-2432s028r), where touch has its own
// dedicated SPI bus (SPI3_HOST), this board's XPT2046 shares the SAME SPI
// bus as the ST7796 display (SPI2_HOST, same MOSI/MISO/SCLK) -- only the CS
// pin differs. Confirmed from Sunton's own board definition.
//
// Pin values below come from the community board definition at
// https://github.com/rzeldent/platformio-espressif32-sunton/blob/main/esp32-3248S035R.json
// -- same source repo the other Sunton-family boards in this project were
// pulled from. Not yet verified on real hardware -- confirm rotation/mirror
// empirically once flashed (Sunton's own config sets DISPLAY_MIRROR_X=true,
// TOUCH_MIRROR_X=true; not yet translated into LovyanGFX's own mirror/
// rotation settings below, so the very first boot may show a horizontally
// flipped image -- adjust setRotation()/invert in main.cpp if so, same
// process already used to resolve the 7" board's rotation mapping).
#pragma once

#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_SPI _bus_instance;
    lgfx::Panel_ST7796 _panel_instance;
    lgfx::Light_PWM _light_instance;
    lgfx::Touch_XPT2046 _touch_instance;

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
            // Portrait-native (320x480), same convention as the 2.8" board
            // (esp32-2432s028r, portrait-native 240x320) -- landscape is
            // reached via BOOT_ROTATION=1 in platformio.ini, not by
            // pre-swapping these dimensions. The earlier "landscape-native"
            // experiment here was chasing a stale build-cache artifact (the
            // boot text was still showing the 5" board's string after
            // rebuilding without a clean, proving the binary hadn't
            // actually been recompiled) -- reverted.
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
            // Backlight -- Sunton's DISPLAY_BCKL=27.
            auto cfg = _light_instance.config();
            cfg.pin_bl = 27;
            cfg.invert = false;
            cfg.freq = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        {
            // XPT2046 resistive touch -- SAME SPI2_HOST bus as the display
            // (shared MOSI/MISO/SCLK), own CS/INT pins, per Sunton's source.
            auto cfg = _touch_instance.config();
            cfg.spi_host = SPI2_HOST;
            cfg.freq = 2000000;
            cfg.pin_sclk = 14;
            cfg.pin_mosi = 13;
            cfg.pin_miso = 12;
            cfg.pin_cs = 33;
            cfg.pin_int = 36;
            cfg.bus_shared = true;  // shares the display's bus, unlike the 2.8" board
            cfg.x_min = 0;
            cfg.x_max = 319;
            cfg.y_min = 0;
            cfg.y_max = 479;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }

        setPanel(&_panel_instance);
    }
};
