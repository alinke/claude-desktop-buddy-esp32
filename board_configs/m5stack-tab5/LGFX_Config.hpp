// LovyanGFX panel configuration for the M5Stack Tab5 (ESP32-P4, RISC-V,
// 5" 1280x720 MIPI-DSI IPS panel, PI4IOE5V6408-driven reset lines, I2C
// capacitive touch). Chip: https://docs.m5stack.com/en/core/Tab5
// Schematics: https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Tab5_Schematics_PDF.pdf
//
// UNLIKE every other board in this catalog, this one was built and
// compile-verified WITHOUT access to real hardware -- added overnight per
// explicit user request ("add support... tech docs are here... lmk any
// questions now before I go to bed"). Every other board's config in this
// project reached its current state only after several rounds of real-
// hardware fixes (see esp32-7-elecrow's PCA9557 story, esp32-5's touch INT pin,
// esp32-2's rotation, etc.) -- this one hasn't had that pass yet. It
// should be treated as a strong first draft, not confirmed-working, until
// verified on a real unit. See esp32lcd/README.md's Tab5 section for the
// specific things most likely to need adjustment.
//
// Two independent real sources went into this file, cross-checked against
// each other for consistency (they agreed on everything checkable):
//   1. LovyanGFX's OWN shipped example, src/lgfx_user/M5Tab5.hpp (fetched
//      directly from github.com/lovyan03/LovyanGFX at the version this
//      project's platformio.ini lib_deps resolves -- ^1.1.16, which
//      resolves to 1.2.27 as of this writing, confirmed to contain
//      esp32p4/Bus_DSI.hpp, Panel_ST7121/ST7123/ILI9881C.hpp, and
//      Touch_ST7123.hpp already). This is what init_impl()'s structure,
//      the PI4IOE register sequences, and the ST7123/ILI9881C
//      config_detail() timing values below are taken from directly.
//   2. M5Stack's own official driver, github.com/m5stack/M5GFX
//      (src/M5GFX.cpp, the `#elif defined(CONFIG_IDF_TARGET_ESP32P4)`
//      block) -- MORE complete than LovyanGFX's own example: it also
//      handles a THIRD panel/touch variant (ST7121, not just ST7123),
//      and determines which variant is present via the touch controller's
//      firmware version register FIRST (before ever bringing up the DSI
//      bus, since M5GFX's own comment notes ST7121 and ST7123 use
//      different DSI lane speeds and can't be told apart reliably by a
//      DSI-side ID read alone). That ordering and the ST7121-specific
//      config_detail() values below are ported from this source, since
//      LovyanGFX's own simpler example doesn't handle ST7121 at all.
//
// Per M5Stack's own docs, three hardware variants exist across production
// revisions: (a) ILI9881C panel + GT911 touch, (b) ST7121 panel + a
// Touch_ST7123-class touch IC (distinguished from (c) by firmware version
// register 1), (c) ST7123 panel + Touch_ST7123 (firmware version 3) --
// "ST7123 / ST7121, integrated display-touch driver as of October 2025"
// per M5Stack's docs, i.e. (c) is the current/newest production config,
// but not the only one in the wild. Rather than guess which this unit is,
// this file runs the SAME auto-detection M5Stack's own firmware does, so
// it should work regardless of which variant this specific unit has.
#pragma once

#include <LovyanGFX.hpp>

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "M5Stack Tab5 requires CONFIG_IDF_TARGET_ESP32P4"
#endif

#include <lgfx/v1/platforms/esp32p4/Bus_DSI.hpp>
#include <lgfx/v1/platforms/esp32p4/Panel_ILI9881C.hpp>
#include <lgfx/v1/platforms/esp32p4/Panel_ST7121.hpp>
#include <lgfx/v1/platforms/esp32p4/Panel_ST7123.hpp>
#include <lgfx/v1/platforms/esp32p4/Touch_ST7123.hpp>
#include <lgfx/v1/touch/Touch_GT911.hpp>

