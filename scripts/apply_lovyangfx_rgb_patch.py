"""
Replaces LovyanGFX's Bus_RGB.cpp (ESP32-S3 RGB-parallel bus driver) after
PlatformIO fetches/installs the library dependency, before compiling.

Why this exists instead of a real fork or a .patch file: LovyanGFX's
Bus_RGB::init() for ESP32-S3 never uses ESP-IDF's own esp_lcd_new_rgb_panel()
-- that code path exists in the source but is entirely commented out, in
favor of a hand-rolled i80-bus-hack + manual GDMA descriptor chain + direct
LCD_CAM register writes, with no bounce buffer and no configured GDMA burst
size/mode/priority (left at silicon defaults). On real 5" (elecrow-
crowpanel-5-0) Sidekick hardware, this caused a STATIC (non-flickering)
white patch on the last 1-6 columns of every scan line, on two different
physical units. Confirmed root cause: ESP-IDF's own RGB LCD docs describe
this exact symptom -- when DMA can't sustain PSRAM fetch bandwidth, the LCD
peripheral outputs fixed dummy bytes for whatever column it stalled on,
every single continuous refresh (RGB-parallel panels have no persistent
GRAM), which is why it's static rather than flickering.

This file replaces Bus_RGB::init()/getDMABuffer()/release() to actually
call esp_lcd_new_rgb_panel() with bounce_buffer_size_px set -- ESP-IDF's
own documented fix, confirmed on real hardware (both previously-affected
units) to eliminate the artifact entirely, not just reduce it (an earlier,
more conservative attempt tuned only the hand-rolled driver's GDMA burst
size/mode/priority and measurably helped one unit but not the other -- see
this project's CLAUDE.md, "Sidekick 5\" (RGB-parallel)" section, for that
full investigation history, including why this rewrite ALSO requires the
board's platformio.ini env to point at pioarduino's platform fork instead
of the mainline espressif32 platform -- the ESP-IDF version mainline
currently resolves to predates bounce-buffer support in its RGB panel
driver entirely).

Since PlatformIO always re-fetches library dependencies from the registry
into .pio/libdeps/<env>/ (ephemeral, not tracked in git, wiped by a clean
checkout or `pio pkg install`), editing that cached copy directly doesn't
persist. This script re-applies the same replacement automatically on
every build via platformio.ini's extra_scripts, so a fresh clone/cache-
clear doesn't silently lose this fix. Idempotent -- safe to run against an
already-patched or not-yet-fetched copy.

IMPORTANT: the GPIO data-pin -> signal-index remap table below
(rgb565sig_tbl) is copied verbatim from the ORIGINAL hand-rolled driver
this replaces, since it's already confirmed correct on real hardware
(colors render correctly, not swapped/scrambled) -- esp_lcd_new_rgb_panel's
own internal signal wiring is a different code path than the one this
replaces, so this mapping has to be supplied explicitly. If a FUTURE
RGB-parallel board added to this project shows swapped/wrong colors after
this patch applies, this table is the first thing to check -- it may not
generalize to every possible pin assignment.

SECOND FIX, added alongside the bounce-buffer one above -- screen tearing on
full-screen content pushes (Fan Art/Screenshot cards from pixelweb, most
noticeable on 5"/7" full-bleed photographic content). Root cause: even with
the bounce-buffer fix, init() below still used num_fbs=1 -- a SINGLE
PSRAM frame buffer, continuously DMA-scanned to the panel. main.cpp's
handleCompleteFrame() already composes each incoming frame off-screen into
its own sprite first (see main.cpp's own doc comment on frameSprite), but
the final pushSprite(0,0) call still blits that sprite into the one live
buffer via LovyanGFX's ordinary Panel_RGB write path -- a real, nonzero-
duration memory copy racing the SAME buffer the LCD_CAM peripheral is
simultaneously scanning out at ~60Hz, with no VSYNC-gated swap to make the
update atomic. A full-screen photographic frame touches every pixel, so it
has the longest copy duration and thus the largest exposure window to land
mid-scan -- small UI redraws are fast/localized and rarely get caught,
which is why this was mostly invisible until full-bleed art cards existed.

Fix: num_fbs=2 (two real hardware frame buffers) plus getBackBuffer()/
commitBackBuffer() below, using ESP-IDF's documented esp_lcd_panel_draw_bitmap()
mechanism to queue a complete new frame into the NON-displayed buffer and
have the driver swap which one is live at the next VSYNC -- the update is
now atomic from the scan-out's point of view (it only ever sees a fully-old
or fully-new frame, never a partial mix), matching the same "buffer 0 or
buffer 1 is either fully current or not being looked at yet" guarantee
ESP-IDF's own RGB-panel double-buffering examples rely on.

main.cpp's handleCompleteFrame() (see its own updated doc comment) redirects
Panel_RGB's row-pointer table to whichever buffer is currently the back one
(Panel_RGB::setActiveFrameBuffer(), added by this same script's
Panel_RGB.cpp/.hpp patch below),
calls the ordinary frameSprite.pushSprite(0,0) (STILL the normal LovyanGFX
write path -- see "THIRD FIX" below for why bypassing it entirely was tried
first and reverted), then commitBackBuffer(). This is why getBackBuffer()/
commitBackBuffer() need to be PUBLIC on Bus_RGB (hence patching Bus_RGB.hpp
too, not just the .cpp this time) -- each RGB-panel board's own
LGFX_Config.hpp also gained small rgbBus()/rgbPanel() accessors so main.cpp
can reach both instances at all (LGFX's own bus/panel members are private).
getDMABuffer() still returns buffer 0 unconditionally -- Panel_RGB::init()
calls it once at boot to set up its own row-pointer table for ordinary
LovyanGFX draw calls (the boot splash text), which is safe specifically
because commitBackBuffer() is never called before the first real streamed
frame, so buffer 0 is still the genuinely-live one throughout boot.

THIRD FIX, found immediately after shipping the double-buffer fix above --
screen rotation/flip stopped working. The symptom was specific and telling:
touch coordinates correctly reflected the configured rotation (tapping the
"flipped" side of the screen registered as the right logical position), but
the DISPLAYED IMAGE never actually rotated/flipped, always showing as if
rotation were 0. Root cause: the double-buffer fix's FIRST implementation
had handleCompleteFrame() bypass pushSprite() entirely, doing a raw memcpy
of frameSprite's pixel buffer straight into getBackBuffer()'s result.
pushSprite() is where LovyanGFX's own rotation-aware coordinate transform
actually lives (Panel_FrameBufferBase's write path maps logical/rotated
coordinates to physical buffer offsets as part of the copy) -- a raw memcpy
has no such transform, so it only ever produced a correct image when
rotation happened to be 0. Touch stayed correct because lcd.getTouch()'s
own rotation transform is a completely separate code path, untouched by
this bug -- which is exactly why touch and display disagreed instead of
both being wrong the same way.

Fix: keep using pushSprite() (so rotation stays correct) but redirect WHERE
it writes. Panel_RGB::setActiveFrameBuffer() (new method, see
this script's Panel_RGB patch below) rebuilds Panel_RGB's row-pointer table
against a caller-supplied buffer -- the exact same loop Panel_RGB::init()
already runs once against buffer 0, just made re-callable. handleCompleteFrame()
now calls setActiveFrameBuffer(getBackBuffer()) before pushSprite(0,0), so
the rotation-aware write lands in the back buffer instead of buffer 0, then
commitBackBuffer() swaps it in.
"""
import os

Import("env")

