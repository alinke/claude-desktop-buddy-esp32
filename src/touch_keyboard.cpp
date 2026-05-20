// ============================================================
// touch_keyboard.cpp — see touch_keyboard.h for the rationale.
// ============================================================
#include "touch_keyboard.h"
#include "hal_m5.h"
#include "character.h"        // characterPalette() — keyboard inherits the theme
#include <string.h>

extern TFT_eSprite spr;       // declared in main.cpp
// Panel dimensions — main.cpp's W/H are file-scope `const int` (C++ internal
// linkage) so we can't reach them by extern. Hardcoded here against the
// same ILI9341 rotation 0 the firmware uses.
static const int W = 240;
static const int H = 320;

// ── Layout constants (240×320) ──────────────────────────────────────────
static const int TITLE_H   = 18;
static const int ENTRY_Y   = 22;
static const int ENTRY_H   = 30;
static const int HINT_Y    = 56;
static const int KBD_Y     = 76;
static const int ROW_H     = 56;
static const int N_ROWS    = 4;
static const int KEY_W_L   = 24;                       // letter/number key width (10 cols × 24 = 240)
static const int KBD_BOTTOM = KBD_Y + N_ROWS * ROW_H;  // 76+224 = 300

// ── Key maps (rows 0..2; 10 chars each) ─────────────────────────────────
static const char* KEYS_LOWER[3] = {
  "qwertyuiop",
  "asdfghjkl-",      // last col is '-' for hostnames / API key dashes
  "zxcvbnm.,/",
};
static const char* KEYS_UPPER[3] = {
  "QWERTYUIOP",
  "ASDFGHJKL_",      // shift turns the dash into an underscore (common in SSIDs)
  "ZXCVBNM<>?",
};
static const char* KEYS_SYM[3] = {
  "1234567890",
  "-/:;()$&@\"",
  ".,?!'=+_\\~",
};

// Bottom row "special" keys, six of them. Widths sum to W (240):
//   SHIFT (36)  MODE (36)  SPACE (96)  BKSP (24)  OK (24)  CXL (24)
enum SpecialKey { SK_SHIFT, SK_MODE, SK_SPACE, SK_BKSP, SK_OK, SK_CXL };
struct SpecRect { int x, w; const char* label; };
static const SpecRect SPECS[6] = {
  { 0,   36, "shift" },
  { 36,  36, "abc"   },        // label flips between "abc" and "?123" at runtime
  { 72,  96, " "     },
  { 168, 24, "<-"    },        // backspace
  { 192, 24, "OK"    },
  { 216, 24, "X"     },        // cancel
};

// Render a single key cell. Returns nothing — caller controls highlighting.
static void drawKey(int x, int y, int w, int h, const char* label,
                    uint16_t fg, uint16_t bg, uint16_t edge, bool highlighted) {
  uint16_t fill = highlighted ? edge : bg;
  spr.fillRoundRect(x + 1, y + 1, w - 2, h - 2, 4, fill);
  spr.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 4, edge);
  spr.setTextColor(highlighted ? bg : fg, fill);
  // Center the label
  int len = (int)strlen(label);
  int tw  = len * 6;
  int th  = 8;
  spr.setTextSize(2);
  tw = len * 12; th = 16;
  // Fall back to size 1 for multi-character labels that wouldn't fit at size 2
  if (tw > w - 6) { spr.setTextSize(1); tw = len * 6; th = 8; }
  spr.setCursor(x + (w - tw) / 2, y + (h - th) / 2);
  spr.print(label);
  spr.setTextSize(1);
}

// Hit-test (lx, ly) against the keyboard grid. Returns row 0..3 and either
// the column (0..9) for alpha rows, or the SpecialKey index for row 3.
// Returns false if outside the keyboard area.
static bool hitTest(int lx, int ly, int& row, int& col) {
  if (lx < 0 || lx >= W) return false;
  if (ly < KBD_Y || ly >= KBD_BOTTOM) return false;
  row = (ly - KBD_Y) / ROW_H;
  if (row < 0) row = 0; if (row > 3) row = 3;
  if (row < 3) {
    col = lx / KEY_W_L;
    if (col > 9) col = 9;
    return true;
  }
  // row 3 — find which SpecRect contains lx
  for (int i = 0; i < 6; i++) {
    if (lx >= SPECS[i].x && lx < SPECS[i].x + SPECS[i].w) { col = i; return true; }
  }
  return false;
}

