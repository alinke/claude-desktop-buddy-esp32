// LovyanGFX configuration for the Waveshare ESP32-S3-Touch-LCD-3.49 **V2**
// (PCB "Rev1.1", case QC label "V2"): ESP32-S3R8 (8MB octal PSRAM, 16MB
// flash), 3.49" 172x640 bar LCD driven by an AXS15231B over QSPI, the
// AXS15231B's own capacitive touch on a second I2C bus, and a TCA9554 I/O
// expander for backlight enable / LCD reset / power hold.
//
// Pins come from Waveshare's V2 demo repo
// (github.com/waveshareteam/ESP32-S3-Touch-LCD-3.49-V2, Arduino/examples/
// 09_LVGL_V8_Test/user_config.h + lvgl_port.c):
//   LCD QSPI: CS 9, CLK 10, D0-D3 11/12/13/14, TE 21 (unused), SPI mode 3
//   backlight PWM: GPIO42 (active-low duty: Waveshare's "full" is duty 0)
//   I2C0 (expander/RTC/IMU): SDA 47, SCL 48. TCA9554 @0x20:
//     P0 touch INT (in), P1 backlight enable, P2/P3 IMU INT, P4 RTC INT,
//     P5 LCD reset, P6 SYS_EN (power hold on battery), P7 speaker amp
//     enable ("NS mode")
//   I2C1 (touch): SDA 17, SCL 18, AXS15231B touch @0x3B, no INT/RST pins
// V1 boards swap LCD_TE/LCD_RESET and LCD_BL/EXIO_INT (Waveshare's own V1->V2
// change list), so this config is V2-only. V1 was discontinued 2026-06-08.
//
// The AXS15231B's QSPI quirk drives the whole design here: in QSPI mode it
// ignores the row address (RASET). Waveshare's driver never sends it; a
// write that starts with RAMWR (0x2C) begins at row 0 and RAMWRC (0x3C)
// continues where the last one stopped. So the panel can only be written
// as whole frames, top to bottom, in its native 172x640 portrait layout --
// no partial rectangles and no hardware rotation (MADCTL MV would need
// RASET too). Everything is therefore drawn into a PSRAM framebuffer
// (Panel_FrameBufferBase does rotation in software, like the RGB/DSI
// boards) and display() streams the whole frame.
//
// The frame goes out through ESP-IDF's esp_lcd QSPI panel IO -- the exact
// path Waveshare's demos and every community project for this board use --
// not LovyanGFX's Bus_SPI. A first attempt that reused LovyanGFX's
// Panel_AMOLED QSPI framing (same 0x02/0x32 opcodes on paper) left the
// panel blank with the backlight on, and the AXS15231B's touch side
// answered with a fixed garbage frame until the display side was brought
// up through esp_lcd.
#pragma once

#include <LovyanGFX.hpp>
#include <Wire.h>
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "freertos/semphr.h"