NEW_BUS_RGB_CPP = r'''/*----------------------------------------------------------------------------/
  Lovyan GFX - Graphics library for embedded devices.

Original Source:
 https://github.com/lovyan03/LovyanGFX/

Licence:
 [FreeBSD](https://github.com/lovyan03/LovyanGFX/blob/master/license.txt)

Author:
 [lovyan03](https://twitter.com/lovyan03)

Contributors:
 [ciniml](https://github.com/ciniml)
 [mongonta0716](https://github.com/mongonta0716)
 [tobozo](https://github.com/tobozo)
/----------------------------------------------------------------------------*/
#if defined (ESP_PLATFORM)
#include <sdkconfig.h>
#if defined (CONFIG_IDF_TARGET_ESP32S3)
#if __has_include (<esp_lcd_panel_rgb.h>)
#include "Bus_RGB.hpp"

#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_ops.h>
#include <esp_pm.h>
#include <esp_log.h>
#include <esp_rom_gpio.h>
#include <rom/gpio.h>
#include <hal/gdma_ll.h>
#include <hal/gpio_ll.h>
#include <hal/gpio_hal.h>
#include <hal/lcd_ll.h>
#include <hal/lcd_hal.h>
#if __has_include(<soc/lcd_periph.h>)
 #include <soc/lcd_periph.h>
 #define LGFX_LCD_RGB_SIG(idx) (&lcd_periph_rgb_signals.panels[(idx)])
 #define LGFX_LCD_I80_SIG(idx) (&lcd_periph_signals.panels[(idx)])
#elif __has_include(<hal/lcd_periph.h>)
 #include <hal/lcd_periph.h>
 #define LGFX_LCD_RGB_SIG(idx) (&soc_lcd_rgb_signals[(idx)])
 #define LGFX_LCD_I80_SIG(idx) (&soc_lcd_i80_signals[(idx)])
#endif
#include <soc/lcd_cam_reg.h>
#include <soc/lcd_cam_struct.h>
#if __has_include(<soc/gdma_channel.h>)
 #include <soc/gdma_channel.h>
#elif __has_include(<hal/gdma_channel.h>)
 #include <hal/gdma_channel.h>
#endif
#include <soc/gdma_reg.h>
#include <soc/gdma_struct.h>

#if __has_include (<esp_private/periph_ctrl.h>)
 #include <esp_private/periph_ctrl.h>
#else
 #include <driver/periph_ctrl.h>
#endif

#if defined ( ESP_IDF_VERSION_VAL )
 #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
  #define LGFX_HAL_FUNC_SEL
 #endif
#endif

namespace lgfx
{
 inline namespace v1
 {
//----------------------------------------------------------------------------

  static __attribute__ ((always_inline)) inline volatile uint32_t* reg(uint32_t addr) { return (volatile uint32_t *)ETS_UNCACHED_ADDR(addr); }

  static lcd_cam_dev_t* getDev(int port)
  {
    return &LCD_CAM;
  }

  void Bus_RGB::config(const config_t& cfg)
  {
    _cfg = cfg;
  }


  IRAM_ATTR void Bus_RGB::lcd_default_isr_handler(void *args)
  {
    // Pixelcade patch (apply_lovyangfx_rgb_patch.py) -- unused, since
    // init()/release() now use esp_lcd_new_rgb_panel() with a real
    // bounce buffer, which manages its own VSYNC-driven DMA restart
    // internally. Kept as a no-op rather than removed, since it's still
    // declared in Bus_RGB.hpp and nothing calls it anymore.
  }

  static void _gpio_pin_sig(uint32_t pin, uint32_t sig)
  {
    #if defined LGFX_HAL_FUNC_SEL
      gpio_hal_context_t gpio_hal = {
          .dev = GPIO_HAL_GET_HW(GPIO_PORT_0)
      };
      gpio_hal_func_sel(&gpio_hal, pin, PIN_FUNC_GPIO);
    #else
      gpio_hal_iomux_func_sel(GPIO_PIN_MUX_REG[pin], PIN_FUNC_GPIO);
    #endif
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
    esp_rom_gpio_connect_out_signal(pin, sig, false, false);
  }

  bool Bus_RGB::init(void)
  {
    // Pixelcade patch (apply_lovyangfx_rgb_patch.py) -- see that script's
    // own doc comment for the full story. Confirmed on real hardware (two
    // previously-affected physical units) to eliminate a static white-
    // column artifact that the original hand-rolled driver this replaces
    // had no protection against at all.
    uint8_t pixel_bytes = (_cfg.panel->getWriteDepth() & bit_mask) >> 3;

    esp_lcd_rgb_panel_config_t panel_config;
    memset(&panel_config, 0, sizeof(panel_config));
    // PIXELCADE_CLKSRC_PLL160M_FINAL -- 160MHz / 10 = exact 16MHz (sharp).
    // LCD_CLK_SRC_XTAL was tried to escape WiFi-correlated noise and ruled
    // out on real hardware: 16MHz from 40MHz is a 2+1/2 fractional divide
    // (soft text), and the noise persisted on XTAL with the radio up anyway.
    // The real cause was the radio's TX power -- see WiFiTransport.hpp's
    // startRadio().
    panel_config.clk_src = LCD_CLK_SRC_PLL160M;
    panel_config.timings.pclk_hz = _cfg.freq_write;
    panel_config.timings.h_res = _cfg.panel->width();
    panel_config.timings.v_res = _cfg.panel->height();
    panel_config.timings.hsync_pulse_width = _cfg.hsync_pulse_width;
    panel_config.timings.hsync_back_porch = _cfg.hsync_back_porch;
    panel_config.timings.hsync_front_porch = _cfg.hsync_front_porch;
    panel_config.timings.vsync_pulse_width = _cfg.vsync_pulse_width;
    panel_config.timings.vsync_back_porch = _cfg.vsync_back_porch;
    panel_config.timings.vsync_front_porch = _cfg.vsync_front_porch;
    // hsync_idle_low/vsync_idle_low: inferred from the hand-rolled code
    // this replaces, which assigned _cfg.hsync_polarity/vsync_polarity
    // directly into the equivalent raw register field with no inversion
    // -- both are always 0 (idle low) in every board config today, so
    // !polarity == true either way; recheck against real hardware if a
    // future board ever sets polarity=1.
    panel_config.timings.flags.hsync_idle_low = !_cfg.hsync_polarity;
    panel_config.timings.flags.vsync_idle_low = !_cfg.vsync_polarity;
    panel_config.timings.flags.de_idle_high = _cfg.de_idle_high;
    panel_config.timings.flags.pclk_active_neg = _cfg.pclk_active_neg;
    panel_config.timings.flags.pclk_idle_high = _cfg.pclk_idle_high;

    panel_config.data_width = 16;
    panel_config.bits_per_pixel = 16;
    // num_fbs=2, not 1 -- see this script's own module doc comment
    // ("SECOND FIX") for the full tearing story. Two real hardware frame
    // buffers so a complete new frame can be written into whichever one
    // ISN'T currently being scanned out, then swapped in atomically via
    // commitBackBuffer() below.
    panel_config.num_fbs = 2;
    // Bounce buffer -- the ORIGINAL fix (static white-column artifact).
    // Fast internal DRAM staging that decouples PSRAM fetch latency from
    // the continuous real-time scan-out; the driver this replaces never
    // had any equivalent protection at all. Sized for a handful of lines
    // per ESP-IDF's own guidance ("only needs to hold a few lines of
    // display data"). Still needed alongside num_fbs=2 above -- it solves
    // a DIFFERENT problem (DMA/PSRAM bandwidth stalls within a single
    // scan line), not superseded by double-buffering.
    //
    // bounce_buffer_size_px MUST evenly divide the panel's total pixel
    // count (width*height) or esp_lcd_new_rgb_panel() aborts at init with
    // "frame buffer size must be multiple of bounce buffer size" --
    // confirmed via a real crash-loop on esp32-4827s043c's 480x272 panel
    // (height=272 isn't divisible by a hardcoded 10, unlike every prior
    // board here, all height=480). Since panel dimensions vary per board
    // and this file is shared across all of them, compute the largest
    // usable line count (up to 10) that evenly divides THIS panel's
    // actual height at runtime, instead of assuming 10 always works.
    // Degrades to 1 (always valid) in the worst case; unchanged 10-line
    // behavior for every board whose height is already divisible by 10.
    // PIXELCADE_S3_BOUNCE_OVERRIDE_V1 -- per-env override: more lines give
    // the scan-out more slack when WiFi + JPEG decode load the shared
    // flash/PSRAM bus (costs width*lines*2 bytes of internal RAM, twice).
    {
#ifdef PIXELCADE_S3_RGB_BOUNCE_LINES
      uint32_t bounce_lines = PIXELCADE_S3_RGB_BOUNCE_LINES;
#else
      uint32_t bounce_lines = 10;
#endif
      while (bounce_lines > 1 && (_cfg.panel->height() % bounce_lines) != 0) { --bounce_lines; }
      panel_config.bounce_buffer_size_px = _cfg.panel->width() * bounce_lines;
    }
    panel_config.dma_burst_size = 16;

    panel_config.hsync_gpio_num = _cfg.pin_hsync;
    panel_config.vsync_gpio_num = _cfg.pin_vsync;
    panel_config.de_gpio_num = _cfg.pin_henable;
    panel_config.pclk_gpio_num = _cfg.pin_pclk;
    panel_config.disp_gpio_num = GPIO_NUM_NC;

    // See this file's module-level doc comment -- copied verbatim from
    // the original hand-rolled driver's rgb565sig_tbl, confirmed correct
    // on real hardware.
    static constexpr const uint8_t rgb565sig_tbl[] = { 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7 };
    for (int i = 0; i < 16; ++i) {
      panel_config.data_gpio_nums[rgb565sig_tbl[i]] = (gpio_num_t)_cfg.pin_data[i];
    }

    panel_config.flags.fb_in_psram = 1;

    if (ESP_OK != esp_lcd_new_rgb_panel(&panel_config, &_panel_handle)) {
      ESP_LOGE("Bus_RGB", "esp_lcd_new_rgb_panel failed");
      return false;
    }
    if (ESP_OK != esp_lcd_panel_reset(_panel_handle)) {
      ESP_LOGE("Bus_RGB", "esp_lcd_panel_reset failed");
      return false;
    }
    if (ESP_OK != esp_lcd_panel_init(_panel_handle)) {
      ESP_LOGE("Bus_RGB", "esp_lcd_panel_init failed");
      return false;
    }

    void* fb0 = nullptr;
    void* fb1 = nullptr;
    if (ESP_OK != esp_lcd_rgb_panel_get_frame_buffer(_panel_handle, 2, &fb0, &fb1) || fb0 == nullptr || fb1 == nullptr) {
      ESP_LOGE("Bus_RGB", "esp_lcd_rgb_panel_get_frame_buffer failed");
      return false;
    }
    _frame_buffers[0] = (uint8_t*)fb0;
    _frame_buffers[1] = (uint8_t*)fb1;
    // Buffer 0 is what's actually live/scanned at this point (ESP-IDF
    // displays fb0 until the first esp_lcd_panel_draw_bitmap call targets
    // the other one) -- _back_idx=1 means the FIRST commitBackBuffer()
    // call writes/commits buffer 1, matching that.
    _back_idx = 1;

    return true;
  }

  uint8_t* Bus_RGB::getDMABuffer(uint32_t length)
  {
    // Buffer 0 unconditionally -- see this script's module doc comment
    // ("SECOND FIX") for why this is safe: only Panel_RGB::init()'s
    // one-time boot-splash row-pointer setup calls this, before any real
    // streamed frame / commitBackBuffer() call has happened.
    return _frame_buffers[0];
  }

  // Pixelcade patch (apply_lovyangfx_rgb_patch.py) -- writes a COMPLETE new
  // frame (already fully composed by the caller into getBackBuffer()'s
  // result) into the panel atomically. esp_lcd_panel_draw_bitmap on an
  // esp_lcd_rgb_panel with num_fbs>1 queues color_data's buffer to become
  // the scanned-out one at the next VSYNC, rather than writing into the
  // live buffer directly -- this is what actually eliminates tearing,
  // since the scan-out only ever sees a fully-old or fully-new frame.
  // Flipping _back_idx AFTER the call means the buffer just committed
  // becomes the new front, and the NEXT caller's getBackBuffer() correctly
  // returns the other (now safe-to-write, non-displayed) one.
  void Bus_RGB::commitBackBuffer(void)
  {
    if (_panel_handle == nullptr) { return; }
    int w = _cfg.panel->width();
    int h = _cfg.panel->height();
    esp_lcd_panel_draw_bitmap(_panel_handle, 0, 0, w, h, _frame_buffers[_back_idx]);
    _back_idx ^= 1;
#ifdef PIXELCADE_S3_RGB_RESTART_ON_COMMIT
    // PIXELCADE_S3_RESTART_ON_COMMIT_V1 -- re-sync the scan-out after each
    // new frame. On the Elecrow Advance boards, streaks that appear with
    // WiFi on stay until a power cycle, i.e. the scan-out loses its place
    // and never re-syncs by itself. esp_lcd_rgb_panel_restart() is
    // ESP-IDF's documented recovery for that: it restarts the RGB DMA at
    // the next VSYNC, so it isn't visible.
    esp_lcd_rgb_panel_restart(_panel_handle);
#endif
  }

  void Bus_RGB::release(void)
  {
    if (_panel_handle) {
      esp_lcd_panel_del(_panel_handle);
      _panel_handle = nullptr;
    }
  }

//----------------------------------------------------------------------------
 }
}

#endif
#endif
#endif
'''

