// LovyanGFX panel configuration for the Guition JC8012P4A1C_I_W_Y1
// (ESP32-P4, 10.1" 800x1280 MIPI-DSI IPS panel, JD9365 driver IC, GSL3680
// capacitive touch, ES8311 codec + NS4150 speaker amp -- see
// JC8012Audio.hpp for the audio side).
//
// Sources (both real, not guessed -- user-confirmed as the correct
// reference for this exact SKU):
//   1. github.com/sukesh-ak/JC8012P4A1-GUITION-ESP32-P4_ESP32-C6 --
//      specifically 1-Demo/arduino-examples/esp32p4_lvgl_v8/{pins_config.h,
//      src/lcd/{esp_lcd_jd9365.h,jd9365_lcd.cpp}} for GPIO pins and the
//      exact DSI bus/DPI timing values below (lane count, lane bit rate,
//      dpi clock, all porches) -- these are Espressif's own official
//      esp_lcd_jd9365 component values for this board, not derived from a
//      generic JD9365 datasheet.
//   2. Guition's own official spec sheet (2-Specification/*.pdf in that
//      same repo) -- confirms JD9365 driver IC, 800x1280, and that the
//      physical product is a LANDSCAPE-mounted 10.1" tablet despite the
//      panel's native (portrait-shaped) pixel memory -- same "portrait
//      native memory, landscape mounting" pattern this catalog's other
//      SPI/DSI boards (esp32-oled, esp32-3, m5stack-tab5) already have.
//
// CONFIRMED WORKING on a real physical unit (boot splash: correct
// gold->orange->pink gradient, clean black background, crisp text,
// correct landscape orientation at BOOT_ROTATION=1). Two real, non-obvious
// findings from that bring-up, both now reflected below:
//   1. This board turned out to need a DIFFERENT register table AND DPI
//      timing than the community repo's "Old Panel" sequence originally
//      ported here -- see Panel_JD9365.hpp's own file header for the full
//      "Old Panel vs New Panel" story. dpi_freq_mhz/vsync porches below
//      are the "New Panel" (V4.4/P4V3x) values.
//   2. rgb_order=true (RGB) is correct here -- an early rgb_order=false
//      test looked plausible at first only because that comparison photo
//      was ALSO contaminated by IPS off-axis viewing-angle color shift (a
//      bare bench panel photographed at an angle); a clean straight-on
//      photo showed true is actually right and false produces a clear
//      R/B channel swap (red-heavy source colors rendering blue-dominant).
//
// Still NOT wired up in this file:
//   - LandscapeRotation/PortraitRotation exact values for pixelweb's
//     catalog.go -- BOOT_ROTATION=1 (platformio.ini) is confirmed correct
//     for the boot splash, which is a reasonable starting point but worth
//     double-checking once real Sidekick card content (not just the boot
//     splash) is rendering, in case orientation-sensitive content reveals
//     a mirroring difference the boot splash's symmetric-ish layout
//     wouldn't expose.
//   - Touch (GSL3680) rotation/mirror calibration -- wired up below via a
//     CUSTOM driver, Touch_GSL3680_Wire (see that file's own header for
//     why: LovyanGFX's own Touch_GSLx680 driver HANGS SOLID on this
//     board's chip, a real bug in LGFX's i2c:: wrapper, not this chip/
//     board/reset sequence -- confirmed via direct comparison against
//     plain Arduino Wire calls, which handle the exact same condition
//     cleanly). offset_rotation-equivalent handling is a starting guess,
//     NOT yet confirmed against real taps. Guition's own vendor touch
//     code (gsl3680_touch.cpp in the reference repo) uses
//     mirror_x=true+mirror_y=true+swap_xy=false for this board's
//     rotation=1 (landscape, matching BOOT_ROTATION=1 here) -- this
//     needs real-hardware tap testing (all four corners + center) to
//     confirm, same as every other board's touch rotation in this
//     catalog was ultimately confirmed.
#pragma once

#include <LovyanGFX.hpp>

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "Guition JC8012P4A1C_I_W_Y1 requires CONFIG_IDF_TARGET_ESP32P4"
#endif

