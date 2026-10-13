// Elecrow CrowPanel Advanced 10.1" ESP32-P4 HMI (SKU DHE04310D, PCB V1.2):
// ESP32-P4 + ESP32-C6 (WiFi, ESP-Hosted over SDIO), 1024x600 IPS MIPI-DSI
// panel (EK79007 controller), GT911 capacitive touch, stereo I2S speakers.
//
// Values from Elecrow's own V1.2 examples (github.com/Elecrow-RD/CrowPanel-
// Advanced-10.1inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen):
//   - example/V1.2/Arduino_Code/Lesson07-Turn_on_the_screen/board_config.h:
//     LCD reset 41, backlight 31 (PWM, active high), 1024x600, DPI 51MHz,
//     HPW/HBP/HFP 70/160/160, VPW/VBP/VFP 10/23/21; touch reset 40, INT 42,
//     I2C SDA 45 / SCL 46.
//   - esp_panel_board_custom_conf.h: controller EK79007, 2 DSI lanes at
//     1000 Mbps, RGB565, default EK79007 init sequence (its override is
//     commented out) -- which LovyanGFX's own Panel_EK79007 reproduces.
//   - Lesson07's .ino turns on the P4's LDO4 at 3.3V before initializing the
//     panel; init_impl() below does the same.
#pragma once

#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32p4/Bus_DSI.hpp>
#include <lgfx/v1/platforms/esp32p4/Panel_EK79007.hpp>
#include <esp_ldo_regulator.h>

namespace lgfx {
inline namespace v1 {
// Panel_EK79007 plus the deferred setInvert() every new DSI panel here needs:
// LGFX's Panel_FrameBufferBase::init() calls setInvert() before the panel's
// reset and before the DPI panel handle exists, and on DSI that sends a real
// DCS command that can hang boot solid (see pixelcadeusbtools' CLAUDE.md,
// "ESP32-P4 MIPI-DSI panels").
struct Panel_EK79007_Deferred : public Panel_EK79007 {
    void setInvert(bool invert) override {
        _invert = invert;
        if (_disp_panel_handle == nullptr) return;
        Panel_EK79007::setInvert(invert);
    }
};
}  // namespace v1
}  // namespace lgfx

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_DSI _bus_instance;
    lgfx::Panel_EK79007_Deferred _panel_instance;
    lgfx::Light_PWM _light_instance;
    lgfx::Touch_GT911 _touch_instance;

public:
    bool init_impl(bool use_reset, bool use_clear) {
#if !CONFIG_SPIRAM
#error "Elecrow CrowPanel P4 10.1 requires PSRAM enabled"
#endif
        // LDO4 -> 3.3V, as Elecrow's own screen example does before the panel.
        {
            static esp_ldo_channel_handle_t ldo4 = nullptr;
            if (!ldo4) {
                esp_ldo_channel_config_t ldo4_cfg = {};
                ldo4_cfg.chan_id = 4;
                ldo4_cfg.voltage_mv = 3300;
                esp_ldo_acquire_channel(&ldo4_cfg, &ldo4);
            }
        }

        auto bus_dsi = &_bus_instance;
        auto bus_cfg = bus_dsi->config();
        bus_cfg.bus_id = 0;
        bus_cfg.lane_num = 2;
        bus_cfg.lane_mbps = 1000;
        bus_cfg.ldo_chan_id = 3;
        bus_cfg.ldo_voltage_mv = 2500;
        bus_dsi->config(bus_cfg);
        if (!bus_dsi->init()) {
            return false;
        }

        auto p = &_panel_instance;
        {
            auto det = p->config_detail();
            det.dpi_freq_mhz = 51;
            det.hsync_pulse_width = 70;
            det.hsync_back_porch = 160;
            det.hsync_front_porch = 160;
            det.vsync_pulse_width = 10;
            det.vsync_back_porch = 23;
            det.vsync_front_porch = 21;
            p->config_detail(det);
        }
        setPanel(p);
        {
            auto cfg = p->config();
            cfg.memory_width = 1024;
            cfg.memory_height = 600;
            cfg.panel_width = 1024;
            cfg.panel_height = 600;
            cfg.readable = true;
            cfg.rgb_order = false;
            cfg.bus_shared = false;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.pin_cs = GPIO_NUM_NC;
            cfg.pin_rst = GPIO_NUM_41;
            p->config(cfg);
            p->setBus(bus_dsi);
        }
        {
            auto cfg = _touch_instance.config();
            cfg.i2c_port = I2C_NUM_0;
            cfg.pin_sda = 45;
            cfg.pin_scl = 46;
            cfg.pin_int = -1;  // polling, same caution as the other GT911 boards
            cfg.pin_rst = 40;
            cfg.freq = 400000;
            cfg.i2c_addr = 0x5D;
            cfg.x_min = 0;
            cfg.x_max = 1023;
            cfg.y_min = 0;
            cfg.y_max = 599;
            cfg.bus_shared = false;
            cfg.offset_rotation = 0;
            _touch_instance.config(cfg);
            p->setTouch(&_touch_instance);
        }
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = GPIO_NUM_31;
            cfg.freq = 30000;  // vendor BLIGHT_PWM_Hz
            cfg.pwm_channel = 7;
            cfg.offset = 0;
            cfg.invert = false;  // vendor BLIGHT_ON_LEVEL = 1
            _light_instance.config(cfg);
            p->setLight(&_light_instance);
        }

        return lgfx::LGFX_Device::init_impl(use_reset, use_clear);
    }

    lgfx::Bus_DSI* dsiBus() { return &_bus_instance; }
};