// Draw the whole keyboard surface from scratch. Called every frame inside
// the modal loop — cheap enough for an inactive screen at ~60 fps.
static void renderKbd(const char* title,
                      const char* text, bool masked, bool maskReveal,
                      int mode, bool shifted,
                      int hiRow, int hiCol) {
  const Palette& pal = characterPalette();
  spr.fillSprite(pal.bg);

  // Title bar
  spr.fillRect(0, 0, W, TITLE_H, pal.body);
  spr.setTextColor(pal.bg, pal.body);
  spr.setTextSize(1);
  spr.setCursor(6, 6);
  spr.print(title);
  if (masked) {
    spr.setCursor(W - 60, 6);
    spr.print(maskReveal ? "show: on" : "show: off");
  }

  // Entry box
  spr.drawRoundRect(4, ENTRY_Y, W - 8, ENTRY_H, 4, pal.textDim);
  spr.setTextSize(2);
  spr.setTextColor(pal.text, pal.bg);
  // Render last ~18 chars so long values still show the typing tail.
  int len = (int)strlen(text);
  int maxChars = (W - 16) / 12;
  int start = len > maxChars ? len - maxChars : 0;
  spr.setCursor(8, ENTRY_Y + 7);
  if (masked && !maskReveal) {
    for (int i = start; i < len; i++) spr.print('*');
  } else {
    spr.print(text + start);
  }
  // Blinking cursor
  if ((millis() / 500) % 2) {
    int cx = 8 + (len - start) * 12;
    if (cx > W - 10) cx = W - 10;
    spr.drawFastVLine(cx, ENTRY_Y + 5, 18, pal.text);
  }

  // Hint row
  spr.setTextSize(1);
  spr.setTextColor(pal.textDim, pal.bg);
  spr.setCursor(6, HINT_Y);
  spr.printf("%d chars   %s",
             len,
             shifted ? "SHIFT on" : (mode == 1 ? "symbols" : "letters"));

  // Keyboard rows 0..2
  uint16_t bg   = pal.bg;
  uint16_t fg   = pal.text;
  uint16_t edge = pal.textDim;
  uint16_t bodyC = pal.body;
  const char* const* layout =
      (mode == 1) ? KEYS_SYM
                  : (shifted ? KEYS_UPPER : KEYS_LOWER);
  for (int r = 0; r < 3; r++) {
    int y = KBD_Y + r * ROW_H;
    for (int c = 0; c < 10; c++) {
      char ch[2] = { layout[r][c], 0 };
      bool hi = (hiRow == r && hiCol == c);
      drawKey(c * KEY_W_L, y, KEY_W_L, ROW_H, ch, fg, bg, edge, hi);
    }
  }
  // Row 3 special keys
  {
    int y = KBD_Y + 3 * ROW_H;
    for (int i = 0; i < 6; i++) {
      bool hi = (hiRow == 3 && hiCol == i);
      const char* lbl = SPECS[i].label;
      if (i == SK_SHIFT && shifted) lbl = "SHIFT";
      if (i == SK_MODE) lbl = (mode == 0) ? "?123" : "abc";
      uint16_t ke = (i == SK_OK) ? 0x07E0 /*green*/
                  : (i == SK_CXL) ? 0xF800 /*red*/
                  : edge;
      uint16_t kf = (i == SK_OK || i == SK_CXL) ? fg : bodyC;
      drawKey(SPECS[i].x, y, SPECS[i].w, ROW_H, lbl, kf, bg, ke, hi);
    }
  }

  spr.pushSprite(0, 0);
}

bool kbdShow(const char* title, char* out, size_t outSize, bool masked) {
  // Local buffer so cancel can simply discard. Sized for the largest
  // expected field (the 128-byte API key) with a little headroom.
  char buf[200] = {0};
  if (out && outSize) {
    strncpy(buf, out, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
  }
  size_t cap = outSize > sizeof(buf) ? sizeof(buf) - 1 : outSize - 1;
  int  mode      = 0;     // 0 = letters, 1 = symbols
  bool shifted   = false; // shift latch (cleared after a letter is typed)
  bool maskReveal = false;
  int  hiRow = -1, hiCol = -1;       // currently-held key (for visual press)

  while (true) {
    M5.update();
    M5.Beep.update();
    // Drain the HAL's "power tap" latch — the title-bar "show: off/on"
    // toggle and any stray top-right tap would otherwise leave the
    // screen-off event armed, blanking the panel right after the
    // keyboard returns. Modal owns the corner while it's up.
    (void)M5.Axp.GetBtnPress();

    // Live highlight while a finger is on a key (resistive single-touch).
    int tx, ty;
    bool down = M5.touch(tx, ty);
    if (down) {
      int r, c;
      if (hitTest(tx, ty, r, c)) { hiRow = r; hiCol = c; }
      else                        { hiRow = -1; hiCol = -1; }
    } else {
      hiRow = -1; hiCol = -1;
    }

    // Commit on release.
    HalTouchEvent evt;
    if (M5.consumeTouchEvent(&evt)) {
      // Use the END coordinates (where the finger lifted) so a slid touch
      // commits the key under the finger, not where it touched down.
      int r, c;
      if (hitTest(evt.ex, evt.ey, r, c)) {
        if (r < 3) {
          // alpha key — append the character (or capitalised version)
          const char* const* layout =
              (mode == 1) ? KEYS_SYM
                          : (shifted ? KEYS_UPPER : KEYS_LOWER);
          char ch = layout[r][c];
          size_t L = strlen(buf);
          if (L < cap) { buf[L] = ch; buf[L + 1] = 0; }
          if (shifted) shifted = false;        // one-shot shift
        } else {
          switch ((SpecialKey)c) {
            case SK_SHIFT: shifted = !shifted; break;
            case SK_MODE:  mode = (mode == 0) ? 1 : 0; shifted = false; break;
            case SK_SPACE: {
              size_t L = strlen(buf);
              if (L < cap) { buf[L] = ' '; buf[L + 1] = 0; }
              break;
            }
            case SK_BKSP: {
              size_t L = strlen(buf);
              if (L) buf[L - 1] = 0;
              break;
            }
            case SK_OK:
              if (out) {
                strncpy(out, buf, outSize - 1);
                out[outSize - 1] = 0;
              }
              return true;
            case SK_CXL:
              return false;
          }
        }
      }
      // mask-reveal toggle: tap inside the title bar's "show: ..." region
      if (masked && evt.ey < TITLE_H && evt.ex >= W - 70) maskReveal = !maskReveal;
    }

    renderKbd(title, buf, masked, maskReveal, mode, shifted, hiRow, hiCol);
    delay(16);
  }
}
