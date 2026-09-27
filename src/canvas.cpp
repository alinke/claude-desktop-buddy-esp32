// ============================================================
// canvas.cpp — see canvas.h.
// ============================================================
#include "canvas.h"
#include "board.h"
#include <esp_heap_caps.h>
#include <esp_rom_crc.h>

Canvas spr;

void Canvas::_free() {
  for (int i = 0; i < _nBands; i++) {
    if (_band[i]) { _band[i]->deleteSprite(); delete _band[i]; _band[i] = nullptr; }
  }
  _nBands = 0;
}

bool Canvas::begin(LGFX* lcd) {
  _free();
  _lcd = lcd;
  _pw = lcd->width();
  _ph = lcd->height();

  // Scale from the short side: 240-320 -> 1, 480 -> 2, 720-800 -> 3.
  int shortSide = _pw < _ph ? _pw : _ph;
  _k = shortSide / 240;
  if (_k < 1) _k = 1;
  _w  = _pw / _k;
  _h  = _ph / _k;
  _ox = (_pw - _w * _k) / 2;
  _oy = (_ph - _h * _k) / 2;
#if defined(BUDDY_SIM_W) && defined(BUDDY_SIM_H)
  // Dev aid (see the sim-* envs): lay the UI out for another board's logical
  // size, centred on this panel, to check layouts without that hardware.
  _k = 1; _w = BUDDY_SIM_W; _h = BUDDY_SIM_H;
  _ox = (_pw - _w) / 2; _oy = (_ph - _h) / 2;
#endif

  bool psram = psramFound();
  _depth = psram ? 16 : 8;
  size_t bpp = _depth / 8;

  // PSRAM: the whole frame, 16 bpp. Otherwise an 8-bpp band of at most
  // ~76 KB (a whole 240x320 frame on the CYD), shrunk until it allocates.
  int rows = psram ? _ph : (int)(76800 / ((size_t)_pw * bpp));
  if (rows > _ph) rows = _ph;
  while (rows >= 32) {
    auto* s = new lgfx::LGFX_Sprite(lcd);
    s->setColorDepth(_depth);
    s->setPsram(psram);
    if (s->createSprite(_pw, rows)) {
      _band[0]  = s;
      _bandY[0] = 0;
      _nBands   = 1;
      _rows     = rows;
      _passes   = (_ph + rows - 1) / rows;
      _stripsValid = false;
      Serial.printf("[canvas] %dx%d phys, %dx%d logical @%dx, %d pass(es) of %d rows, %d bpp, heap %u largest %u\n",
                    _pw, _ph, _w, _h, _k, _passes, rows, _depth,
                    (unsigned)ESP.getFreeHeap(),
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
      return true;
    }
    delete s;
    rows = rows * 2 / 3;
  }
  Serial.printf("[canvas] allocation FAILED for %dx%d (heap %u, largest %u)\n", _pw, _ph,
                (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  return false;
}

void Canvas::render(DrawFn fn) {
  if (!_nBands || !fn) return;
  _lastFn = fn;
#if BUDDY_RGB_PANEL
  // RGB panels are scanned continuously out of a PSRAM frame buffer. Writing
  // into the live buffer tears, so draw into the back buffer and let the
  // patched driver swap it in at the next VSYNC (see
  // scripts/apply_lovyangfx_rgb_patch.py, "SECOND FIX"). Always one pass.
  _bandY[0] = 0;
  fn();
  auto* bus   = _lcd->rgbBus();
  auto* panel = _lcd->rgbPanel();
  uint8_t* back = bus ? bus->getBackBuffer() : nullptr;
  if (back && panel) {
    panel->setActiveFrameBuffer(back);
    _band[0]->pushSprite(0, 0);
    bus->commitBackBuffer();
  } else {
    _band[0]->pushSprite(0, 0);
  }
#else
  _lcd->startWrite();
  for (int p = 0; p < _passes; p++) {
    _bandY[0] = p * _rows;
    fn();
    _pushPass(p);
  }
  _lcd->endWrite();
  _stripsValid = true;
  _bandY[0] = 0;
#endif
}

// Send the strips of the current pass whose contents changed since the
// last frame.
void Canvas::_pushPass(int pass) {
  const uint8_t* buf = (const uint8_t*)_band[0]->getBuffer();
  size_t bpr = (size_t)_pw * (_depth / 8);
  int y0 = pass * _rows;
  int h  = (_ph - y0 < _rows) ? _ph - y0 : _rows;
  for (int r = 0; r < h; r += STRIP_ROWS) {
    int sh = (h - r < STRIP_ROWS) ? h - r : STRIP_ROWS;
    int strip = (y0 + r) / STRIP_ROWS;
    const uint8_t* p = buf + (size_t)r * bpr;
    uint32_t crc = esp_rom_crc32_le(0, p, sh * bpr);
    if (_stripsValid && strip < MAX_STRIPS && _stripCrc[strip] == crc) continue;
    if (strip < MAX_STRIPS) _stripCrc[strip] = crc;
    if (_depth == 8) _lcd->pushImage(0, y0 + r, _pw, sh, (const lgfx::rgb332_t*)p);
    else             _lcd->pushImage(0, y0 + r, _pw, sh, (const lgfx::swap565_t*)p);
  }
}

// Each drawing call goes to the band at its current y offset; the sprite
// clips whatever falls outside the band.
#define EACH_BAND(stmt) \
  for (int _i = 0; _i < _nBands; _i++) { lgfx::LGFX_Sprite& b = *_band[_i]; const int by = _bandY[_i]; (void)by; stmt; }

void Canvas::_pfillRect(int px, int py, int pw, int ph, uint16_t c) {
  if (pw <= 0 || ph <= 0) return;
  EACH_BAND(b.fillRect(px, py - by, pw, ph, c));
}

void Canvas::fillSprite(uint16_t c) { EACH_BAND(b.fillSprite(c)); }

void Canvas::fillRect(int x, int y, int w, int h, uint16_t c) {
  _pfillRect(X(x), Y(y), w * _k, h * _k, c);
}

void Canvas::drawFastHLine(int x, int y, int w, uint16_t c) { _pfillRect(X(x), Y(y), w * _k, _k, c); }
void Canvas::drawFastVLine(int x, int y, int h, uint16_t c) { _pfillRect(X(x), Y(y), _k, h * _k, c); }

void Canvas::drawRect(int x, int y, int w, int h, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  drawFastHLine(x, y, w, c);
  drawFastHLine(x, y + h - 1, w, c);
  drawFastVLine(x, y, h, c);
  drawFastVLine(x + w - 1, y, h, c);
}

void Canvas::fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
  EACH_BAND(b.fillRoundRect(X(x), Y(y) - by, w * _k, h * _k, r * _k, c));
}

void Canvas::drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
  // A K-px stroke: K nested 1-px outlines, each inset one physical pixel.
  for (int i = 0; i < _k; i++) {
    int rr = r * _k - i; if (rr < 0) rr = 0;
    EACH_BAND(b.drawRoundRect(X(x) + i, Y(y) + i - by, w * _k - 2 * i, h * _k - 2 * i, rr, c));
  }
}

void Canvas::fillCircle(int x, int y, int r, uint16_t c) {
  EACH_BAND(b.fillCircle(CX(x), CY(y) - by, r * _k, c));
}

void Canvas::drawCircle(int x, int y, int r, uint16_t c) {
  if (_k == 1) { EACH_BAND(b.drawCircle(X(x), Y(y) - by, r, c)); return; }
  int ro = r * _k + _k / 2, ri = ro - _k + 1;
  EACH_BAND(b.fillArc(CX(x), CY(y) - by, ro, ri, 0.0f, 360.0f, c));
}

void Canvas::drawEllipse(int x, int y, int rx, int ry, uint16_t c) {
  if (_k == 1) { EACH_BAND(b.drawEllipse(X(x), Y(y) - by, rx, ry, c)); return; }
  int rxo = rx * _k + _k / 2, ryo = ry * _k + _k / 2;
  EACH_BAND(b.fillEllipseArc(CX(x), CY(y) - by, rxo - _k + 1, rxo, ryo - _k + 1, ryo, 0.0f, 360.0f, c));
}

void Canvas::drawLine(int x0, int y0, int x1, int y1, uint16_t c) {
  if (_k == 1) { EACH_BAND(b.drawLine(X(x0), Y(y0) - by, X(x1), Y(y1) - by, c)); return; }
  if (y0 == y1) { int xa = min(x0, x1); drawFastHLine(xa, y0, abs(x1 - x0) + 1, c); return; }
  if (x0 == x1) { int ya = min(y0, y1); drawFastVLine(x0, ya, abs(y1 - y0) + 1, c); return; }
  float r = _k * 0.5f;
  EACH_BAND(b.drawWideLine((float)CX(x0), (float)(CY(y0) - by), (float)CX(x1), (float)(CY(y1) - by), r, c));
}

void Canvas::fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
  EACH_BAND(b.fillTriangle(CX(x0), CY(y0) - by, CX(x1), CY(y1) - by, CX(x2), CY(y2) - by, c));
}