# Bus_RGB.hpp companion patch -- needed starting with the double-buffer
# ("SECOND FIX") change above, since getBackBuffer()/commitBackBuffer() must
# be PUBLIC methods main.cpp can call, and _frame_buffer (singular) became
# _frame_buffers[2]/_back_idx. Same replace-whole-file approach as the .cpp,
# same reason (PlatformIO re-fetches this file fresh into .pio/libdeps on
# every clean checkout, so editing the cached copy directly doesn't
# persist).
NEW_BUS_RGB_HPP = r'''/*----------------------------------------------------------------------------/
  Lovyan GFX - Graphics library for embedded devices.

Original Source:
 https://github.com/lovyan03/LovyanGFX/

Licence:
 [FreeBSD](https://github.com/lovyan03/LovyanGFX/blob/master/license.txt)

Author:
 [lovyan03](https://twitter.com/lovyan03)

Contributors:
 [ciniml](https://github.com/ciniml)
 [mongonta0716](https://github.com/mongonta0716)
 [tobozo](https://github.com/tobozo)
/----------------------------------------------------------------------------*/
#pragma once

#if __has_include (<esp_lcd_panel_rgb.h>)
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_interface.h>


#include <esp_private/gdma.h>
#include <hal/dma_types.h>

#include "../../Bus.hpp"
#include "../../panel/Panel_FrameBufferBase.hpp"
#include "../common.hpp"

struct lcd_cam_dev_t;
struct esp_rgb_panel_t;

namespace lgfx
{
 inline namespace v1
 {
//----------------------------------------------------------------------------

  class Bus_RGB : public IBus
  {
  public:
    struct config_t
    {
      Panel_FrameBufferBase* panel = nullptr;

      // LCD_CAM peripheral number. No need to change (only 0 for ESP32-S3.)
      int8_t port = 0;

      // pixel clock
      uint32_t freq_write = 16000000;

      int8_t pin_pclk = -1;
      int8_t pin_vsync = -1;
      int8_t pin_hsync = -1;
      int8_t pin_henable = -1;
      union
      {
        int8_t pin_data[16];
        struct
        {
          int8_t pin_d0;
          int8_t pin_d1;
          int8_t pin_d2;
          int8_t pin_d3;
          int8_t pin_d4;
          int8_t pin_d5;
          int8_t pin_d6;
          int8_t pin_d7;
          int8_t pin_d8;
          int8_t pin_d9;
          int8_t pin_d10;
          int8_t pin_d11;
          int8_t pin_d12;
          int8_t pin_d13;
          int8_t pin_d14;
          int8_t pin_d15;
        };
      };

      int8_t hsync_pulse_width = 0;
      int8_t hsync_back_porch = 0;
      int8_t hsync_front_porch = 0;
      int8_t vsync_pulse_width = 0;
      int8_t vsync_back_porch = 0;
      int8_t vsync_front_porch = 0;
      bool hsync_polarity = 0;
      bool vsync_polarity = 0;
      bool pclk_active_neg = 1;
      bool de_idle_high = 0;
      bool pclk_idle_high = 0;
    };

    const config_t& config(void) const { return _cfg; }
    void config(const config_t& config);

    bus_type_t busType(void) const override { return bus_type_t::bus_unknown; }

    bool init(void) override;
    void release(void) override;

    void beginTransaction(void) override {}
    void endTransaction(void) override {}
    void wait(void) override {}
    bool busy(void) const override { return false; }

    void flush(void) override {}
    bool writeCommand(uint32_t data, uint_fast8_t bit_length) override { return true; }
    void writeData(uint32_t data, uint_fast8_t bit_length) override {}
    void writeDataRepeat(uint32_t data, uint_fast8_t bit_length, uint32_t count) override {}
    void writePixels(pixelcopy_t* param, uint32_t length) override {}
    void writeBytes(const uint8_t* data, uint32_t length, bool dc, bool use_dma) override {}

    void initDMA(void) override {}
    void addDMAQueue(const uint8_t* data, uint32_t length) override {}
    void execDMAQueue(void) override {}
    uint8_t* getDMABuffer(uint32_t length) override;

    // Pixelcade patch (apply_lovyangfx_rgb_patch.py) -- double-buffering to
    // eliminate screen tearing on full-screen content pushes. See
    // Bus_RGB.cpp's commitBackBuffer() doc comment and this script's own
    // module doc comment ("SECOND FIX") for the full mechanism.
    // getBackBuffer() returns the buffer safe to write a COMPLETE new frame
    // into; commitBackBuffer() queues it to become the scanned-out buffer
    // at the next VSYNC.
    uint8_t* getBackBuffer(void) const { return _frame_buffers[_back_idx]; }
    void commitBackBuffer(void);

    void beginRead(void) override {}
    void endRead(void) override {}
    uint32_t readData(uint_fast8_t bit_length) override { return 0; }
    bool readBytes(uint8_t* dst, uint32_t length, bool use_dma) override { return false; }
    void readPixels(void* dst, pixelcopy_t* param, uint32_t length) override {}

  private:
    config_t _cfg;

    dma_descriptor_t _dmadesc_restart;
    dma_descriptor_t* _dmadesc = nullptr;
    esp_lcd_i80_bus_handle_t _i80_bus = nullptr;
    int32_t _dma_ch;

    esp_lcd_panel_handle_t _panel_handle = nullptr;

    // Pixelcade patch: two hardware frame buffers (num_fbs=2 in init()),
    // not one -- _frame_buffers[0] is also what getDMABuffer() returns
    // (Panel_RGB::init() calls this once at boot to build its row-pointer
    // table for ordinary LovyanGFX draw calls, e.g. the boot splash text --
    // safe because commitBackBuffer() is never called before the first
    // real streamed frame, so buffer 0 is still the genuinely-live one
    // throughout boot). _back_idx tracks which buffer is currently the
    // NON-displayed one, safe to write a new frame into.
    uint8_t* _frame_buffers[2] = { nullptr, nullptr };
    int _back_idx = 1;
    intr_handle_t _intr_handle;
    static void lcd_default_isr_handler(void *args);
  };

//----------------------------------------------------------------------------
 }
}
#endif
'''