namespace waveshare349 {

static constexpr int kI2cSda = 47;
static constexpr int kI2cScl = 48;
static constexpr uint8_t kExpanderAddr = 0x20; // TCA9554, A2-A0 = 000

static constexpr uint8_t kExioBacklightEn = 1u << 1;
static constexpr uint8_t kExioLcdReset = 1u << 5;
static constexpr uint8_t kExioSysEn = 1u << 6;
static constexpr uint8_t kExioAmpEn = 1u << 7; // "NS mode": speaker amp enable (Waveshare's audio demo drives it high)

static uint8_t expanderOut = 0;

static bool expanderWrite(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(kExpanderAddr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static void expanderSet(uint8_t bits, bool high) {
    expanderOut = high ? (expanderOut | bits) : (expanderOut & ~bits);
    expanderWrite(0x01, expanderOut); // output port
}

// Brings up the expander with the backlight off, then pulses the LCD reset
// line exactly as Waveshare's example_lcd_reset() does (high 30ms, low
// 250ms, high 30ms). SYS_EN goes high so a battery-powered unit stays on.
static bool expanderInit() {
    Wire.begin(kI2cSda, kI2cScl);
    Wire.setClock(400000);
    expanderOut = kExioLcdReset | kExioSysEn;
    bool ok = expanderWrite(0x01, expanderOut);
    // Configuration register: 1 = input. Outputs: backlight enable, LCD
    // reset, SYS_EN, speaker amp enable (off until a sound plays -- see
    // Waveshare349Audio.hpp). The INT lines stay inputs.
    ok = expanderWrite(0x03, (uint8_t)~(kExioBacklightEn | kExioLcdReset | kExioSysEn | kExioAmpEn)) && ok;
    delay(30);
    expanderSet(kExioLcdReset, false);
    delay(250);
    expanderSet(kExioLcdReset, true);
    delay(30);
    return ok;
}

} // namespace waveshare349

class Panel_AXS15231B : public lgfx::Panel_FrameBufferBase {
public:
    Panel_AXS15231B(void) {
        _cfg.memory_width = _cfg.panel_width = kWidth;
        _cfg.memory_height = _cfg.panel_height = kHeight;
        _write_depth = lgfx::color_depth_t::rgb565_2Byte;
        _read_depth = lgfx::color_depth_t::rgb565_2Byte;
        setBus(&_nullBus); // Panel_Device::init() needs a bus; output is esp_lcd
    }

    bool init(bool) override {
        if (!_frame && !allocFrame()) return false;
        if (!_io && !initLcd()) return false;
        return Panel_FrameBufferBase::init(false);
    }

    // Streams the whole framebuffer: CASET once, then RAMWR (0x2C) for the
    // first chunk and RAMWRC (0x3C) for the rest, through two internal DMA
    // buffers (PSRAM -> DMA buffer memcpy, as Waveshare's flush does).
    void display(uint_fast16_t, uint_fast16_t, uint_fast16_t w, uint_fast16_t h) override {
        if (!_io) return;
        if (_range_mod.empty() && (w == 0 || h == 0)) return;

        static const uint8_t caset[4] = {0, 0, (kWidth - 1) >> 8, (kWidth - 1) & 0xFF};
        sendCommand(0x2A, caset, sizeof(caset));
        const size_t total = kLineBytes * kHeight;
        size_t pos = 0;
        for (int i = 0; pos < total; ++i) {
            xSemaphoreTake(_freeBufs, portMAX_DELAY);
            size_t len = std::min(kChunkBytes, total - pos);
            uint8_t *buf = _dma[i & 1];
            memcpy(buf, _frame + pos, len);
            esp_lcd_panel_io_tx_color(_io, (0x32 << 24) | ((i == 0 ? 0x2C : 0x3C) << 8), buf, len);
            pos += len;
        }
        // Wait for both buffers to come back so the next frame (or a caller
        // touching _frame) can't race the DMA.
        xSemaphoreTake(_freeBufs, portMAX_DELAY);
        xSemaphoreTake(_freeBufs, portMAX_DELAY);
        xSemaphoreGive(_freeBufs);
        xSemaphoreGive(_freeBufs);

        _range_mod.top = INT16_MAX;
        _range_mod.left = INT16_MAX;
        _range_mod.right = 0;
        _range_mod.bottom = 0;
    }

private:
    static constexpr int kWidth = 172;
    static constexpr int kHeight = 640;
    static constexpr size_t kLineBytes = kWidth * 2;
    static constexpr size_t kChunkBytes = kLineBytes * 32; // 20 chunks per frame

    lgfx::Bus_NULL _nullBus;
    uint8_t *_frame = nullptr;
    uint8_t *_dma[2] = {nullptr, nullptr};
    SemaphoreHandle_t _freeBufs = nullptr;
    esp_lcd_panel_io_handle_t _io = nullptr;

    bool allocFrame() {
        _frame = (uint8_t *)heap_caps_calloc(kLineBytes * kHeight, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        auto lines = (uint8_t **)heap_caps_malloc(kHeight * sizeof(uint8_t *), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!_frame || !lines) return false;
        for (int y = 0; y < kHeight; ++y) lines[y] = _frame + y * kLineBytes;
        _lines_buffer = lines;
        return true;
    }

    static bool IRAM_ATTR onColorDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *ctx) {
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(static_cast<Panel_AXS15231B *>(ctx)->_freeBufs, &woken);
        return woken == pdTRUE;
    }

    void sendCommand(uint8_t cmd, const uint8_t *data, size_t len) {
        esp_lcd_panel_io_tx_param(_io, (0x02 << 24) | (cmd << 8), data, len);
    }

    bool initLcd() {
        _dma[0] = (uint8_t *)heap_caps_malloc(kChunkBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        _dma[1] = (uint8_t *)heap_caps_malloc(kChunkBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        _freeBufs = xSemaphoreCreateCounting(2, 2);
        if (!_dma[0] || !_dma[1] || !_freeBufs) return false;

        spi_bus_config_t bus = {};
        bus.sclk_io_num = 10;
        bus.data0_io_num = 11;
        bus.data1_io_num = 12;
        bus.data2_io_num = 13;
        bus.data3_io_num = 14;
        bus.max_transfer_sz = kChunkBytes;
        if (spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

        esp_lcd_panel_io_spi_config_t io = {};
        io.cs_gpio_num = 9;
        io.dc_gpio_num = -1;
        io.spi_mode = 3;
        io.pclk_hz = 40 * 1000 * 1000;
        io.trans_queue_depth = 4;
        io.on_color_trans_done = onColorDone;
        io.user_ctx = this;
        io.lcd_cmd_bits = 32;
        io.lcd_param_bits = 8;
        io.flags.quad_mode = true;
        if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io, &_io) != ESP_OK) {
            _io = nullptr;
            return false;
        }

        // Waveshare's V2 init: SLPOUT, MADCTL 0, COLMOD 16bpp (from its
        // driver's panel_init), then its own two-entry table (SLPOUT,
        // DISPON) -- the panel's OTP defaults are what V2 ships with.
        static const uint8_t madctl = 0x00, colmod = 0x55;
        sendCommand(0x11, nullptr, 0);
        delay(100);
        sendCommand(0x36, &madctl, 1);
        sendCommand(0x3A, &colmod, 1);
        sendCommand(0x11, nullptr, 0);
        delay(100);
        sendCommand(0x29, nullptr, 0);
        delay(100);
        return true;
    }
};

// AXS15231B touch, read with plain Wire1 (its own I2C bus, SDA 17 / SCL 18)
// using the 11-byte read command from Waveshare's example_lvgl_touch_cb,
// reading the full 32-byte reply as Waveshare does. Reply: [1] = touch count
// (1..4 while pressed), [2..3] = raw X (12 bits), [4..5] = raw Y. Raw X runs
// along the 640px axis, inverted; raw Y along the 172px axis -- Waveshare's
// unrotated mapping is x = rawY, y = 640 - rawX. getTouchRaw returns those
// native-panel coordinates.
class Touch_AXS15231B : public lgfx::ITouch {
public:
    bool init(void) override {
        Wire1.begin(17, 18);
        Wire1.setClock(300000); // Waveshare's scl_speed_hz
        Wire1.beginTransmission(kAddr);
        _inited = Wire1.endTransmission() == 0;
        return _inited;
    }

    void wakeup(void) override {}
    void sleep(void) override {}

    // While a finger is held, some polls come back with no touch (seen on
    // real hardware: every press showed up as 4-5 down/up pairs). A miss
    // within kHoldMs of the last real reading reports that reading again,
    // so a hold reads as one press and swipes stay continuous.
    uint_fast8_t getTouchRaw(lgfx::touch_point_t *tp, uint_fast8_t count) override {
        if (!_inited || count == 0) return 0;
        // main.cpp polls every loop (~700/s); polled that fast the controller
        // stops answering with touch data and returns filler bytes instead
        // (0x58/0x5E) until it's polled slower. Read it at most every
        // kPollMs and report the last result in between.
        uint32_t now = millis();
        if (now - _lastPollMs < kPollMs) {
            if (!_lastMs) return 0;
            tp[0] = _last;
            return 1;
        }
        _lastPollMs = now;
        if (readPoint(tp[0])) {
            _last = tp[0];
            _lastMs = millis();
            return 1;
        }
        if (_lastMs && millis() - _lastMs < kHoldMs) {
            tp[0] = _last;
            return 1;
        }
        _lastMs = 0;
        return 0;
    }

private:
    static constexpr uint8_t kAddr = 0x3B;
    static constexpr uint32_t kHoldMs = 60;
    static constexpr uint32_t kPollMs = 15;
    uint32_t _lastPollMs = 0;
    bool _inited = false;
    lgfx::touch_point_t _last = {};
    uint32_t _lastMs = 0;

    bool readPoint(lgfx::touch_point_t &tp) {
        static constexpr uint8_t cmd[11] = {0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00};
        uint8_t buf[32] = {0};
        Wire1.beginTransmission(kAddr);
        Wire1.write(cmd, sizeof(cmd));
        if (Wire1.endTransmission(false) != 0) return false;
        if (Wire1.requestFrom((int)kAddr, (int)sizeof(buf)) != (int)sizeof(buf)) return false;
        for (auto &b : buf) b = Wire1.read();
        if (buf[1] == 0 || buf[1] > 4) return false;

        int rawX = ((buf[2] & 0x0F) << 8) | buf[3];
        int rawY = ((buf[4] & 0x0F) << 8) | buf[5];
        tp.x = std::min(rawY, 171);
        tp.y = 639 - std::min(rawX, 639);
        tp.size = 1;
        tp.id = 0;
        return true;
    }
};

class LGFX : public lgfx::LGFX_Device {
    Panel_AXS15231B _panel_instance;
    lgfx::Light_PWM _light_instance;
    Touch_AXS15231B _touch_instance;

public:
    // Called from main.cpp's setup() (PIXELCADE_BOARD_WAVESHARE_3_49 block)
    // after init(). The panel is always a framebuffer; this just turns on
    // auto-display so every endWrite() pushes the frame.
    bool enableFrameBuffer(bool auto_display = false) {
        _panel_instance.setAutoDisplay(auto_display);
        return true;
    }

    LGFX(void) {
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = 42;
            cfg.invert = true; // duty 0 = full brightness on this board
            cfg.freq = 50000;
            cfg.pwm_channel = 1;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }
        {
            auto cfg = _touch_instance.config();
            cfg.x_min = 0;
            cfg.x_max = 171;
            cfg.y_min = 0;
            cfg.y_max = 639;
            cfg.offset_rotation = 0;
            cfg.bus_shared = false;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }
        setPanel(&_panel_instance);
    }

    bool init() {
        waveshare349::expanderInit();
        bool ok = lgfx::LGFX_Device::init();
        waveshare349::expanderSet(waveshare349::kExioBacklightEn, true);
        return ok;
    }
};
