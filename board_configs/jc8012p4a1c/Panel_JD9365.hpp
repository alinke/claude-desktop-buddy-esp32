/*----------------------------------------------------------------------------/
  Panel_JD9365 -- LovyanGFX Panel_DSI driver for the JD9365 MIPI-DSI panel
  driver IC, as used on the Guition JC8012P4A1C_I_W_Y1 (10.1", 800x1280).

  This project's pinned LovyanGFX version (see Panel_ILI9881C.hpp/
  Panel_ST7121.hpp/Panel_EK79007.hpp in the same directory) has no JD9365
  support -- confirmed by grepping the actual installed library, not
  assumed from docs. This file fills that gap.

  IMPORTANT -- "New Panel" vs "Old Panel": this board went through a real
  panel revision after initial release. Guition's own vendor package
  (downloaded directly from their doc site, not just the community GitHub
  mirror) ships TWO separate demo variants and TWO separate factory-flash
  binaries (JC8012P4A1C_I_W_Y-V3.3_Old_Panel.bin vs
  JC8012P4A1C_I_W_Y_V4.4(P4V3x)-new_panel.bin) with GENUINELY DIFFERENT
  JD9365 register values and DIFFERENT DPI timing (not just cosmetic
  differences -- different gamma table, completely different page-2 GIP
  table, different page-4 registers, an entire extra page 5 the old
  version doesn't have, and dpi_clock_freq_mhz 60 vs 80 /
  vsync_pulse_width 4 vs 12 / vsync_back_porch 8 vs 4 / vsync_front_porch
  20 vs 30). Confirmed on real hardware: porting the "Old Panel" sequence
  (from the community repo, github.com/sukesh-ak/JC8012P4A1-GUITION-ESP32-P4_ESP32-C6)
  produced a fully-alive DSI link (correct porches for at least SOME
  values, backlight on, no crash) but a garbled/banded image -- consistent
  with a genuine register-table mismatch against a differently-revisioned
  physical panel, not a fundamentally broken port. This file uses the
  "New Panel" (V4.4/P4V3x) sequence instead, transcribed from Guition's
  own esp_lcd_jd9365.c in that variant's demo folder -- the more likely
  match for a unit purchased recently. If a NEWER or OLDER unit ever shows
  the same corruption again, check which panel revision it actually has
  before assuming this file's sequence is wrong.

  Structural pattern (list-of-commands, getInitParams/getInitDelay) copied
  from Panel_ILI9881C.hpp in this same directory, which is the closest
  existing panel to this one in this LovyanGFX version (another MIPI-DSI
  IC needing a page-select register before writing extended registers).

  Two things intentionally NOT carried over from the vendor's ESP-IDF
  driver, because Panel_DSI's own base class already handles them
  generically for every panel in this platform folder:
    - MADCTL/COLMOD/lane-count setup (jd9365->madctl_val/colmod_val and
      the JD9365_CMD_DSI_INT0 write in panel_jd9365_init) -- Panel_DSI
      derives these from PanelCommon's own color-order/pixel-width config
      and the bus's own lane_num, same as every other panel here. (The
      explicit {0x80,0x01} 2-lane command IS still included below since
      it's part of the vendor's page-0 "unlock" block, sent while
      is_user_set is still false in the vendor's own init loop -- kept
      verbatim rather than assumed redundant.)
    - The LCD ID readback (esp_lcd_panel_io_rx_param(io, 0x04, ...)) --
      diagnostic-only in the vendor driver (logged, never checked against
      an expected value), not required for the panel to initialize.
    - Reset pulse timing -- Panel_DSI's default reset() already does the
      same low/high/settle pulse shape as panel_jd9365_reset().

  CONFIRMED WORKING on a real physical JC8012P4A1C_I_W_Y1 unit: correct
  gold->orange->pink boot-splash gradient, clean black background, crisp
  text, correct landscape orientation (see LGFX_Config.hpp's own header
  for the rgb_order=true finding that got colors right, and the
  BOOT_ROTATION note on why a bare bench panel photographed in its native
  portrait resting position looks rotated 90 degrees -- that's expected,
  not a bug). This was the SECOND real-hardware attempt -- the first,
  Old-Panel-based version of this file produced a garbled/banded image;
  see git history for that attempt if a future panel revision needs the
  same kind of re-diagnosis.
/----------------------------------------------------------------------------*/
#pragma once

