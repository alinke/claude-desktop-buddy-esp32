// LovyanGFX panel configuration for the LilyGO T-Display-S3-AMOLED (1.91",
// RM67162 AMOLED, QSPI), specifically the H712 "V2.0 Soldered Pin" SKU --
// no touch hardware on this unit.
//
// Pin values are cross-checked against two independent official sources
// that agree exactly on every pin:
//   - LilyGO's own board library: BOARD_AMOLED_191 in
//     https://github.com/Xinyuan-LilyGO/LilyGo-AMOLED-Series (src/LilyGo_AMOLED.h)
//   - LovyanGFX's own bundled example for this exact board:
//     https://github.com/lovyan03/LovyanGFX/blob/master/src/lgfx_user/Lilygo_T_Display_S3_AMOLED.hpp
//
// Confirmed on real hardware (this SKU): LovyanGFX's bundled example claims
// GPIO9 must be driven high as a "power enable" output. That turned out to
// be wrong for this board -- removing it made no difference. The real fix,
// found by reading LilyGO's own LilyGo_AMOLED.cpp rather than the LovyanGFX
// example: GPIO38 is the actual board-level power-enable pin (`PMICEnPins`
// in their BOARD_AMOLED_191 struct), and LovyanGFX's example doesn't touch
// it at all -- it's outside a display driver's scope, since it's board
// wiring, not a panel pin. Without it, `lcd.init()` reports success (it has
// no way to detect an unpowered panel) but the screen stays black. GPIO9 is
// left unconfigured here, consistent with LilyGO's own docs calling it `TE`
// (an input), not something this firmware needs to drive.
#pragma once

#define LGFX_USE_QSPI
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
    lgfx::Bus_SPI _bus_instance;
    lgfx::Panel_RM67162 _panel_instance;

public:
    // RM67162 hardware limitation (noted in LovyanGFX's own example): any
    // drawn area's x-coordinate and width must both be even, or the command
    // is ignored. drawJpg() always draws at x=0 here (even), but the JPEG's
    // pixel width must also be even -- true for this panel's native 240 or
    // 536 width, so only a concern if Go ever scales to an odd width. To
    // sidestep this entirely, framebuffer mode is enabled in setup() (see
    // main.cpp), trading a little performance for not having to think about
    // it -- fine for this design's event-driven, non-realtime update rate.
    bool enableFrameBuffer(bool auto_display = false) {
        if (_panel_instance.initPanelFb()) {
            auto fbPanel = _panel_instance.getPanelFb();
            if (fbPanel) {
                fbPanel->setBus(&_bus_instance);
                fbPanel->setAutoDisplay(auto_display);
                setPanel(fbPanel);
                return true;
            }
        }
        return false;
    }

    LGFX(void) {
        {
            auto cfg = _bus_instance.config();

            cfg.freq_write = 75000000;
            cfg.freq_read = 20000000;

            cfg.pin_sclk = 47;
            cfg.pin_io0 = 18;
            cfg.pin_io1 = 7;
            cfg.pin_io2 = 48;
            cfg.pin_io3 = 5;

            cfg.spi_host = SPI2_HOST;
            cfg.spi_mode = SPI_MODE0;
            cfg.dma_channel = SPI_DMA_CH_AUTO;

            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        {
            auto cfg = _panel_instance.config();
            cfg.pin_rst = 17;
            cfg.pin_cs = 6;
            cfg.panel_width = 240;
            cfg.panel_height = 536;
            cfg.readable = true;
            _panel_instance.config(cfg);
        }

        setPanel(&_panel_instance);
    }

    bool init() {
        // Board-level power-enable pin (GPIO38, LilyGO's `PMICEnPins`) --
        // see file header comment. Required or the panel stays unpowered.
        lgfx::pinMode(38, lgfx::pin_mode_t::output);
        lgfx::gpio_hi(38);
        delay(10);  // brief settle before driving the display bus

        return lgfx::LGFX_Device::init();
    }
};

// No touch hardware on this SKU (H712, "V2.0 Soldered Pin") -- confirmed
// against LilyGO's own touch/non-touch variant listing. main.cpp's
// pollTouch()/lcd.getTouch() calls are safe no-ops here regardless (verified
// against LovyanGFX source: Panel_Device::getTouchRaw returns 0 immediately
// if no ITouch instance was configured), so main.cpp needs no board-specific
// guarding for this.