void Canvas::drawPixel(int x, int y, uint16_t c) {
  if (_k == 1) { EACH_BAND(b.drawPixel(X(x), Y(y) - by, c)); return; }
  _pfillRect(X(x), Y(y), _k, _k, c);
}

void Canvas::pushImage(int x, int y, int w, int h, const void* data, float zoom) {
  float z = zoom * _k;
  if (_depth == 8) {
    auto* d = (const lgfx::rgb332_t*)data;
    if (z == 1.0f) { EACH_BAND(b.pushImage(X(x), Y(y) - by, w, h, d)); }
    else { EACH_BAND(b.pushImageRotateZoom((float)X(x), (float)(Y(y) - by), 0.0f, 0.0f, 0.0f, z, z, w, h, d)); }
  } else {
    auto* d = (const lgfx::rgb565_t*)data;
    if (z == 1.0f) { EACH_BAND(b.pushImage(X(x), Y(y) - by, w, h, d)); }
    else { EACH_BAND(b.pushImageRotateZoom((float)X(x), (float)(Y(y) - by), 0.0f, 0.0f, 0.0f, z, z, w, h, d)); }
  }
}

// ---- text ----
void Canvas::setTextSize(float s)                  { EACH_BAND(b.setTextSize(s * _k)); }
void Canvas::setTextColor(uint16_t fg)             { EACH_BAND(b.setTextColor(fg)); }
void Canvas::setTextColor(uint16_t fg, uint16_t bg){ EACH_BAND(b.setTextColor(fg, bg)); }
void Canvas::setTextDatum(uint8_t d)               { EACH_BAND(b.setTextDatum((lgfx::textdatum_t)d)); }
void Canvas::setCursor(int x, int y)               { EACH_BAND(b.setCursor(X(x), Y(y) - by)); }
void Canvas::drawString(const char* s, int x, int y) { EACH_BAND(b.drawString(s, X(x), Y(y) - by)); }

size_t Canvas::write(uint8_t c) { EACH_BAND(b.write(c)); return 1; }
size_t Canvas::write(const uint8_t* buf, size_t len) { EACH_BAND(b.write(buf, len)); return len; }

void Canvas::setClipRect(int x, int y, int w, int h) {
  EACH_BAND(b.setClipRect(X(x), Y(y) - by, w * _k, h * _k));
}
void Canvas::clearClipRect() { EACH_BAND(b.clearClipRect()); }

void Canvas::forEachRow(void (*fn)(const uint8_t*, size_t)) {
  if (!_nBands || !_lastFn) return;
  size_t bpr = (size_t)_pw * (_depth / 8);
  for (int p = 0; p < _passes; p++) {
    _bandY[0] = p * _rows;
    if (_passes > 1) _lastFn();          // single pass: the band already holds the frame
    const uint8_t* buf = (const uint8_t*)_band[0]->getBuffer();
    int h = (_ph - p * _rows < _rows) ? _ph - p * _rows : _rows;
    for (int r = 0; r < h; r++) fn(buf + (size_t)r * bpr, bpr);
  }
  _bandY[0] = 0;
  _stripsValid = false;   // the band now holds the last pass, not what's on screen
}