// Full explicit paths, not bare quoted includes -- this file lives in this
// board's own board_configs directory, not alongside Panel_DSI.hpp/
// Bus_DSI.hpp inside the LovyanGFX library tree (unlike Panel_ILI9881C.hpp,
// which this file's structure is based on). A bare #include "Panel_DSI.hpp"
// only resolves via the including file's own directory first, which
// wouldn't find it here. Same full-path pattern m5stack-tab5's own
// LGFX_Config.hpp already uses successfully to reach into this same
// library from a different directory, so no library-patching step (like
// apply_lovyangfx_rgb_patch.py) is needed just to add this file.
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>
#if SOC_MIPI_DSI_SUPPORTED

#include <lgfx/v1/platforms/esp32p4/Bus_DSI.hpp>

namespace lgfx
{
 inline namespace v1
 {
//----------------------------------------------------------------------------

  struct Panel_JD9365 : public Panel_DSI
  {
  public:
  protected:
    static constexpr uint8_t PAGE_CMD    = 0xE0;
    static constexpr uint8_t PAGE_USER   = 0x00;
    static constexpr uint8_t PAGE_1      = 0x01;
    static constexpr uint8_t PAGE_2      = 0x02;
    static constexpr uint8_t PAGE_4      = 0x04;
    static constexpr uint8_t PAGE_5      = 0x05;

#ifdef PIXELCADE_JC8012_OLD_PANEL
    // OLD panel revision (Guition SKU 10153001, label "JC8012P4A1C_I_W_Y",
    // vendor binary JC8012P4A1C_I_W_Y-V3.3_Old_Panel.bin): the full JD9365
    // init table from the community repo's esp_lcd_jd9365.c
    // (github.com/sukesh-ak/JC8012P4A1-GUITION-ESP32-P4_ESP32-C6,
    // 1-Demo/arduino-examples/esp32p4_lvgl_v8/src/lcd), copied command for
    // command by a script -- including its 1-byte 0x00 data on 0x11/0x29/
    // 0x35 and its ending (SLPOUT, DISPON +5ms, SLPOUT again +120ms, TE on).
    // This is the sequence that gave a garbled image on our V2 (10153002-V2,
    // "_Y1") unit; the other revision gets the New Panel lists below.
    const uint8_t* getInitParams(size_t listno) const override
    {
      static constexpr uint8_t old0[] =
      {//len(cmd+params), cmd, params
        2, 0xE0, 0x00,
        2, 0xE1, 0x93,
        2, 0xE2, 0x65,
        2, 0xE3, 0xF8,
        2, 0x80, 0x01,
        2, 0xE0, 0x01,
        2, 0x00, 0x00,
        2, 0x01, 0x39,
        2, 0x03, 0x10,
        2, 0x04, 0x41,
        2, 0x0C, 0x74,
        2, 0x17, 0x00,
        2, 0x18, 0xD7,
        2, 0x19, 0x00,
        2, 0x1A, 0x00,
        2, 0x1B, 0xD7,
        2, 0x1C, 0x00,
        2, 0x24, 0xFE,
        2, 0x35, 0x26,
        2, 0x37, 0x69,
        2, 0x38, 0x05,
        2, 0x39, 0x06,
        2, 0x3A, 0x08,
        2, 0x3C, 0x78,
        2, 0x3D, 0xFF,
        2, 0x3E, 0xFF,
        2, 0x3F, 0xFF,
        2, 0x40, 0x06,
        2, 0x41, 0xA0,
        2, 0x43, 0x14,
        2, 0x44, 0x0B,
        2, 0x45, 0x30,
        2, 0x4B, 0x04,
        2, 0x55, 0x02,
        2, 0x57, 0x89,
        2, 0x59, 0x0A,
        2, 0x5A, 0x28,
        2, 0x5B, 0x15,
        2, 0x5D, 0x50,
        2, 0x5E, 0x37,
        2, 0x5F, 0x29,
        2, 0x60, 0x1E,
        2, 0x61, 0x1D,
        2, 0x62, 0x12,
        2, 0x63, 0x1A,
        2, 0x64, 0x08,
        2, 0x65, 0x25,
        2, 0x66, 0x26,
        2, 0x67, 0x28,
        2, 0x68, 0x49,
        2, 0x69, 0x3A,
        2, 0x6A, 0x43,
        2, 0x6B, 0x3A,
        2, 0x6C, 0x3B,
        2, 0x6D, 0x32,
        2, 0x6E, 0x1F,
        2, 0x6F, 0x0E,
        2, 0x70, 0x50,
        2, 0x71, 0x37,
        2, 0x72, 0x29,
        2, 0x73, 0x1E,
        2, 0x74, 0x1D,
        2, 0x75, 0x12,
        2, 0x76, 0x1A,
        2, 0x77, 0x08,
        2, 0x78, 0x25,
        2, 0x79, 0x26,
        2, 0x7A, 0x28,
        2, 0x7B, 0x49,
        2, 0x7C, 0x3A,
        2, 0x7D, 0x43,
        2, 0x7E, 0x3A,
        2, 0x7F, 0x3B,
        2, 0x80, 0x32,
        2, 0x81, 0x1F,
        2, 0x82, 0x0E,
        2, 0xE0, 0x02,
        2, 0x00, 0x1F,
        2, 0x01, 0x1F,
        2, 0x02, 0x52,
        2, 0x03, 0x51,
        2, 0x04, 0x50,
        2, 0x05, 0x4B,
        2, 0x06, 0x4A,
        2, 0x07, 0x49,
        2, 0x08, 0x48,
        2, 0x09, 0x47,
        2, 0x0A, 0x46,
        2, 0x0B, 0x45,
        2, 0x0C, 0x44,
        2, 0x0D, 0x40,
        2, 0x0E, 0x41,
        2, 0x0F, 0x1F,
        2, 0x10, 0x1F,
        2, 0x11, 0x1F,
        2, 0x12, 0x1F,
        2, 0x13, 0x1F,
        2, 0x14, 0x1F,
        2, 0x15, 0x1F,
        2, 0x16, 0x1F,
        2, 0x17, 0x1F,
        2, 0x18, 0x52,
        2, 0x19, 0x51,
        2, 0x1A, 0x50,
        2, 0x1B, 0x4B,
        2, 0x1C, 0x4A,
        2, 0x1D, 0x49,
        2, 0x1E, 0x48,
        2, 0x1F, 0x47,
        2, 0x20, 0x46,
        2, 0x21, 0x45,
        2, 0x22, 0x44,
        2, 0x23, 0x40,
        2, 0x24, 0x41,
        2, 0x25, 0x1F,
        2, 0x26, 0x1F,
        2, 0x27, 0x1F,
        2, 0x28, 0x1F,
        2, 0x29, 0x1F,
        2, 0x2A, 0x1F,
        2, 0x2B, 0x1F,
        2, 0x2C, 0x1F,
        2, 0x2D, 0x1F,
        2, 0x2E, 0x52,
        2, 0x2F, 0x40,
        2, 0x30, 0x41,
        2, 0x31, 0x48,
        2, 0x32, 0x49,
        2, 0x33, 0x4A,
        2, 0x34, 0x4B,
        2, 0x35, 0x44,
        2, 0x36, 0x45,
        2, 0x37, 0x46,
        2, 0x38, 0x47,
        2, 0x39, 0x51,
        2, 0x3A, 0x50,
        2, 0x3B, 0x1F,
        2, 0x3C, 0x1F,
        2, 0x3D, 0x1F,
        2, 0x3E, 0x1F,
        2, 0x3F, 0x1F,
        2, 0x40, 0x1F,
        2, 0x41, 0x1F,
        2, 0x42, 0x1F,
        2, 0x43, 0x1F,
        2, 0x44, 0x52,
        2, 0x45, 0x40,
        2, 0x46, 0x41,
        2, 0x47, 0x48,
        2, 0x48, 0x49,
        2, 0x49, 0x4A,
        2, 0x4A, 0x4B,
        2, 0x4B, 0x44,
        2, 0x4C, 0x45,
        2, 0x4D, 0x46,
        2, 0x4E, 0x47,
        2, 0x4F, 0x51,
        2, 0x50, 0x50,
        2, 0x51, 0x1F,
        2, 0x52, 0x1F,
        2, 0x53, 0x1F,
        2, 0x54, 0x1F,
        2, 0x55, 0x1F,
        2, 0x56, 0x1F,
        2, 0x57, 0x1F,
        2, 0x58, 0x40,
        2, 0x59, 0x00,
        2, 0x5A, 0x00,
        2, 0x5B, 0x10,
        2, 0x5C, 0x05,
        2, 0x5D, 0x50,
        2, 0x5E, 0x01,
        2, 0x5F, 0x02,
        2, 0x60, 0x50,
        2, 0x61, 0x06,
        2, 0x62, 0x04,
        2, 0x63, 0x03,
        2, 0x64, 0x64,
        2, 0x65, 0x65,
        2, 0x66, 0x0B,
        2, 0x67, 0x73,
        2, 0x68, 0x07,
        2, 0x69, 0x06,
        2, 0x6A, 0x64,
        2, 0x6B, 0x08,
        2, 0x6C, 0x00,
        2, 0x6D, 0x32,
        2, 0x6E, 0x08,
        2, 0xE0, 0x04,
        2, 0x2C, 0x6B,
        2, 0x35, 0x08,
        2, 0x37, 0x00,
        2, 0xE0, 0x00,
        2, 0x11, 0x00,
        2, 0x29, 0x00,
        0, // end
      };
      static constexpr uint8_t old1[] =
      {
        2, 0x11, 0x00,
        0,
      };
      static constexpr uint8_t old2[] =
      {
        2, 0x35, 0x00,
        0,
      };
      switch (listno)
      {
      case 0: return old0;
      case 1: return old1;
      case 2: return old2;
      default: return nullptr;
      }
    }

    size_t getInitDelay(size_t listno) const override
    {
      switch (listno)
      {
      case 0: return 5;   // after DISPON
      case 1: return 120; // after the second SLPOUT
      default: return 0;
      }
    }
#else
    const uint8_t* getInitParams(size_t listno) const override
    {
      // Page-0 "unlock" sequence -- E1/E2/E3 fixed vendor password bytes,
      // then the 2-lane DSI select (0x80=0x01).
      static constexpr uint8_t list0[] =
      {//len(cmd+params), cmd, params
        2, PAGE_CMD, PAGE_USER,
        2, 0xE1, 0x93,
        2, 0xE2, 0x65,
        2, 0xE3, 0xF8,
        2, 0x80, 0x01,
        0, // end
      };

      // Page 1: power/timing registers + full gamma table (New Panel
      // values -- genuinely different from the Old Panel sequence this
      // replaced, not just re-formatted).
      static constexpr uint8_t list1[] =
      {//len(cmd+params), cmd, params
        2, PAGE_CMD, PAGE_1,
        2, 0x00, 0x00,
        2, 0x01, 0x44,
        2, 0x03, 0x10,
        2, 0x04, 0x38,
        2, 0x0C, 0x74,
        2, 0x17, 0x00,
        2, 0x18, 0xAF,
        2, 0x19, 0x00,
        2, 0x1A, 0x00,
        2, 0x1B, 0xAF,
        2, 0x1C, 0x00,
        2, 0x35, 0x26,
        2, 0x37, 0x09,
        2, 0x38, 0x04,
        2, 0x39, 0x00,
        2, 0x3A, 0x01,
        2, 0x3C, 0x78,
        2, 0x3D, 0xFF,
        2, 0x3E, 0xFF,
        2, 0x3F, 0x7F,
        2, 0x40, 0x06,
        2, 0x41, 0xA0,
        2, 0x42, 0x81,
        2, 0x43, 0x1E,
        2, 0x44, 0x0D,
        2, 0x45, 0x28,
        2, 0x55, 0x02,
        2, 0x57, 0x69,
        2, 0x59, 0x0A,
        2, 0x5A, 0x2A,
        2, 0x5B, 0x17,
        // G2.2 / G2.5 Positive Gamma
        2, 0x5D, 0x7F,
        2, 0x5E, 0x6B,
        2, 0x5F, 0x5C,
        2, 0x60, 0x50,
        2, 0x61, 0x4C,
        2, 0x62, 0x3E,
        2, 0x63, 0x41,
        2, 0x64, 0x2B,
        2, 0x65, 0x43,
        2, 0x66, 0x42,
        2, 0x67, 0x43,
        2, 0x68, 0x62,
        2, 0x69, 0x52,
        2, 0x6A, 0x5A,
        2, 0x6B, 0x4C,
        2, 0x6C, 0x48,
        2, 0x6D, 0x3A,
        2, 0x6E, 0x28,
        2, 0x6F, 0x10,
        // Negative Gamma
        2, 0x70, 0x7F,
        2, 0x71, 0x6B,
        2, 0x72, 0x5C,
        2, 0x73, 0x50,
        2, 0x74, 0x4C,
        2, 0x75, 0x3E,
        2, 0x76, 0x41,
        2, 0x77, 0x2B,
        2, 0x78, 0x43,
        2, 0x79, 0x42,
        2, 0x7A, 0x43,
        2, 0x7B, 0x62,
        2, 0x7C, 0x52,
        2, 0x7D, 0x5A,
        2, 0x7E, 0x4C,
        2, 0x7F, 0x48,
        2, 0x80, 0x3A,
        2, 0x81, 0x28,
        2, 0x82, 0x10,
        0, // end
      };

      // Page 2: GIP (gate-in-panel) timing table -- New Panel values,
      // completely different from the Old Panel table this replaced.
      static constexpr uint8_t list2[] =
      {//len(cmd+params), cmd, params
        2, PAGE_CMD, PAGE_2,
        2, 0x00, 0x42, 2, 0x01, 0x42, 2, 0x02, 0x40, 2, 0x03, 0x40, 2, 0x04, 0x5E,
        2, 0x05, 0x5E, 2, 0x06, 0x5F, 2, 0x07, 0x5F, 2, 0x08, 0x5F, 2, 0x09, 0x57,
        2, 0x0A, 0x57, 2, 0x0B, 0x77, 2, 0x0C, 0x77, 2, 0x0D, 0x47, 2, 0x0E, 0x47,
        2, 0x0F, 0x45, 2, 0x10, 0x45, 2, 0x11, 0x4B, 2, 0x12, 0x4B, 2, 0x13, 0x49,
        2, 0x14, 0x49, 2, 0x15, 0x5F,
        2, 0x16, 0x41, 2, 0x17, 0x41, 2, 0x18, 0x40, 2, 0x19, 0x40, 2, 0x1A, 0x5E,
        2, 0x1B, 0x5E, 2, 0x1C, 0x5F, 2, 0x1D, 0x5F, 2, 0x1E, 0x5F, 2, 0x1F, 0x57,
        2, 0x20, 0x57, 2, 0x21, 0x77, 2, 0x22, 0x77, 2, 0x23, 0x46, 2, 0x24, 0x46,
        2, 0x25, 0x44, 2, 0x26, 0x44, 2, 0x27, 0x4A, 2, 0x28, 0x4A, 2, 0x29, 0x48,
        2, 0x2A, 0x48, 2, 0x2B, 0x5F,
        2, 0x2C, 0x01, 2, 0x2D, 0x01, 2, 0x2E, 0x00, 2, 0x2F, 0x00, 2, 0x30, 0x1F,
        2, 0x31, 0x1F, 2, 0x32, 0x1E, 2, 0x33, 0x1E, 2, 0x34, 0x1F, 2, 0x35, 0x17,
        2, 0x36, 0x17, 2, 0x37, 0x37, 2, 0x38, 0x37, 2, 0x39, 0x08, 2, 0x3A, 0x08,
        2, 0x3B, 0x0A, 2, 0x3C, 0x0A, 2, 0x3D, 0x04, 2, 0x3E, 0x04, 2, 0x3F, 0x06,
        2, 0x40, 0x06, 2, 0x41, 0x1F,
        2, 0x42, 0x02, 2, 0x43, 0x02, 2, 0x44, 0x00, 2, 0x45, 0x00, 2, 0x46, 0x1F,
        2, 0x47, 0x1F, 2, 0x48, 0x1E, 2, 0x49, 0x1E, 2, 0x4A, 0x1F, 2, 0x4B, 0x17,
        2, 0x4C, 0x17, 2, 0x4D, 0x37, 2, 0x4E, 0x37, 2, 0x4F, 0x09, 2, 0x50, 0x09,
        2, 0x51, 0x0B, 2, 0x52, 0x0B, 2, 0x53, 0x05, 2, 0x54, 0x05, 2, 0x55, 0x07,
        2, 0x56, 0x07, 2, 0x57, 0x1F,
        2, 0x58, 0x40, 2, 0x5B, 0x30, 2, 0x5C, 0x00, 2, 0x5D, 0x34, 2, 0x5E, 0x05,
        2, 0x5F, 0x02, 2, 0x63, 0x00, 2, 0x64, 0x6A, 2, 0x67, 0x73, 2, 0x68, 0x07,
        2, 0x69, 0x08, 2, 0x6A, 0x6A, 2, 0x6B, 0x08,
        2, 0x6C, 0x00, 2, 0x6D, 0x00, 2, 0x6E, 0x00, 2, 0x6F, 0x88,
        2, 0x75, 0xFF, 2, 0x77, 0xDD, 2, 0x78, 0x2C, 2, 0x79, 0x15, 2, 0x7A, 0x17,
        2, 0x7D, 0x14, 2, 0x7E, 0x82,
        0, // end
      };

      // Page 4: New Panel's own (different, smaller) register set.
      static constexpr uint8_t list3[] =
      {//len(cmd+params), cmd, params
        2, PAGE_CMD, PAGE_4,
        2, 0x00, 0x0E,
        2, 0x02, 0xB3,
        2, 0x09, 0x60,
        2, 0x0E, 0x48,
        2, 0x37, 0x58,
        2, 0x2B, 0x0F,
        0, // end
      };

      // Page 5 -- doesn't exist at all in the Old Panel sequence.
      static constexpr uint8_t list4[] =
      {//len(cmd+params), cmd, params
        2, PAGE_CMD, PAGE_5,
        2, 0x15, 0x1D,
        0, // end
      };

      // Back to user page, two more page-0 registers before sleep-out
      // (E6/E7 -- not present in the Old Panel sequence either).
      static constexpr uint8_t list5[] =
      {//len(cmd+params), cmd, params
        2, PAGE_CMD, PAGE_USER,
        2, 0xE6, 0x02,
        2, 0xE7, 0x0C,
        1, CMD_SLPOUT,
        0, // end
      };

      // Vendor sends a dummy {0x00,{0x00}} write with the real delay
      // attached to IT, not to SLPOUT itself -- kept as-is (harmless no-op
      // register write) rather than "simplified" to attaching the delay
      // to SLPOUT directly, in case this specific panel's controller
      // treats the two differently.
      static constexpr uint8_t list6[] =
      {
        2, 0x00, 0x00,
        0,
      };

      static constexpr uint8_t list7[] =
      {
        1, CMD_DISPON,
        0,
      };

      static constexpr uint8_t list8[] =
      {
        2, 0x00, 0x00,
        0,
      };

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
      case 8: return list8;
      default: return nullptr;
      }
    }

    size_t getInitDelay(size_t listno) const override
    {
      switch (listno)
      {
      case 6: return 120; // after SLPOUT (dummy 0x00 write per vendor sequence)
      case 8: return 20;  // after DISPON (dummy 0x00 write per vendor sequence)
      default: return 0;
      }
    }
#endif // PIXELCADE_JC8012_OLD_PANEL
  };

//----------------------------------------------------------------------------
 }
}

#endif