# Markers used to detect an already-patched file (idempotency check), one
# per file. MUST be a string unique to the CURRENT content specifically --
# not just "some Pixelcade patch was applied at some point". An earlier
# version of this script used the generic "Pixelcade patch
# (apply_lovyangfx_rgb_patch.py)" comment text as the marker, which is
# present in EVERY revision of this patch (including the older, confirmed-
# insufficient GDMA-tuning-only attempt that predates the real bounce-buffer
# fix -- see this script's own module doc comment). Any env whose
# .pio/libdeps cache was populated back when that older revision was current
# got stuck on it forever: the marker matched, so the idempotency check kept
# skipping the newer content on every subsequent build, silently regressing
# the static-white-column fix back to the insufficient version with no
# error or warning. Confirmed on real hardware: jc8048w550c's cache was
# exactly this stale, while elecrow-crowpanel-5-0 and esp32-8048s070c (whose
# caches happened to populate AFTER the bounce-buffer fix landed) were fine.
# "commitBackBuffer"/"getBackBuffer" are unique to the double-buffer fix
# specifically (a bounce-buffer-only cache still has "bounce_buffer_size_px"
# but NOT these), so a stale bounce-buffer-only cache is correctly detected
# as needing this newer patch too.
#
# CPP marker further updated to "bounce_lines" (present ONLY in the
# runtime-computed-bounce-buffer-height fix, see NEW_BUS_RGB_CPP's own
# comment on why a hardcoded *10 isn't safe for every panel height) --
# same reasoning as the commitBackBuffer/getBackBuffer split above: a
# cache patched with an older revision that has "commitBackBuffer" but
# NOT "bounce_lines" must be detected as needing this newer patch, not
# skipped as "already applied". Confirmed this exact failure mode already
# happened once for a different content change (see this project's
# CLAUDE.md, "static white column artifact" section, "REGRESSED AND
# RE-FIXED") -- the marker must change whenever the patched content
# changes in a way that matters, every time, not just this once.
#
# Marker is a literal sentinel token that exists ONLY as a marker, not as
# part of any functional code (variable names, macro names, etc.) -- see
# NEW_BUS_RGB_CPP's own comment on the bounce-buffer-sizing block for why
# (a real, confirmed silent-regression bug from using a functional-code
# string as the marker, twice, before this scheme). Bumped to
# PIXELCADE_CLKSRC_PLL160M_FINAL -- back to plain LCD_CLK_SRC_PLL160M after
# the XTAL / selectable-clock experiments (see NEW_BUS_RGB_CPP's comment on
# the clk_src line). Exists only in this revision, so any cache holding one
# of those experimental versions is correctly detected as stale.
# Bumped to PIXELCADE_S3_RESTART_ON_COMMIT_V1 for the optional scan-out
# restart after each frame (Elecrow Advance streaks); before that,
# PIXELCADE_S3_BOUNCE_OVERRIDE_V1 for the per-env
# PIXELCADE_S3_RGB_BOUNCE_LINES override.
CPP_ALREADY_PATCHED_MARKER = "PIXELCADE_S3_RESTART_ON_COMMIT_V1"
HPP_ALREADY_PATCHED_MARKER = "getBackBuffer"


def patch_file(path, new_content, marker, label):
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()

    if marker in content:
        print(f"[lovyangfx-patch] already applied ({label}): {path}")
        return

    with open(path, "w", encoding="utf-8") as f:
        f.write(new_content)
    print(f"[lovyangfx-patch] replaced with {label} version: {path}")