// Real I2C pins from M5Stack's own schematic/docs: SDA=GPIO31, SCL=GPIO32,
// touch INT=GPIO23 -- also doubles as the GT911-vs-alternate-address select
// pin (driven high during the PI4IOE dance below, matching both reference
// sources) before being released back to input/interrupt duty afterward.
static constexpr int_fast16_t tab5_i2c_port = I2C_NUM_1;

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_DSI _bus_instance;
    lgfx::Light_PWM _light_instance;
    // Which physical panel/touch variant this unit has can't be known at
    // compile time -- see file header. Heap-allocated once the probe below
    // determines which one responded, instead of a fixed static member
    // like every other (single-variant) board in this catalog.
    lgfx::Panel_Device *_panel_ptr = nullptr;
    lgfx::ITouch *_touch_ptr = nullptr;

    // M5Stack Tab5's two PI4IOE5V6408 I2C GPIO expanders -- one drives the
    // LCD reset + GT911-variant touch reset lines, the other configures
    // additional board I/O. Both must be sequenced through this exact
    // register dance before either the DSI panel or the touch controller
    // will respond at all. Addresses and register values taken verbatim
    // from both reference sources (identical in each).
    static constexpr uint_fast8_t pi4io1_i2c_addr = 0x43;
    static constexpr uint_fast8_t pi4io2_i2c_addr = 0x44;

    void i2cWriteRegisterArray(int_fast16_t i2c_port, uint_fast8_t i2c_addr, const uint8_t *reg_data_mask, uint32_t freq) {
        while (reg_data_mask[0] != 0xFF || reg_data_mask[1] != 0xFF || reg_data_mask[2] != 0xFF) {
            lgfx::i2c::writeRegister8(i2c_port, i2c_addr, reg_data_mask[0], reg_data_mask[1], reg_data_mask[2], freq);
            reg_data_mask += 3;
        }
    }

