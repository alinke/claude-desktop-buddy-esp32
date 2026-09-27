#pragma once
// ============================================================
// canvas.h — the off-screen surface the whole UI draws into.
//
// Upstream drew into one TFT_eSprite the exact size of the CYD's 240x320
// panel, with every coordinate hardcoded. The boards this fork supports run
// from 240x320 to 800x480 and have very different memory, so the UI draws
// into a Canvas instead, in *logical* pixels:
//
//   * Scale K — logical pixels are KxK physical pixels. K comes from the
//     panel's short side (240-320 -> 1, 480 -> 2, 720-800 -> 3), so an
//     800x480 panel is a 400x240 logical landscape. Text sizes, rect sizes
//     and line widths all scale with K, so the GLCD font and 1-px strokes
//     look the same on every board. Rendering is done at physical
//     resolution — only the geometry scales — so circles and diagonals are
//     smooth rather than pixel-doubled.
//   * Offset — when the panel isn't a multiple of K the logical area is
//     centred and the margin stays background colour.
//   * Passes — a board without PSRAM can't hold a whole frame: a 320x480
//     8-bpp frame is 150 KB, the ESP32's largest free block is ~110 KB and
//     BLE still needs its share. So the UI is drawn immediate-mode: every
//     frame is redrawn from scratch by one draw function, and render() runs
//     that function once per horizontal band, reusing a single band sprite
//     and pushing it before drawing the next. Boards with PSRAM (and the
//     CYD, whose 240x320 frame fits) do it in one pass.
//   * Dirty strips — on SPI panels only the 16-row strips whose pixels
//     changed since the last frame are sent (CRC compare). A full 320x480
//     push at the 3.5" boards' 24 MHz SPI clock takes ~100 ms; an idle
//     home screen changes a few strips a frame.
//
// The draw API mirrors the TFT_eSprite subset upstream uses, so the UI code
// reads the same as upstream. Colours are RGB565 throughout.
// ============================================================
#include <Arduino.h>
#include <LovyanGFX.hpp>

class LGFX;

class Canvas : public Print {
 public:
  static const int MAX_BANDS = 1;     // one band sprite, reused per pass
  static const int STRIP_ROWS = 16;
  static const int MAX_STRIPS = 1280 / STRIP_ROWS;

  typedef void (*DrawFn)();

  // Allocates the band sprite for the lcd's current rotation. Safe to call
  // again (frees the old band first). Returns false if nothing fit.
  bool begin(LGFX* lcd);

  // Draw a whole frame with fn and send it to the panel. fn must repaint
  // every pixel it cares about (start with fillSprite) — it may run more
  // than once per frame, once per band.
  void render(DrawFn fn);
  // Forget what's on the panel, so the next render() sends every strip
  // (after something drew on the lcd directly, e.g. touch calibration).
  void invalidate() { _stripsValid = false; }

  // Logical geometry
  int width()  const { return _w; }
  int height() const { return _h; }
  int scale()  const { return _k; }
  int offX()   const { return _ox; }
  int offY()   const { return _oy; }
  int bands()  const { return _nBands; }
  int passes() const { return _passes; }
  int colorDepth() const { return _depth; }

  // ---- drawing (logical coordinates) ----
  void fillSprite(uint16_t c);
  void fillScreen(uint16_t c) { fillSprite(c); }
  void fillRect(int x, int y, int w, int h, uint16_t c);
  void drawRect(int x, int y, int w, int h, uint16_t c);
  void drawFastHLine(int x, int y, int w, uint16_t c);
  void drawFastVLine(int x, int y, int h, uint16_t c);
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c);
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c);
  void fillCircle(int x, int y, int r, uint16_t c);
  void drawCircle(int x, int y, int r, uint16_t c);
  void drawEllipse(int x, int y, int rx, int ry, uint16_t c);
  void drawLine(int x0, int y0, int x1, int y1, uint16_t c);
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c);
  void drawPixel(int x, int y, uint16_t c);
  // Blit a w x h image in the canvas's own pixel format (RGB332 at 8 bpp,
  // RGB565 at 16 bpp — see colorDepth()) with its top-left at (x, y),
  // scaled by `zoom` on top of the canvas scale.
  void pushImage(int x, int y, int w, int h, const void* data, float zoom = 1.0f);

  // ---- text ----
  void setTextSize(float s);
  void setTextColor(uint16_t fg);
  void setTextColor(uint16_t fg, uint16_t bg);
  void setTextDatum(uint8_t d);
  void setCursor(int x, int y);
  void drawString(const char* s, int x, int y);
  void drawString(const String& s, int x, int y) { drawString(s.c_str(), x, y); }
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buf, size_t len) override;
  using Print::write;

  // Translate subsequent drawing by (dx, dy) logical px — lets a page laid
  // out for the full portrait screen render inside a landscape pane.
  void setOrigin(int dx, int dy) { _tx = dx; _ty = dy; }

  // Clip subsequent drawing to a logical rect (pane-local text overflow).
  void setClipRect(int x, int y, int w, int h);
  void clearClipRect();

  // Stream the physical frame as rows for tools/snap.py (see cmdScreenshot):
  // re-renders the last frame pass by pass and hands each physical row to
  // fn (rowPtr, bytesPerRow), top to bottom.
  // On multi-pass boards each band is re-rendered when its turn comes, and
  // streaming one band over serial takes seconds — so time-dependent text
  // (e.g. the approval card's "Ns" counter) can differ across the seam in
  // a screenshot. The panel itself always shows one consistent frame.
  void forEachRow(void (*fn)(const uint8_t*, size_t));
  int physWidth()  const { return _pw; }
  int physHeight() const { return _ph; }

 private:
  void _free();
  int  X(int x) const { return _ox + (x + _tx) * _k; }
  int  Y(int y) const { return _oy + (y + _ty) * _k; }
  // Centre of logical pixel (x,y) in physical space.
  int  CX(int x) const { return X(x) + _k / 2; }
  int  CY(int y) const { return Y(y) + _k / 2; }
  void _pfillRect(int X, int Y, int W, int H, uint16_t c);
  void _pushPass(int pass);

  LGFX*              _lcd = nullptr;
  lgfx::LGFX_Sprite* _band[MAX_BANDS] = {nullptr};
  int                _bandY[MAX_BANDS] = {0};
  int                _nBands = 0;
  int                _rows = 0;        // band height (physical rows)
  int                _passes = 1;
  int                _depth = 16;
  DrawFn             _lastFn = nullptr;
  uint32_t           _stripCrc[MAX_STRIPS] = {0};
  bool               _stripsValid = false;
  int _pw = 0, _ph = 0;       // physical panel size (current rotation)
  int _w = 0, _h = 0;         // logical size
  int _k = 1, _ox = 0, _oy = 0;
  int _tx = 0, _ty = 0;       // setOrigin() translation, logical px
};

extern Canvas spr;