#include <lgfx/v1/platforms/esp32p4/Bus_DSI.hpp>
#include "Panel_JD9365.hpp"
#include "Touch_GSL3680_Wire.hpp"

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_DSI _bus_instance;
    lgfx::Panel_JD9365 _panel_instance;
    lgfx::Light_PWM _light_instance;
    Touch_GSL3680_Wire _touch_instance;

public:
    bool init_impl(bool use_reset, bool use_clear) {
#if !CONFIG_SPIRAM
#error "Guition JC8012P4A1C_I_W_Y1 requires PSRAM enabled (see platformio.ini board_build.arduino.memory_type)"
#endif

        auto bus_dsi = &_bus_instance;
        auto bus_cfg = bus_dsi->config();
        bus_cfg.bus_id = 0;
        bus_cfg.lane_num = 2;
        // esp_lcd_jd9365.h's own JD9365_PANEL_BUS_DSI_2CH_CONFIG() value --
        // confirmed the SAME (1500) in both the Old Panel and New Panel
        // vendor header variants, so this wasn't the source of the garbled-
        // image finding that led to Panel_JD9365.hpp's New Panel rewrite
        // (see that file's header). A real-hardware experiment dropping
        // this to 1000 (matching m5stack-tab5's own lane_mbps range
        // elsewhere in this codebase) made things WORSE -- full black
        // screen instead of a garbled-but-visible image -- consistent with
        // 1500 being correct and the actual problem being the register
        // table, not this value.
        bus_cfg.lane_mbps = 1500;
        bus_cfg.ldo_chan_id = 3;
        bus_cfg.ldo_voltage_mv = 2500;
        bus_dsi->config(bus_cfg);
        if (!bus_dsi->init()) {
            return false;
        }

        auto p = &_panel_instance;
        {
            // Timing values from the "New Panel" (V4.4/P4V3x) variant's
            // esp_lcd_jd9365.h -- see Panel_JD9365.hpp's own file header
            // for why this board has two genuinely different real
            // revisions, and why New Panel (not Old Panel, tried first
            // and confirmed producing a garbled image on real hardware)
            // is used here. Notably dpi_freq_mhz and every vsync porch
            // differ from the Old Panel values despite the macro name
            // (JD9365_800_1280_PANEL_60HZ_DPI_CONFIG) staying the same
            // across both vendor header variants.
            auto det = p->config_detail();
#ifdef PIXELCADE_JC8012_OLD_PANEL
            // Old Panel (SKU 10153001, "_Y"): the community repo's
            // JD9365_800_1280_PANEL_60HZ_DPI_CONFIG values.
            det.dpi_freq_mhz = 60;
            det.hsync_pulse_width = 20;
            det.hsync_back_porch = 20;
            det.hsync_front_porch = 40;
            det.vsync_pulse_width = 4;
            det.vsync_back_porch = 8;
            det.vsync_front_porch = 20;
#else
            det.dpi_freq_mhz = 80;
            det.hsync_pulse_width = 20;
            det.hsync_back_porch = 20;
            det.hsync_front_porch = 40;
            det.vsync_pulse_width = 12;
            det.vsync_back_porch = 4;
            det.vsync_front_porch = 30;
#endif
            p->config_detail(det);
        }
        setPanel(p);
        {
            auto cfg = p->config();
            // Panel memory is native portrait (800 wide x 1280 tall) --
            // matches LCD_H_RES/LCD_V_RES in the reference repo's own
            // pins_config.h/esp_lcd_jd9365.h. The physical product is a
            // landscape-mounted 10.1" tablet (per Guition's own spec
            // sheet product photo) -- same "portrait-native memory needs
            // a rotation value to present landscape" pattern as this
            // catalog's other SPI/DSI boards. Which LandscapeRotation
            // value is actually correct is UNVERIFIED -- see file header.
            cfg.memory_width = 800;
            cfg.memory_height = 1280;
            cfg.panel_width = 800;
            cfg.panel_height = 1280;
            cfg.readable = true;
            // EXPERIMENTAL -- the first true-vs-false comparison was
            // contaminated by IPS off-axis viewing-angle color shift (a
            // bare bench panel photographed at an angle, unrelated to this
            // setting) -- confirmed once a straight-on, correctly-oriented
            // photo showed background reliably black either way. With that
            // clean reference, false (BGR) still showed clearly
            // blue-dominant colors on a red-heavy source gradient (gold/
            // orange/pink, always R=255) -- textbook R/B channel swap.
            // Reverting to true (RGB).
            cfg.rgb_order = true;
            cfg.bus_shared = false;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = 0;
            cfg.pin_cs = GPIO_NUM_NC;
            // LCD_RST from the reference repo's pins_config.h -- a plain
            // GPIO directly wired to this board's ESP32-P4, NOT behind an
            // I2C GPIO expander like m5stack-tab5's PI4IOE dance.
            cfg.pin_rst = GPIO_NUM_27;
            p->config(cfg);
            p->setBus(bus_dsi);
        }
        {
            // Custom Wire-based GSL3680 driver -- see Touch_GSL3680_Wire.hpp's
            // own file header for why this exists instead of LGFX's own
            // Touch_GSLx680 (that driver hangs solid on this board's chip;
            // this one doesn't, confirmed via real-hardware A/B testing of
            // the exact same register writes). Pins from the reference
            // repo's pins_config.h: TP_I2C_SDA=7, TP_I2C_SCL=8, TP_RST=22,
            // TP_INT=21. The RST+INT reset dance and full firmware-blob
            // upload (JC8012_GSL3680_FW.hpp) happen inside
            // Touch_GSL3680_Wire::init(), called by
            // LGFX_Device::init_impl() below via p->setTouch(t).
            auto t = &_touch_instance;
            t->setPins(GPIO_NUM_7, GPIO_NUM_8, GPIO_NUM_22, GPIO_NUM_21);
            // x_max/y_max here matter beyond documentation: Panel_Device::
            // setTouch() calls touchCalibrate() immediately using
            // ITouch::config()'s x_min/x_max/y_min/y_max to build the
            // raw->screen coordinate transform, so these must be set via
            // config() BEFORE setTouch() -- leaving them at ITouch's
            // built-in default (0..3600) would silently miscalibrate every
            // touch even though this custom driver's own getTouchRaw()
            // never reads _cfg itself.
            auto cfg = t->config();
            cfg.x_min = 0;
            cfg.x_max = 800;
            cfg.y_min = 0;
            cfg.y_max = 1280;
            // UNVERIFIED -- see file header. Starting guess; confirm
            // against real taps (all four corners + center) once flashed.
            cfg.offset_rotation = 0;
            t->config(cfg);
            p->setTouch(t);
        }
        {
            // LCD_LED from the reference repo's pins_config.h. Active
            // level/PWM freq/channel not confirmed from a real reference
            // (jd9365_lcd.cpp drives it as a plain digital GPIO, not PWM,
            // with EXAMPLE_LCD_BK_LIGHT_ON_LEVEL=1 -- active high) --
            // using Light_PWM here for brightness control like every
            // other board in this catalog rather than a plain on/off
            // digital pin; confirm on real hardware that PWM doesn't
            // produce visible flicker/whine on this specific backlight
            // driver circuit.
            auto cfg = _light_instance.config();
            cfg.pin_bl = GPIO_NUM_23;
            cfg.freq = 44100;
            cfg.pwm_channel = 7;
            cfg.offset = 0;
            cfg.invert = false;
            _light_instance.config(cfg);
            p->setLight(&_light_instance);
        }

        return lgfx::LGFX_Device::init_impl(use_reset, use_clear);
    }

    // Exposes the DSI bus's own release() (see Bus_DSI.hpp -- already tears
    // down the panel IO + DSI bus AND calls esp_ldo_release_channel() on the
    // MIPI DSI PHY power rail acquired in init_impl() above) so main.cpp's
    // power-off sequence can drop this rail before esp_deep_sleep_start().
    // Same accessor pattern as rgbBus()/rgbPanel() on the RGB-parallel
    // boards' own LGFX_Config.hpp. This board's battery (via the IP5306
    // power IC) has no software shutdown pin wired on this schematic -- the
    // real off happens via the IP5306's own automatic light-load shutoff a
    // short while after total draw drops low enough, which requires this
    // LDO released (in addition to lcd.sleep() and deep sleep) since it
    // stays powered independently of the panel/backlight state otherwise.
    lgfx::Bus_DSI* dsiBus() { return &_bus_instance; }
};