public:
    bool init_impl(bool use_reset, bool use_clear) {
        lgfx::pinMode(GPIO_NUM_23, lgfx::pin_mode_t::output); // TP INT
        lgfx::gpio_hi(GPIO_NUM_23);                           // select I2C addr (high=0x14 / low=0x5D on the GT911 variant)
        lgfx::i2c::init(tab5_i2c_port, GPIO_NUM_31, GPIO_NUM_32);

        bool expandersPresent = lgfx::i2c::readRegister8(tab5_i2c_port, pi4io1_i2c_addr, 0x01).has_value()
                              && lgfx::i2c::readRegister8(tab5_i2c_port, pi4io2_i2c_addr, 0x01).has_value();
        if (!expandersPresent) {
            return false;
        }

        static constexpr const uint8_t reg_data_io1_1[] = {
            0x03, 0b01111111, 0, // PI4IO_REG_IO_DIR
            0x05, 0b01000110, 0, // PI4IO_REG_OUT_SET (bit4=LCD Reset, bit5=GT911 TouchReset -- LOW)
            0x07, 0b00000000, 0, // PI4IO_REG_OUT_H_IM
            0x0D, 0b01111111, 0, // PI4IO_REG_PULL_SEL
            0x0B, 0b01111111, 0, // PI4IO_REG_PULL_EN
            0xFF, 0xFF, 0xFF,
        };
        static constexpr const uint8_t reg_data_io1_2[] = {
            0x05, 0b01110110, 0, // PI4IO_REG_OUT_SET (bit4=LCD Reset, bit5=GT911 TouchReset -- HIGH)
            0xFF, 0xFF, 0xFF,
        };
        static constexpr const uint8_t reg_data_io2[] = {
            0x03, 0b10111001, 0, // PI4IO_REG_IO_DIR
            0x07, 0b00000110, 0, // PI4IO_REG_OUT_H_IM
            0x0D, 0b10111001, 0, // PI4IO_REG_PULL_SEL
            0x0B, 0b11111001, 0, // PI4IO_REG_PULL_EN
            0x09, 0b01000000, 0, // PI4IO_REG_IN_DEF_STA
            0x11, 0b10111111, 0, // PI4IO_REG_INT_MASK
            0x05, 0b10001001, 0, // PI4IO_REG_OUT_SET
            0xFF, 0xFF, 0xFF,
        };
        i2cWriteRegisterArray(tab5_i2c_port, pi4io1_i2c_addr, reg_data_io1_1, 100000);
        i2cWriteRegisterArray(tab5_i2c_port, pi4io2_i2c_addr, reg_data_io2, 100000);
        lgfx::delay(10);
        i2cWriteRegisterArray(tab5_i2c_port, pi4io1_i2c_addr, reg_data_io1_2, 100000);
        lgfx::pinMode(GPIO_NUM_23, lgfx::pin_mode_t::input); // release TP INT back to interrupt duty
        lgfx::delay(100);

#if !CONFIG_SPIRAM
#error "M5Stack Tab5 requires PSRAM enabled (see platformio.ini board_build.arduino.memory_type)"
#endif

        // Determine panel/touch variant via the touch controller's firmware
        // version register FIRST, before bringing up the DSI bus at all --
        // per M5GFX's own source comment, ST7121 and ST7123 use different
        // DSI lane speeds and can't be reliably told apart by a DSI-side ID
        // read, so the touch side is the primary signal; a DSI ID read is
        // only used as the fallback to recognize the third (ILI9881C+GT911)
        // variant, which has no equivalent touch-firmware-version register.
        bool hitST7121 = false;
        bool hitST7123 = false;
        bool foundGT911 = false;
        for (int i = 0; i < 60; ++i) {
            uint8_t fwVersion = 0;
            uint8_t fwReg[2] = {0, 0};
            if (lgfx::i2c::transactionWriteRead(tab5_i2c_port, lgfx::Touch_ST7123::default_addr, fwReg, sizeof(fwReg), &fwVersion, 1, 100000).has_value()) {
                if (fwVersion == 1) {
                    hitST7121 = true;
                    break;
                }
                if (fwVersion == 3) {
                    hitST7123 = true;
                    break;
                }
            } else if (lgfx::i2c::beginTransaction(tab5_i2c_port, lgfx::Touch_GT911::default_addr_1, 100000, false).has_value()
                    && lgfx::i2c::endTransaction(tab5_i2c_port).has_value()) {
                foundGT911 = true;
                break;
            }
            lgfx::delay(10);
        }

        auto bus_dsi = &_bus_instance;
        auto bus_cfg = bus_dsi->config();
        bus_cfg.bus_id = 0;
        bus_cfg.lane_num = 2;
        bus_cfg.lane_mbps = hitST7121 ? 900 : 1040; // ST7121 runs its DSI lanes slower than ST7123/ILI9881C
        bus_cfg.ldo_chan_id = 3;
        bus_cfg.ldo_voltage_mv = 2500;
        bus_dsi->config(bus_cfg);
        if (!bus_dsi->init()) {
            return false;
        }

        bool hitILI9881 = false;
        if (!hitST7121 && !hitST7123 && !foundGT911) {
            // Touch-side probe was inconclusive -- fall back to a DSI-side
            // ID read, which only reliably distinguishes the ILI9881C
            // variant (see comment above on why ST7121/ST7123 aren't safe
            // to tell apart this way).
            lgfx::delay(80);
            uint8_t id[3] = {0, 0, 0};
            static constexpr uint8_t params_page1[] = {0x98, 0x81, 0x01};
            bus_dsi->writeParams(0xFF, params_page1, 3);
            bus_dsi->readParams(0x00, &id[0], 1);
            bus_dsi->readParams(0x01, &id[1], 1);
            bus_dsi->readParams(0x02, &id[2], 1);
            if (id[0] == 0x98 && id[1] == 0x81) {
                static constexpr uint8_t params_page0[] = {0x98, 0x81, 0x00};
                bus_dsi->writeParams(0xFF, params_page0, 3);
                hitILI9881 = true;
            }
        }

        if (hitILI9881) {
            auto p = new lgfx::Panel_ILI9881C();
            _panel_ptr = p;
            _touch_ptr = static_cast<lgfx::ITouch *>(new lgfx::Touch_GT911());
            auto det = p->config_detail();
            det.dpi_freq_mhz = 80;
            det.hsync_back_porch = 140;
            det.hsync_pulse_width = 40;
            det.hsync_front_porch = 40;
            det.vsync_back_porch = 20;
            det.vsync_pulse_width = 4;
            det.vsync_front_porch = 20;
            p->config_detail(det);
        } else if (hitST7121) {
            auto p = new lgfx::Panel_ST7121();
            _panel_ptr = p;
            _touch_ptr = static_cast<lgfx::ITouch *>(new lgfx::Touch_ST7123());
            auto det = p->config_detail();
            det.dpi_freq_mhz = 70;
            det.hsync_back_porch = 40;
            det.hsync_pulse_width = 2;
            det.hsync_front_porch = 40;
            det.vsync_back_porch = 24;
            det.vsync_pulse_width = 20;
            det.vsync_front_porch = 200;
            p->config_detail(det);
        } else {
            // Default to ST7123 -- current/newest production panel per
            // M5Stack's own docs, and the touch-FW-version probe's own
            // fallback target if nothing else matched (hitST7123 true, or
            // everything was inconclusive and this is still the most
            // likely variant for a unit bought recently).
            auto p = new lgfx::Panel_ST7123();
            _panel_ptr = p;
            _touch_ptr = static_cast<lgfx::ITouch *>(new lgfx::Touch_ST7123());
            auto det = p->config_detail();
            det.dpi_freq_mhz = 80;
            det.hsync_back_porch = 40;
            det.hsync_pulse_width = 2;
            det.hsync_front_porch = 40;
            // back + pulse == 10 -- if out of sync, the display position
            // shifts vertically (per M5GFX's own source comment).
            det.vsync_back_porch = 8;
            det.vsync_pulse_width = 2;
            // Reducing the front porch stops the touch panel from working
            // (per M5GFX's own source comment).
            det.vsync_front_porch = 220;
            p->config_detail(det);
        }

        setPanel(_panel_ptr);
        {
            auto p = _panel_ptr;
            auto cfg = p->config();
            // Panel memory is native PORTRAIT (720 wide x 1280 tall) even
            // though the product is marketed/mounted as a 1280x720
            // landscape 5" panel -- matches this catalog's existing SPI-
            // board convention (esp32-oled, esp32-3) of a portrait-
            // native memory needing a rotation value to present landscape.
            // UNVERIFIED which LandscapeRotation value is actually correct
            // for this panel -- see catalog.go's Chip field doc comment on
            // why this can't be assumed from "shape" alone; needs real
            // hardware.
            cfg.memory_width = 720;
            cfg.memory_height = 1280;
            cfg.panel_width = 720;
            cfg.panel_height = 1280;
            cfg.readable = true;
            cfg.rgb_order = true;
            cfg.bus_shared = false;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.pin_cs = GPIO_NUM_NC;
            cfg.pin_rst = GPIO_NUM_NC;
            p->config(cfg);
            p->setBus(bus_dsi);
        }
        if (_touch_ptr) {
            auto t = _touch_ptr;
            auto cfg = t->config();
            cfg.pin_rst = -1;
            cfg.pin_sda = GPIO_NUM_31;
            cfg.pin_scl = GPIO_NUM_32;
            cfg.pin_int = GPIO_NUM_23;
            cfg.freq = 400000;
            cfg.x_min = 0;
            cfg.x_max = 719;
            cfg.y_min = 0;
            cfg.y_max = 1279;
            cfg.i2c_port = tab5_i2c_port;
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            t->config(cfg);
            _panel_ptr->setTouch(t);
        }
        {
            // Backlight pin/freq/channel from both reference sources
            // (identical in each).
            auto cfg = _light_instance.config();
            cfg.pin_bl = GPIO_NUM_22;
            cfg.freq = 44100;
            cfg.pwm_channel = 7;
            cfg.offset = 0;
            cfg.invert = false;
            _light_instance.config(cfg);
            _panel_ptr->setLight(&_light_instance);
        }

        return lgfx::LGFX_Device::init_impl(use_reset, use_clear);
    }
};
