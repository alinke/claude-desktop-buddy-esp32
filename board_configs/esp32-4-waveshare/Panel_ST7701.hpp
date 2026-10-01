/*----------------------------------------------------------------------------/
  Panel_ST7701 -- LovyanGFX Panel_DSI driver for the ST7701 MIPI-DSI panel
  driver IC, as used on the Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3
  (480x800 portrait IPS).

  This project's pinned LovyanGFX version has no ST7701 DSI support --
  confirmed by grepping the actual installed library (only
  Panel_EK79007/Panel_ILI9881C/Panel_LT8912B/Panel_ST7121/Panel_ST7123 ship
  for the esp32p4 platform). Same gap this project already hit once for
  JD9365 on the Guition 10.1" board -- see Panel_JD9365.hpp (same directory
  tree, board_configs/jc8012p4a1c/) for that precedent; this file follows
  its exact structural pattern (list-of-commands, getInitParams/
  getInitDelay indexed by listno).

  Command table transcribed VERBATIM (byte-for-byte, only regrouped into
  LovyanGFX's listno-indexed chunks -- values/order unchanged) from
  Waveshare's own official GitHub repo,
  github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3, examples/arduino/
  libraries/displays/displays_config.h's lcd43_st7701_init[] array (sourced
  there from "bsp/esp32_p4_wifi6_touch_lcd_4_3 at e843306" per that file's
  own comment) -- i.e. Waveshare's real, shipped BSP init sequence for this
  exact board, not a generic ST7701 datasheet guess. DSI bus/DPI timing
  values (Panel_JD9365.hpp-equivalent config_detail fields, this board's
  own LGFX_Config.hpp) came from the same repo's
  waveshare_dsi_display.cpp.

  NOT YET CONFIRMED on real hardware -- this transcription has not been
  bench-tested at the time this file was written. If the boot splash comes
  up garbled/banded (not a clean render), this is the first thing to
  re-verify against the source file above, the same way Panel_JD9365.hpp's
  own header documents a real Old-Panel/New-Panel register-table mismatch
  that looked superficially plausible (DSI link alive, wrong image) before
  being root-caused.
/----------------------------------------------------------------------------*/
#pragma once

// Full explicit path, not a bare quoted include -- same reasoning as
// Panel_JD9365.hpp's own header comment: this file lives in this board's
// own board_configs directory, not alongside Panel_DSI.hpp/Bus_DSI.hpp
// inside the LovyanGFX library tree, so a bare #include "Panel_DSI.hpp"
// wouldn't resolve here.
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>
#if SOC_MIPI_DSI_SUPPORTED