# Panel_RGB.hpp/.cpp -- SURGICAL insertion, not a full-file replacement like
# Bus_RGB above. Panel_RGB.cpp is large (500+ lines, including the ST7701/
# GC9503 panel init-command byte tables) -- reproducing the whole file here
# verbatim would risk a transcription error silently corrupting one of those
# tables for a DIFFERENT board than the one being built. Inserting a single
# new method next to an existing, uniquely-named anchor carries none of that
# risk and needs no maintenance if upstream LovyanGFX changes unrelated
# parts of the file.
def insert_at_anchor(path, anchor, new_text, marker, label, before=False):
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()

    if marker in content:
        print(f"[lovyangfx-patch] already applied ({label}): {path}")
        return

    idx = content.find(anchor)
    if idx == -1:
        print(f"[lovyangfx-patch] WARNING: anchor not found, skipping ({label}): {path}")
        return

    insert_at = idx if before else idx + len(anchor)
    content = content[:insert_at] + new_text + content[insert_at:]
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"[lovyangfx-patch] inserted {label}: {path}")


# Marker/anchor for Panel_RGB.hpp -- adds the setActiveFrameBuffer()
# declaration right after init()'s own declaration (a stable, unique line
# in the public section of the Panel_RGB struct).
PANEL_RGB_HPP_ANCHOR = "    bool init(bool) override;\n"
PANEL_RGB_HPP_MARKER = "setActiveFrameBuffer"
PANEL_RGB_HPP_INSERT = """
    // Pixelcade patch (apply_lovyangfx_rgb_patch.py, "THIRD FIX") --
    // rebuilds _lines_buffer against fb, the same loop init() below already
    // runs once against buffer 0. Lets a caller redirect the ROTATION-AWARE
    // write path (pushSprite/writePixels/etc., which is what actually
    // applies the current rotation/flip transform) at a DIFFERENT hardware
    // buffer -- e.g. Bus_RGB's back buffer for double-buffered, tear-free
    // AND rotation-correct frame pushes. See that script's own module doc
    // comment for the full story of why a raw memcpy (bypassing this write
    // path entirely) broke rotation/flip.
    void setActiveFrameBuffer(uint8_t* fb);
"""

# Marker/anchor for Panel_RGB.cpp -- inserts the implementation right before
# initFrameBuffer(), a stable, unique function signature already in the
# file.
PANEL_RGB_CPP_ANCHOR = "  bool Panel_RGB::initFrameBuffer("
PANEL_RGB_CPP_MARKER = "setActiveFrameBuffer"
PANEL_RGB_CPP_INSERT = """  // Pixelcade patch (apply_lovyangfx_rgb_patch.py, "THIRD FIX") -- see
  // Panel_RGB.hpp's own doc comment on the declaration. Identical row-
  // stride math to the loop in Panel_RGB::init() above (w padded to a
  // multiple of 4, _write_bits/8 bytes per pixel) -- just parameterized on
  // fb instead of always using buffer 0's pointer, and re-runnable at any
  // time rather than only once at init.
  void Panel_RGB::setActiveFrameBuffer(uint8_t* fb)
  {
    if (_lines_buffer == nullptr || fb == nullptr) { return; }
    auto h = _cfg.panel_height;
    uint8_t bits = _write_bits;
    int w = (_cfg.panel_width + 3) & ~3;
    auto cur = fb;
    for (int i = 0; i < h; ++i) {
      _lines_buffer[i] = cur;
      cur += w * bits >> 3;
    }
  }

"""


project_dir = env.subst("$PROJECT_DIR")
env_name = env.subst("$PIOENV")
bus_rgb_dir = f"{project_dir}/.pio/libdeps/{env_name}/LovyanGFX/src/lgfx/v1/platforms/esp32s3"
bus_rgb_cpp_path = f"{bus_rgb_dir}/Bus_RGB.cpp"
bus_rgb_hpp_path = f"{bus_rgb_dir}/Bus_RGB.hpp"
panel_rgb_cpp_path = f"{bus_rgb_dir}/Panel_RGB.cpp"
panel_rgb_hpp_path = f"{bus_rgb_dir}/Panel_RGB.hpp"

if os.path.exists(bus_rgb_cpp_path):
    patch_file(bus_rgb_cpp_path, NEW_BUS_RGB_CPP, CPP_ALREADY_PATCHED_MARKER, "double-buffer")
else:
    print(f"[lovyangfx-patch] skipped, not found (not an RGB-panel board?): {bus_rgb_cpp_path}")

if os.path.exists(bus_rgb_hpp_path):
    patch_file(bus_rgb_hpp_path, NEW_BUS_RGB_HPP, HPP_ALREADY_PATCHED_MARKER, "double-buffer")
else:
    print(f"[lovyangfx-patch] skipped, not found (not an RGB-panel board?): {bus_rgb_hpp_path}")

if os.path.exists(panel_rgb_hpp_path):
    insert_at_anchor(panel_rgb_hpp_path, PANEL_RGB_HPP_ANCHOR, PANEL_RGB_HPP_INSERT, PANEL_RGB_HPP_MARKER, "setActiveFrameBuffer decl")
else:
    print(f"[lovyangfx-patch] skipped, not found (not an RGB-panel board?): {panel_rgb_hpp_path}")

if os.path.exists(panel_rgb_cpp_path):
    # before=True: PANEL_RGB_CPP_ANCHOR is initFrameBuffer's OWN signature,
    # which must stay intact -- the new method's implementation goes right
    # before it, not after (there's no clean single-line "end of the
    # previous function" anchor to insert after instead).
    insert_at_anchor(panel_rgb_cpp_path, PANEL_RGB_CPP_ANCHOR, PANEL_RGB_CPP_INSERT, PANEL_RGB_CPP_MARKER, "setActiveFrameBuffer impl", before=True)
else:
    print(f"[lovyangfx-patch] skipped, not found (not an RGB-panel board?): {panel_rgb_cpp_path}")


