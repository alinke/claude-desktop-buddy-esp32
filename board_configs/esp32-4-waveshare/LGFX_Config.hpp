// LovyanGFX panel configuration for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3
// (ESP32-P4, 4.3" 480x800 portrait MIPI-DSI IPS panel, ST7701 driver IC,
// GT911 capacitive touch). Display + touch only in this first pass -- this
// board also has a real speaker (ES8311+NS4150B), mic (ES7210), and camera
// connector, all deliberately out of scope for now (see esp32lcd's own
// history for m5stack-tab5/jc8012p4a1c/elecrow-crowpanel-ai-p4: every board
// in this catalog got its core display+touch confirmed first, audio as a
// separate later pass).
//
// Source: Waveshare's own official GitHub repo,
// github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3 (Apache 2.0) --
// examples/arduino/libraries/displays/{displays_config.h,
// waveshare_dsi_display.cpp,gt911.h} for every pin/timing value below, plus
// the checked-in schematic PDF for the touch RST trace (see the touch
// config block's own comment). Waveshare uses Arduino_GFX, not LovyanGFX --
// no existing LGFX reference for this exact board exists anywhere, unlike
// Guition10"/Tab5 which had at least a community LGFX port to start from.
// Panel_ST7701.hpp's own doc comment has the full init-table provenance.
//
// CONFIRMED on real hardware: boot splash renders correctly in landscape
// (BOOT_ROTATION=1, see platformio.ini), and a rigorous one-at-a-time
// corner+center tap test confirmed the touch mapping below (x_min/x_max/
// y_min/y_max/offset_rotation=0) is correct with NO axis inversion or
// mirroring needed -- unlike jc8012p4a1c's Guition10" bring-up, which had
// a real Y-axis inversion bug. One real bug WAS found and fixed during
// bring-up: Panel_ST7701.hpp's own header documents a setInvert()-before-
// reset hang that this DSI panel needed a driver-level workaround for.
#pragma once

#include <LovyanGFX.hpp>

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 requires CONFIG_IDF_TARGET_ESP32P4"
#endif

#include <lgfx/v1/platforms/esp32p4/Bus_DSI.hpp>
#include "Panel_ST7701.hpp"

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_DSI _bus_instance;
    lgfx::Panel_ST7701 _panel_instance;
    lgfx::Light_PWM _light_instance;
    lgfx::Touch_GT911 _touch_instance;