namespace lgfx
{
 inline namespace v1
 {
//----------------------------------------------------------------------------

  struct Panel_ST7701 : public Panel_DSI
  {
  public:
    // Real-hardware bring-up finding: Panel_FrameBufferBase::init() (the
    // base class LGFX_Device::init_impl() ultimately calls) invokes
    // setInvert() UNCONDITIONALLY at its very top -- BEFORE Panel_DSI::init()
    // has created the DPI video panel (_disp_panel_handle is still nullptr
    // at that point) and BEFORE this panel's own physical reset pulse has
    // even happened. For a DSI panel, setInvert() sends a REAL command
    // (INVON=0x21 or INVOFF=0x20) over the DBI command channel -- meaning
    // LGFX was sending a live command to this panel before resetting it and
    // before its own DPI panel object existed, which Waveshare's own
    // reference sequence (esp_lcd_new_dsi_bus -> esp_lcd_new_panel_io_dbi ->
    // esp_lcd_new_panel_dpi -> THEN reset -> THEN init commands) never
    // does -- their reset always happens first, and no command is sent
    // before it. Suspected root cause of a real, reproducible hang first
    // seen when sending register 0xB2 on page 0x11 (bisected down from the
    // full 41-command table) -- an out-of-sequence command this early could
    // leave the panel's own DSI receiver state machine in a condition that
    // only surfaces as a hang partway through the real init sequence, not
    // as an immediate, obvious failure. Guarding here so setInvert() is a
    // no-op until _disp_panel_handle actually exists (i.e., until
    // init_dpi() has run), which naturally defers the base class's early
    // call to a harmless no-op instead of a live wire write.
    void setInvert(bool invert) override
    {
      _invert = invert; // still track the requested state
      if (_disp_panel_handle == nullptr) return; // DPI panel/reset haven't happened yet -- defer
      Panel_DSI::setInvert(invert);
    }

  protected:
    const uint8_t* getInitParams(size_t listno) const override
    {
      // Page select (0xFF) to the "user"/default page (0x77,0x01,0x00,0x00,0x00),
      // then MADCTL (0x36, mirror/order left at 0x00 -- Panel_DSI's own base
      // class derives the real color-order/rotation handling from
      // PanelCommon's config, same as Panel_JD9365.hpp's own header notes
      // for JD9365's equivalent MADCTL write) and COLMOD (0x3A=0x55, 16bpp).
      static constexpr uint8_t list0[] =
      {//len(cmd+params), cmd, params
        6, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x00,
        2, 0x36, 0x00,
        2, 0x3A, 0x55,
        0, // end
      };

      // Page 0x13 -- single EF register (vendor "unlock"-adjacent write,
      // kept verbatim rather than assumed a no-op).
      static constexpr uint8_t list1[] =
      {//len(cmd+params), cmd, params
        6, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x13,
        2, 0xEF, 0x08,
        0, // end
      };

      // Page 0x10 -- power/timing (C0-CC) + the two 16-byte gamma-adjacent
      // tables (B0/B1) this page carries on this panel revision.
      static constexpr uint8_t list2[] =
      {//len(cmd+params), cmd, params
        6, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x10,
        3, 0xC0, 0x63, 0x00,
        3, 0xC1, 0x0D, 0x02,
        3, 0xC2, 0x17, 0x08,
        2, 0xCC, 0x10,
        17, 0xB0, 0x40, 0xC9, 0x94, 0x0E, 0x10, 0x05, 0x0B, 0x09, 0x08, 0x26, 0x04, 0x52, 0x10, 0x69, 0x6B, 0x69,
        17, 0xB1, 0x40, 0xD2, 0x98, 0x0C, 0x92, 0x07, 0x09, 0x08, 0x07, 0x25, 0x02, 0x0E, 0x0C, 0x6E, 0x78, 0x55,
        0, // end
      };

      // Page 0x11 -- panel timing/voltage registers (B0-D0).
      static constexpr uint8_t list3[] =
      {//len(cmd+params), cmd, params
        6, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x11,
        2, 0xB0, 0x5D,
        2, 0xB1, 0x4E,
        2, 0xB2, 0x87,
        2, 0xB3, 0x80,
        2, 0xB5, 0x4E,
        2, 0xB7, 0x85,
        2, 0xB8, 0x21,
        3, 0xB9, 0x10, 0x1F,
        2, 0xBB, 0x03,
        2, 0xBC, 0x00,
        2, 0xC1, 0x78,
        2, 0xC2, 0x78,
        2, 0xD0, 0x88,
        0, // end
      };

      // Still page 0x11 -- the E-series GIP/gamma timing tables (E0-EF).
      // Same page as list3 above; split into its own list purely to keep
      // each C array a manageable size, not because a page-select
      // boundary falls here (there isn't one -- Waveshare's own source
      // has this as one continuous run after D0).
      static constexpr uint8_t list4[] =
      {//len(cmd+params), cmd, params
        4, 0xE0, 0x00, 0x3A, 0x02,
        12, 0xE1, 0x04, 0xA0, 0x00, 0xA0, 0x05, 0xA0, 0x00, 0xA0, 0x00, 0x40, 0x40,
        14, 0xE2, 0x30, 0x00, 0x40, 0x40, 0x32, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00,
        5, 0xE3, 0x00, 0x00, 0x33, 0x33,
        3, 0xE4, 0x44, 0x44,
        17, 0xE5, 0x09, 0x2E, 0xA0, 0xA0, 0x0B, 0x30, 0xA0, 0xA0, 0x05, 0x2A, 0xA0, 0xA0, 0x07, 0x2C, 0xA0, 0xA0,
        5, 0xE6, 0x00, 0x00, 0x33, 0x33,
        3, 0xE7, 0x44, 0x44,
        17, 0xE8, 0x08, 0x2D, 0xA0, 0xA0, 0x0A, 0x2F, 0xA0, 0xA0, 0x04, 0x29, 0xA0, 0xA0, 0x06, 0x2B, 0xA0, 0xA0,
        8, 0xEB, 0x00, 0x00, 0x4E, 0x4E, 0x00, 0x00, 0x00,
        3, 0xEC, 0x08, 0x01,
        17, 0xED, 0xB0, 0x2B, 0x98, 0xA4, 0x56, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF7, 0x65, 0x4A, 0x89, 0xB2, 0x0B,
        7, 0xEF, 0x08, 0x08, 0x08, 0x45, 0x3F, 0x54,
        0, // end
      };

      // Back to the default/user page before sleep-out.
      static constexpr uint8_t list5[] =
      {//len(cmd+params), cmd, params
        6, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x00,
        0, // end
      };

      // Sleep out -- isolated into its own list so getInitDelay(6) can
      // attach the vendor's real 120ms post-SLPOUT delay, same convention
      // Panel_JD9365.hpp uses for its own SLPOUT list.
      static constexpr uint8_t list6[] =
      {
        1, 0x11,
        0,
      };

      // Display on -- Waveshare's own source attaches no delay after this
      // (unlike Panel_JD9365.hpp's 20ms after DISPON) -- kept faithful to
      // the real source rather than adding one not present there.
      static constexpr uint8_t list7[] =
      {
        1, 0x29,
        0,
      };

      (void)list3;
      switch (listno)
      {
      case 0: return list0;
      case 1: return list1;
      case 2: return list2;
      case 3: return list3;
      case 4: return list4;
      case 5: return list5;
      case 6: return list6;
      case 7: return list7;
      default: return nullptr;
      }
    }

    size_t getInitDelay(size_t listno) const override
    {
      switch (listno)
      {
      case 6: return 120; // after SLPOUT (0x11) -- Waveshare's own source's delay_ms=120
      default: return 0;
      }
    }
  };

//----------------------------------------------------------------------------
 }
}
#endif