# =============================================================================
# ESP32-P4 RGB-parallel port (elecrow-crowpanel-ai-p4)
# =============================================================================
#
# LovyanGFX ships ZERO RGB-parallel support for the ESP32-P4 target -- its
# esp32p4 platform folder only has MIPI-DSI classes (Bus_DSI/Panel_DSI/
# Panel_EK79007/etc, used by m5stack-tab5 and jc8012p4a1c), confirmed via a
# direct filesystem search before writing any of this. So unlike the
# esp32s3 section above (which PATCHES an existing Bus_RGB.cpp that's just
# missing the bounce-buffer fix), this section WRITES BRAND NEW files --
# there is nothing to patch, since the classes don't exist for this target
# at all.
#
# This is a close, high-confidence adaptation of the exact same
# double-buffer/bounce-buffer Bus_RGB fix above, NOT a from-scratch design
# -- verified chip-agnostic piece by piece against real ESP-IDF source
# before porting:
#   - SOC_LCD_RGB_SUPPORTED=1 for ESP32-P4 (soc/esp32p4/include/soc/
#     soc_caps.h) -- esp_lcd_new_rgb_panel() genuinely works on this chip.
#   - esp_lcd_rgb_panel_config_t (components/esp_lcd/rgb/include/
#     esp_lcd_panel_rgb.h) is defined once, chip-agnostically, gated only
#     by #if SOC_LCD_RGB_SUPPORTED -- not an ESP32-S3-specific struct.
#   - The rgb565sig_tbl GPIO remap table below is NOT an S3-specific
#     quirk -- confirmed by fetching and diffing BOTH chips' own
#     components/soc/<chip>/lcd_periph.c: both have an identical, linear
#     (non-permuted) data_sigs[] array. The permutation exists solely to
#     translate this PROJECT'S OWN pin_data[] board-physical ordering
#     (B0-B4,G0-G5,R0-R4) into RGB565 bit-significance order -- a
#     chip-independent concern -- so the exact same table applies here
#     unchanged.
#   - LCD_CLK_SRC_PLL160M is a real, valid case in ESP32-P4's own HAL
#     clock-select switch (components/hal/esp32p4/include/hal/lcd_ll.h,
#     lcd_ll_select_clk_src()) -- not S3-only.
#
# The original hand-rolled GDMA/register-poking code the ESP32-S3 section
# above still carries (getDev/reg/_gpio_pin_sig/lcd_default_isr_handler,
# now dead code kept only because Bus_RGB.hpp still declares it) is
# deliberately NOT ported here at all -- there's no legacy P4 driver to
# stay compatible with, so this starts clean, esp_lcd_new_rgb_panel()-only,
# with none of that cruft.
#
# Panel_RGB (the base struct Panel_FrameBufferBase subclass, NOT the
# ST7701_Base/GC9503 SPI-init-command subclasses -- this board's ST7262
# driver IC needs no SPI init sequence at all, exactly like
# elecrow-crowpanel-5-0-advance's own ESP32-S3 Panel_RGB usage) is
# similarly confirmed chip-agnostic: it only touches heap_alloc_dma/
# heap_alloc_psram/heap_free/gpio_hi/pinMode (all declared in the shared,
# non-platform-specific platforms/common.hpp) plus this file's own
# Bus_RGB -- no ESP32-S3-specific calls anywhere in its body, only its
# containing .cpp's own top-level #if guard, which this port simply
# retargets to CONFIG_IDF_TARGET_ESP32P4.
NEW_P4_BUS_RGB_HPP = r'''/*----------------------------------------------------------------------------/
  Lovyan GFX - Graphics library for embedded devices.

Original Source:
 https://github.com/lovyan03/LovyanGFX/

Licence:
 [FreeBSD](https://github.com/lovyan03/LovyanGFX/blob/master/license.txt)
/----------------------------------------------------------------------------*/
// Pixelcade patch (apply_lovyangfx_rgb_patch.py) -- ESP32-P4 RGB-parallel
// bus driver. LovyanGFX ships no such class for this target at all (only
// MIPI-DSI); this is a from-scratch file, not a patch to existing
// upstream content. See this script's own "ESP32-P4 RGB-parallel port"
// module doc comment for the full chip-agnostic verification this is
// based on -- it's a close adaptation of this same file's ESP32-S3
// double-buffer/bounce-buffer Bus_RGB (below), not an independent design.
#pragma once

#if __has_include (<esp_lcd_panel_rgb.h>)
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_interface.h>

#include "../../Bus.hpp"
#include "../../panel/Panel_FrameBufferBase.hpp"
#include "../common.hpp"

namespace lgfx
{
 inline namespace v1
 {
//----------------------------------------------------------------------------

  class Bus_RGB : public IBus
  {
  public:
    struct config_t
    {
      Panel_FrameBufferBase* panel = nullptr;

      int8_t port = 0;

      // pixel clock
      uint32_t freq_write = 16000000;

      int8_t pin_pclk = -1;
      int8_t pin_vsync = -1;
      int8_t pin_hsync = -1;
      int8_t pin_henable = -1;
      union
      {
        int8_t pin_data[16];
        struct
        {
          int8_t pin_d0;
          int8_t pin_d1;
          int8_t pin_d2;
          int8_t pin_d3;
          int8_t pin_d4;
          int8_t pin_d5;
          int8_t pin_d6;
          int8_t pin_d7;
          int8_t pin_d8;
          int8_t pin_d9;
          int8_t pin_d10;
          int8_t pin_d11;
          int8_t pin_d12;
          int8_t pin_d13;
          int8_t pin_d14;
          int8_t pin_d15;
        };
      };

      int8_t hsync_pulse_width = 0;
      int8_t hsync_back_porch = 0;
      int8_t hsync_front_porch = 0;
      int8_t vsync_pulse_width = 0;
      int8_t vsync_back_porch = 0;
      int8_t vsync_front_porch = 0;
      bool hsync_polarity = 0;
      bool vsync_polarity = 0;
      bool pclk_active_neg = 1;
      bool de_idle_high = 0;
      bool pclk_idle_high = 0;
    };

    const config_t& config(void) const { return _cfg; }
    void config(const config_t& config);

    bus_type_t busType(void) const override { return bus_type_t::bus_unknown; }

    bool init(void) override;
    void release(void) override;

    void beginTransaction(void) override {}
    void endTransaction(void) override {}
    void wait(void) override {}
    bool busy(void) const override { return false; }

    void flush(void) override {}
    bool writeCommand(uint32_t data, uint_fast8_t bit_length) override { return true; }
    void writeData(uint32_t data, uint_fast8_t bit_length) override {}
    void writeDataRepeat(uint32_t data, uint_fast8_t bit_length, uint32_t count) override {}
    void writePixels(pixelcopy_t* param, uint32_t length) override {}
    void writeBytes(const uint8_t* data, uint32_t length, bool dc, bool use_dma) override {}

    void initDMA(void) override {}
    void addDMAQueue(const uint8_t* data, uint32_t length) override {}
    void execDMAQueue(void) override {}
    uint8_t* getDMABuffer(uint32_t length) override;

    // Double-buffered, tear-free frame push -- see Bus_RGB.cpp's
    // commitBackBuffer() doc comment (same mechanism as the ESP32-S3
    // section of this script).
    uint8_t* getBackBuffer(void) const { return _frame_buffers[_back_idx]; }
    void commitBackBuffer(void);

    void beginRead(void) override {}
    void endRead(void) override {}
    uint32_t readData(uint_fast8_t bit_length) override { return 0; }
    bool readBytes(uint8_t* dst, uint32_t length, bool use_dma) override { return false; }
    void readPixels(void* dst, pixelcopy_t* param, uint32_t length) override {}

  private:
    config_t _cfg;

    esp_lcd_panel_handle_t _panel_handle = nullptr;

    uint8_t* _frame_buffers[2] = { nullptr, nullptr };
    int _back_idx = 1;
  };

//----------------------------------------------------------------------------
 }
}
#endif
'''