public:
    bool init_impl(bool use_reset, bool use_clear) {
#if !CONFIG_SPIRAM
#error "Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 requires PSRAM enabled (see platformio.ini board_build.arduino.memory_type)"
#endif

        auto bus_dsi = &_bus_instance;
        auto bus_cfg = bus_dsi->config();
        bus_cfg.bus_id = 0;
        // 2-lane, 500Mbps/lane -- Waveshare's own LCD43_LANE_RATE constant
        // (waveshare_dsi_display.cpp's num_data_lanes=2/lane_bit_rate_mbps_).
        // Unlike Guition10"'s JD9365 bring-up (which needed real bench
        // iteration between two conflicting vendor variants), there's only
        // one real, vendor-confirmed number here -- no reason to guess a
        // different starting value.
        bus_cfg.lane_num = 2;
        bus_cfg.lane_mbps = 500;
        // Same DSI PHY LDO channel/voltage as m5stack-tab5/jc8012p4a1c --
        // standard P4 MIPI-DSI PHY power rail, confirmed identical in
        // Waveshare's own waveshare_dsi_display.cpp (kDsiPhyLdoChannel=3,
        // kDsiPhyLdoVoltageMv=2500).
        bus_cfg.ldo_chan_id = 3;
        bus_cfg.ldo_voltage_mv = 2500;
        bus_dsi->config(bus_cfg);
        if (!bus_dsi->init()) {
            return false;
        }

        auto p = &_panel_instance;
        {
            // DPI/sync timing -- verbatim from waveshare_dsi_display.cpp's
            // kHsyncPulse/kHsyncBackPorch/kHsyncFrontPorch/kVsyncPulse/
            // kVsyncBackPorch/kVsyncFrontPorch constants and
            // LCD43_DPI_CLOCK (30000000UL -> 30MHz). Real, shipped BSP
            // values, not a datasheet guess.
            auto det = p->config_detail();
            det.dpi_freq_mhz = 30;
            det.hsync_pulse_width = 12;
            det.hsync_back_porch = 42;
            det.hsync_front_porch = 42;
            det.vsync_pulse_width = 8;
            det.vsync_back_porch = 2;
            det.vsync_front_porch = 60;
            p->config_detail(det);
        }
        setPanel(p);
        {
            auto cfg = p->config();
            // Panel memory is native portrait (480 wide x 800 tall) --
            // matches LCD43_WIDTH/LCD43_HEIGHT in Waveshare's own
            // displays_config.h. The physical product is landscape-mounted
            // -- confirmed on real hardware via BOOT_ROTATION=1 (see
            // platformio.ini), same portrait-memory/landscape-mount shape
            // as m5stack-tab5/jc8012p4a1c.
            cfg.memory_width = 480;
            cfg.memory_height = 800;
            cfg.panel_width = 480;
            cfg.panel_height = 800;
            cfg.readable = true;
            // CONFIRMED correct on real hardware -- boot splash renders
            // with correct colors, no R/B swap.
            cfg.rgb_order = true;
            cfg.bus_shared = false;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.pin_cs = GPIO_NUM_NC;
            // LCD43_LCD_RST from Waveshare's displays_config.h -- a plain
            // GPIO directly wired to the ESP32-P4, no I2C co-processor
            // involved (unlike elecrow-crowpanel-ai-p4's STC8H1KXX).
            cfg.pin_rst = GPIO_NUM_27;
            p->config(cfg);
            p->setBus(bus_dsi);
        }
        {
            // GT911 capacitive touch -- LovyanGFX's OWN built-in
            // Touch_GT911 driver (unlike Guition10"'s GSL3680, which
            // needed a custom Wire-based driver because LGFX's own driver
            // hangs on that chip -- no such issue is expected here, GT911
            // is already used successfully elsewhere in this catalog, e.g.
            // elecrow-crowpanel-ai-p4). Pins from Waveshare's
            // displays_config.h: LCD43_SDA=7, LCD43_SCL=8. Address 0x5D
            // (this catalog's existing default-to-0x5D convention for
            // GT911; Waveshare's own gt911.cpp probes 0x5D first, 0x14 as
            // fallback -- if 0x5D doesn't respond on real hardware, try
            // 0x14 next before assuming a wiring problem).
            //
            // RST/INT: confirmed via BOTH Waveshare's own gt911.h comment
            // ("GT911 is polled; the board does not drive INT or RST") AND
            // a direct schematic trace -- TP_RST IS physically linked to
            // GPIO23 via a 0-ohm resistor (R37) even though Waveshare's
            // own software chooses not to drive it, while TP_INT traces
            // only to a test point, no GPIO net at all. Driving the
            // confirmed-real RST pin here (rather than skipping it, since
            // an explicit reset before use is a small, safe difference
            // from the polling-only path Waveshare's own firmware takes)
            // while leaving INT at -1 (genuinely not wired -- setting a
            // real GPIO here would repeat the EXACT bug this project
            // already hit once on jc8048w550c: a wrong/floating pin_int
            // silently breaking every touch read, see that board's own
            // LGFX_Config.hpp history for the full story).
            auto cfg = _touch_instance.config();
            cfg.i2c_port = I2C_NUM_0;
            cfg.pin_sda = 7;
            cfg.pin_scl = 8;
            cfg.pin_int = -1;
            cfg.pin_rst = 23;
            cfg.freq = 400000;
            cfg.i2c_addr = 0x5D;
            cfg.x_min = 0;
            cfg.x_max = 479;
            cfg.y_min = 0;
            cfg.y_max = 799;
            cfg.bus_shared = false;
            // CONFIRMED on real hardware via the rigorous one-at-a-time
            // real-tap corner+center test (top-left/top-right/bottom-left/
            // bottom-right/center all landed correctly, no axis inversion
            // or mirror) -- this project's own Guition10" bring-up learned
            // the hard way that a looser "tap all corners together" test
            // can't reliably distinguish a correct mapping from an
            // inverted/mirrored one.
            cfg.offset_rotation = 0;
            _touch_instance.config(cfg);
            p->setTouch(&_touch_instance);
        }
        {
            // LCD43_BACKLIGHT from Waveshare's displays_config.h -- plain
            // GPIO PWM into a boost LED-driver IC, no I2C co-processor
            // (unlike elecrow-crowpanel-ai-p4's STC8H1KXX scheme).
            // Confirmed ACTIVE-LOW: Waveshare's own lcd43_backlight()
            // calls ledcOutputInvert(LCD43_BACKLIGHT, true) explicitly,
            // matching "the BSP's LEDC output_invert flag" per that
            // function's own comment. 5kHz/10-bit matches
            // LCD43_BACKLIGHT_FREQ/LCD43_BACKLIGHT_RESOLUTION exactly.
            auto cfg = _light_instance.config();
            cfg.pin_bl = GPIO_NUM_26;
            cfg.freq = 5000;
            cfg.pwm_channel = 7;
            cfg.offset = 0;
            cfg.invert = true;
            _light_instance.config(cfg);
            p->setLight(&_light_instance);
        }

        return lgfx::LGFX_Device::init_impl(use_reset, use_clear);
    }

    // Same accessor pattern as jc8012p4a1c/m5stack-tab5's own
    // LGFX_Config.hpp -- exposes the DSI bus's own release() so a future
    // power-off sequence (not yet implemented for this board -- no power
    // button/IP5306-style battery IC scope confirmed for this bring-up
    // pass) could release the MIPI DSI PHY LDO channel if one gets added
    // later.
    lgfx::Bus_DSI* dsiBus() { return &_bus_instance; }
};