NEW_P4_BUS_RGB_CPP = r'''/*----------------------------------------------------------------------------/
  Lovyan GFX - Graphics library for embedded devices.

Original Source:
 https://github.com/lovyan03/LovyanGFX/

Licence:
 [FreeBSD](https://github.com/lovyan03/LovyanGFX/blob/master/license.txt)
/----------------------------------------------------------------------------*/
// Pixelcade patch (apply_lovyangfx_rgb_patch.py) -- see Bus_RGB.hpp's own
// doc comment. Ported from this script's proven ESP32-S3 double-buffer/
// bounce-buffer fix (see the module doc comment above, "ESP32-P4
// RGB-parallel port", for the chip-agnostic verification this is based
// on) -- same esp_lcd_new_rgb_panel()-only approach, none of the dead
// hand-rolled GDMA/register code the S3 file still carries for legacy
// reasons.
#if defined (ESP_PLATFORM)
#include <sdkconfig.h>
#if defined (CONFIG_IDF_TARGET_ESP32P4)
#if __has_include (<esp_lcd_panel_rgb.h>)
#include "Bus_RGB.hpp"

#include <cstring>
#include <esp_lcd_panel_rgb.h>
#include <esp_lcd_panel_ops.h>
#include <esp_log.h>

namespace lgfx
{
 inline namespace v1
 {
//----------------------------------------------------------------------------

  void Bus_RGB::config(const config_t& cfg)
  {
    _cfg = cfg;
  }

  bool Bus_RGB::init(void)
  {
    esp_lcd_rgb_panel_config_t panel_config;
    memset(&panel_config, 0, sizeof(panel_config));
    panel_config.clk_src = LCD_CLK_SRC_PLL160M;
    panel_config.timings.pclk_hz = _cfg.freq_write;
    panel_config.timings.h_res = _cfg.panel->width();
    panel_config.timings.v_res = _cfg.panel->height();
    panel_config.timings.hsync_pulse_width = _cfg.hsync_pulse_width;
    panel_config.timings.hsync_back_porch = _cfg.hsync_back_porch;
    panel_config.timings.hsync_front_porch = _cfg.hsync_front_porch;
    panel_config.timings.vsync_pulse_width = _cfg.vsync_pulse_width;
    panel_config.timings.vsync_back_porch = _cfg.vsync_back_porch;
    panel_config.timings.vsync_front_porch = _cfg.vsync_front_porch;
    panel_config.timings.flags.hsync_idle_low = !_cfg.hsync_polarity;
    panel_config.timings.flags.vsync_idle_low = !_cfg.vsync_polarity;
    panel_config.timings.flags.de_idle_high = _cfg.de_idle_high;
    panel_config.timings.flags.pclk_active_neg = _cfg.pclk_active_neg;
    panel_config.timings.flags.pclk_idle_high = _cfg.pclk_idle_high;

    panel_config.data_width = 16;
    panel_config.bits_per_pixel = 16;
    // Two real hardware frame buffers -- see getBackBuffer()/
    // commitBackBuffer() below for the tear-free double-buffer mechanism
    // (identical to the ESP32-S3 section of this script).
    panel_config.num_fbs = 2;
    // Bounce buffer -- decouples PSRAM fetch latency from the continuous
    // real-time scan-out (ESP-IDF's own documented fix for a DMA/PSRAM
    // bandwidth-starvation artifact; see the ESP32-S3 section's own doc
    // comment for the full original investigation this was confirmed
    // against on real hardware). Runtime-computed line count -- see the
    // ESP32-S3 section's own comment for why a hardcoded 10 isn't safe
    // for every panel height.
    {
#ifdef PIXELCADE_P4_RGB_BOUNCE_LINES
      // Per-env override: the bounce buffers live in internal DMA RAM, which
      // on a P4 board with WiFi is also what ESP-Hosted's SDIO buffers need.
      uint32_t bounce_lines = PIXELCADE_P4_RGB_BOUNCE_LINES;
#else
      uint32_t bounce_lines = 10;
#endif
      while (bounce_lines > 1 && (_cfg.panel->height() % bounce_lines) != 0) { --bounce_lines; }
      panel_config.bounce_buffer_size_px = _cfg.panel->width() * bounce_lines;
    }
    panel_config.dma_burst_size = 16;

    panel_config.hsync_gpio_num = _cfg.pin_hsync;
    panel_config.vsync_gpio_num = _cfg.pin_vsync;
    panel_config.de_gpio_num = _cfg.pin_henable;
    panel_config.pclk_gpio_num = _cfg.pin_pclk;
    panel_config.disp_gpio_num = GPIO_NUM_NC;

    // Copied verbatim from the ESP32-S3 section's own table -- confirmed
    // chip-agnostic (see this script's module doc comment) rather than
    // re-derived.
    static constexpr const uint8_t rgb565sig_tbl[] = { 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7 };
    for (int i = 0; i < 16; ++i) {
      panel_config.data_gpio_nums[rgb565sig_tbl[i]] = (gpio_num_t)_cfg.pin_data[i];
    }

    panel_config.flags.fb_in_psram = 1;

    if (ESP_OK != esp_lcd_new_rgb_panel(&panel_config, &_panel_handle)) {
      ESP_LOGE("Bus_RGB_P4", "esp_lcd_new_rgb_panel failed");
      return false;
    }
    if (ESP_OK != esp_lcd_panel_reset(_panel_handle)) {
      ESP_LOGE("Bus_RGB_P4", "esp_lcd_panel_reset failed");
      return false;
    }
    if (ESP_OK != esp_lcd_panel_init(_panel_handle)) {
      ESP_LOGE("Bus_RGB_P4", "esp_lcd_panel_init failed");
      return false;
    }

    void* fb0 = nullptr;
    void* fb1 = nullptr;
    if (ESP_OK != esp_lcd_rgb_panel_get_frame_buffer(_panel_handle, 2, &fb0, &fb1) || fb0 == nullptr || fb1 == nullptr) {
      ESP_LOGE("Bus_RGB_P4", "esp_lcd_rgb_panel_get_frame_buffer failed");
      return false;
    }
    _frame_buffers[0] = (uint8_t*)fb0;
    _frame_buffers[1] = (uint8_t*)fb1;
    // Buffer 0 is what's live at this point -- _back_idx=1 means the
    // FIRST commitBackBuffer() call writes/commits buffer 1.
    _back_idx = 1;

    return true;
  }

  uint8_t* Bus_RGB::getDMABuffer(uint32_t length)
  {
    // Buffer 0 unconditionally -- only Panel_RGB::init()'s one-time boot
    // row-pointer setup calls this, before any real streamed frame /
    // commitBackBuffer() call has happened, so buffer 0 is still the
    // genuinely-live one at that point.
    return _frame_buffers[0];
  }

  void Bus_RGB::commitBackBuffer(void)
  {
    if (_panel_handle == nullptr) { return; }
    int w = _cfg.panel->width();
    int h = _cfg.panel->height();
    esp_lcd_panel_draw_bitmap(_panel_handle, 0, 0, w, h, _frame_buffers[_back_idx]);
    _back_idx ^= 1;
  }

  void Bus_RGB::release(void)
  {
    if (_panel_handle) {
      esp_lcd_panel_del(_panel_handle);
      _panel_handle = nullptr;
    }
  }

//----------------------------------------------------------------------------
 }
}
#endif
#endif
#endif
'''

NEW_P4_PANEL_RGB_HPP = r'''/*----------------------------------------------------------------------------/
  Lovyan GFX - Graphics library for embedded devices.

Original Source:
 https://github.com/lovyan03/LovyanGFX/

Licence:
 [FreeBSD](https://github.com/lovyan03/LovyanGFX/blob/master/license.txt)
/----------------------------------------------------------------------------*/
// Pixelcade patch (apply_lovyangfx_rgb_patch.py) -- ESP32-P4 RGB-parallel
// panel driver. Base Panel_FrameBufferBase subclass only -- NOT the
// ST7701_Base/GC9503 SPI-init-command subclasses the ESP32-S3 section of
// this script also patches, since this board's ST7262 driver IC needs no
// SPI init sequence at all (identical situation to
// elecrow-crowpanel-5-0-advance's own ESP32-S3 Panel_RGB usage). Confirmed
// chip-agnostic before porting -- see this script's "ESP32-P4
// RGB-parallel port" module doc comment.
#pragma once

#include "../../panel/Panel_FrameBufferBase.hpp"

namespace lgfx
{
 inline namespace v1
 {
//----------------------------------------------------------------------------

  struct Panel_RGB : public Panel_FrameBufferBase
  {
  public:

    Panel_RGB(void);
    virtual ~Panel_RGB(void);

    struct config_detail_t
    {
      int8_t pin_cs = -1;
      int8_t pin_sclk = -1;
      int8_t pin_mosi = -1;
      uint8_t use_psram = 2;
    };
    const config_detail_t& config_detail(void) const { return _config_detail; }
    void config_detail(const config_detail_t& config_detail) { _config_detail = config_detail; }

    color_depth_t setColorDepth(color_depth_t) override { return _write_depth; }

    void setPsram( bool use_psram ) { _config_detail.use_psram = use_psram; }

    bool init(bool) override;

    void writeCommand(uint32_t, uint_fast8_t) override;
    void writeData(uint32_t, uint_fast8_t) override;

    // Rebuilds the row-pointer table against a caller-supplied buffer --
    // lets main.cpp redirect the rotation-aware write path (pushSprite)
    // at Bus_RGB's back buffer for tear-free, rotation-correct frame
    // pushes. See apply_lovyangfx_rgb_patch.py's ESP32-S3 "THIRD FIX" for
    // the full story of why this exists (a raw memcpy bypassing
    // pushSprite broke rotation).
    void setActiveFrameBuffer(uint8_t* fb);

  protected:

    config_detail_t _config_detail;

    bool initFrameBuffer(uint_fast16_t w, uint_fast16_t h, color_depth_t depth, uint8_t chunk_lines, uint8_t use_psram);
    void deinitFrameBuffer(void);

    uint8_t _lines_per_chunk = 4;

    uint8_t* _frame_buffer = nullptr;
  };

//----------------------------------------------------------------------------
 }
}
'''

NEW_P4_PANEL_RGB_CPP = r'''/*----------------------------------------------------------------------------/
  Lovyan GFX - Graphics library for embedded devices.

Original Source:
 https://github.com/lovyan03/LovyanGFX/

Licence:
 [FreeBSD](https://github.com/lovyan03/LovyanGFX/blob/master/license.txt)
/----------------------------------------------------------------------------*/
// Pixelcade patch (apply_lovyangfx_rgb_patch.py) -- see Panel_RGB.hpp's own
// doc comment. init()/initFrameBuffer()/deinitFrameBuffer() are a direct
// port of the ESP32-S3 section's Panel_RGB base class (confirmed
// chip-agnostic -- only heap_alloc_dma/heap_alloc_psram/heap_free/gpio_hi/
// pinMode, all from the shared platforms/common.hpp, no ESP32-S3-specific
// calls). writeCommand/writeData are no-ops here (not ported from the S3
// file's real bit-banged-SPI implementation) since this board's ST7262
// panel has no pin_cs/pin_sclk/pin_mosi wired at all -- those exist only
// to drive the ST7701/GC9503 SPI-init-command subclasses this port
// deliberately excludes.
#if defined (ESP_PLATFORM)
#include <sdkconfig.h>
#if defined (CONFIG_IDF_TARGET_ESP32P4)

#include "Panel_RGB.hpp"
#include "../../Bus.hpp"
#include "../common.hpp"

#include "Bus_RGB.hpp"

#include <cstring>

namespace lgfx
{
 inline namespace v1
 {
//----------------------------------------------------------------------------

  Panel_RGB::Panel_RGB(void)
  {
    _write_depth = color_depth_t::rgb565_2Byte;
    _read_depth = color_depth_t::rgb565_2Byte;
  }

  Panel_RGB::~Panel_RGB(void)
  {
    deinitFrameBuffer();
  }

  bool Panel_RGB::init(bool use_reset)
  {
    if (!Panel_FrameBufferBase::init(use_reset)) { return false; }

    auto h = _cfg.panel_height;

    auto frame_buffer_ = _bus->getDMABuffer(0);
    size_t lineArray_size = h * sizeof(void*);
    uint8_t** lineArray = (uint8_t**)heap_alloc_dma(lineArray_size);

    if (lineArray)
    {
      _lines_buffer = lineArray;
      memset(lineArray, 0, lineArray_size);

      uint8_t bits = _write_bits;
      int w = (_cfg.panel_width + 3) & ~3;
      if (frame_buffer_) {
        auto fb = frame_buffer_;
        for (int i = 0; i < h; ++i) {
          lineArray[i] = fb;
          fb += w * bits >> 3;
        }

        int32_t pin_cs = _config_detail.pin_cs;
        if (pin_cs >= 0) {
          lgfx::gpio_hi(pin_cs);
          lgfx::pinMode(pin_cs, pin_mode_t::output);
        }

        return true;
      }
      heap_free(lineArray);
    }

    return false;
  }

  bool Panel_RGB::initFrameBuffer(uint_fast16_t w, uint_fast16_t h, color_depth_t depth, uint8_t chunk_lines, uint8_t use_psram)
  {
    size_t lineArray_size = h * sizeof(void*);
    uint8_t** lineArray = (uint8_t**)heap_alloc_dma(lineArray_size);
    if (lineArray)
    {
      memset(lineArray, 0, lineArray_size);

      uint8_t bits = (depth & color_depth_t::bit_mask);
      w = (w + 3) & ~3;
      _frame_buffer = (uint8_t*)heap_alloc_psram((w * bits >> 3) * h);
      if (_frame_buffer) {
        _lines_buffer = lineArray;
        auto fb = _frame_buffer;
        for (int i = 0; i < h; ++i) {
          lineArray[i] = fb;
          fb += w * bits >> 3;
        }
        return true;
      }
      heap_free(lineArray);
    }
    return false;
  }

  void Panel_RGB::deinitFrameBuffer(void)
  {
    if (_frame_buffer)
    {
      heap_free(_frame_buffer);
      _frame_buffer = nullptr;
    }

    if (_lines_buffer)
    {
      heap_free(_lines_buffer);
      _lines_buffer = nullptr;
    }
  }

  void Panel_RGB::writeCommand(uint32_t data, uint_fast8_t len)
  {
    // No-op -- see this file's own doc comment. ST7262 needs no command
    // bus; nothing in this board's LGFX_Config.hpp configures pin_cs/
    // pin_sclk/pin_mosi, so this is never meaningfully called.
  }

  void Panel_RGB::writeData(uint32_t data, uint_fast8_t len)
  {
    // No-op -- see writeCommand() above.
  }

  void Panel_RGB::setActiveFrameBuffer(uint8_t* fb)
  {
    if (_lines_buffer == nullptr || fb == nullptr) { return; }
    auto h = _cfg.panel_height;
    uint8_t bits = _write_bits;
    int w = (_cfg.panel_width + 3) & ~3;
    auto cur = fb;
    for (int i = 0; i < h; ++i) {
      _lines_buffer[i] = cur;
      cur += w * bits >> 3;
    }
  }

//----------------------------------------------------------------------------
 }
}
#endif
#endif
'''

p4_platform_dir = f"{project_dir}/.pio/libdeps/{env_name}/LovyanGFX/src/lgfx/v1/platforms/esp32p4"
p4_bus_rgb_hpp_path = f"{p4_platform_dir}/Bus_RGB.hpp"
p4_bus_rgb_cpp_path = f"{p4_platform_dir}/Bus_RGB.cpp"
p4_panel_rgb_hpp_path = f"{p4_platform_dir}/Panel_RGB.hpp"
p4_panel_rgb_cpp_path = f"{p4_platform_dir}/Panel_RGB.cpp"

# Marker used for all four P4 files -- unlike the S3 section, these are
# brand-new files with nothing upstream to collide with, so a single
# generic marker is safe (no risk of matching an older, insufficient
# revision the way the S3 marker history warns about -- see
# CPP_ALREADY_PATCHED_MARKER's own comment).
P4_FILE_MARKER = "Pixelcade patch (apply_lovyangfx_rgb_patch.py) -- ESP32-P4 RGB-parallel"


def write_new_file(path, content, label):
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            existing = f.read()
        if P4_FILE_MARKER in existing and existing == content:
            print(f"[lovyangfx-patch] already up to date ({label}): {path}")
            return
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"[lovyangfx-patch] wrote {label}: {path}")


if os.path.isdir(p4_platform_dir):
    write_new_file(p4_bus_rgb_hpp_path, NEW_P4_BUS_RGB_HPP, "P4 Bus_RGB.hpp")
    write_new_file(p4_bus_rgb_cpp_path, NEW_P4_BUS_RGB_CPP, "P4 Bus_RGB.cpp")
    write_new_file(p4_panel_rgb_hpp_path, NEW_P4_PANEL_RGB_HPP, "P4 Panel_RGB.hpp")
    write_new_file(p4_panel_rgb_cpp_path, NEW_P4_PANEL_RGB_CPP, "P4 Panel_RGB.cpp")
else:
    print(f"[lovyangfx-patch] skipped, not found (not an ESP32-P4 board?): {p4_platform_dir}")
