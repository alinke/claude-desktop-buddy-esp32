// ============================================================
// hal_m5.cpp — CYD-backed implementation of the M5 shim.
// See hal_m5.h for the rationale and the behavioural deltas.
// ============================================================
#include "hal_m5.h"

// Flip to 1 to log raw + mapped on each press while debugging.
#define HAL_TOUCH_DEBUG 0

// Screen geometry — must match main.cpp's W/H and the TFT rotation.
static const int SCR_W = 240;
static const int SCR_H = 320;

// Runtime touch calibration. We use a 3-point affine transform built
// from the TL / TR / BL corner samples — handles any combination of
// rotation, mirror, skew, and scale a near-linear resistive panel
// can throw at us. BR is captured too but currently used only as a
// sanity check during calibration.
//
// Targets land at TARGET_INSET pixels in from each edge, NOT at the
// physical corners (those edges are unreachable with a finger). The
// affine math extrapolates correctly beyond the calibrated points so
// taps near the very edges still map sensibly; constrain() clamps
// anything outside the screen.
static const int TARGET_INSET = 16;
static int  s_tcRx[4] = { 230, 3900, 3900, 230 };
static int  s_tcRy[4] = { 230, 230,  3900, 3900 };
// Precomputed basis: u runs from TL→TR in raw space, v from TL→BL.
static int  s_tcDxU = 3670, s_tcDyU = 0;
static int  s_tcDxV = 0,    s_tcDyV = 3670;
static long s_tcDet = 3670L * 3670L;
static bool s_tcLoaded = false;

static void _tcRebuildBasis() {
  s_tcDxU = s_tcRx[1] - s_tcRx[0];
  s_tcDyU = s_tcRy[1] - s_tcRy[0];
  s_tcDxV = s_tcRx[3] - s_tcRx[0];
  s_tcDyV = s_tcRy[3] - s_tcRy[0];
  s_tcDet = (long)s_tcDxU * s_tcDyV - (long)s_tcDyU * s_tcDxV;
  if (s_tcDet == 0) s_tcDet = 1;     // degenerate guard — keeps divisions safe
}

// LEDC channels (arduino-esp32 v2 API, matches the pipboy reference).
#define BL_LEDC_CH    0
#define SPK_LEDC_CH   2
#define BL_PWM_FREQ   5000
#define SPK_DUTY      128   // 50% square wave

M5Class M5;

static SPIClass          touchSPI(VSPI);
// Named `tp` rather than `touch` so it doesn't shadow M5Class::touch() inside
// member functions.
static XPT2046_Touchscreen tp(CYD_TOUCH_CS, CYD_TOUCH_IRQ);

// ---- HalButton ---------------------------------------------------------
void HalButton::_update(bool down, uint32_t now) {
  _wasPressed  = (down && !_last);
  _wasReleased = (!down && _last);
  if (_wasPressed) _downMs = now;
  _state = down;
  _last  = down;
}

// ---- HalAxp ------------------------------------------------------------
void HalAxp::begin() {
  ledcSetup(BL_LEDC_CH, BL_PWM_FREQ, 8);
  ledcAttachPin(CYD_BL_PIN, BL_LEDC_CH);
  _on = true;
  ScreenBreath(_level);
}

void HalAxp::ScreenBreath(int level) {
  if (level < 0)   level = 0;
  if (level > 100) level = 100;
  _level = level;
  if (_on) ledcWrite(BL_LEDC_CH, (level * 255) / 100);
}

void HalAxp::SetLDO2(bool on) {
  _on = on;
  ledcWrite(BL_LEDC_CH, on ? (_level * 255) / 100 : 0);
}

void HalAxp::PowerOff() {
  // No PMIC to cut power. Next best thing: blank the panel and deep
  // sleep until the touch IRQ goes low, which wakes via a clean reboot.
  ledcWrite(BL_LEDC_CH, 0);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)CYD_TOUCH_IRQ, 0);
  esp_deep_sleep_start();
}

float HalAxp::GetBatVoltage() {
  // ESP32-2432S028R routes VBAT through a 2:1 divider to GPIO34 on the
  // variants that wire a battery at all. Returns ~0 on USB-only units.
  uint32_t mv = analogReadMilliVolts(34);
  return (mv * 2.0f) / 1000.0f;
}
float HalAxp::GetBatCurrent()    { return 0.0f; }
float HalAxp::GetVBusVoltage()   { return 5.0f; }   // USB-powered: keep "on USB" true
float HalAxp::GetTempInAXP192()  { return temperatureRead(); }

uint8_t HalAxp::GetBtnPress() {
  if (_powerEvent) { _powerEvent = false; return 0x02; }
  return 0;
}

// ---- HalBeep -----------------------------------------------------------
void HalBeep::begin() {
  pinMode(CYD_AMP_EN_PIN, OUTPUT);
  digitalWrite(CYD_AMP_EN_PIN, HIGH);   // amp off (active low)
}

void HalBeep::_silence() {
  if (_attached) ledcWrite(SPK_LEDC_CH, 0);     // silence, keep attached
  digitalWrite(CYD_AMP_EN_PIN, HIGH);            // amp off
  _playing = false;
}

void HalBeep::_startNote(const BeepNote& note) {
  _offAtMs = millis() + note.durMs;
  _playing = true;
  if (note.freq == 0) {                          // rest
    if (_attached) ledcWrite(SPK_LEDC_CH, 0);
    return;
  }
  if (!_attached) {
    ledcSetup(SPK_LEDC_CH, note.freq, 8);
    ledcAttachPin(CYD_SPK_PIN, SPK_LEDC_CH);
    _attached = true;
  } else {
    ledcWriteTone(SPK_LEDC_CH, note.freq);
  }
  ledcWrite(SPK_LEDC_CH, SPK_DUTY);
  digitalWrite(CYD_AMP_EN_PIN, LOW);             // amp on
}

void HalBeep::tone(uint16_t freq, uint16_t durMs) {
  if (durMs == 0) return;
  _seq[0] = { freq, durMs };
  _n   = 1;
  _idx = 0;
  _startNote(_seq[0]);
}

void HalBeep::play(const BeepNote* seq, uint8_t n) {
  if (!seq || n == 0) return;
  if (n > 8) n = 8;
  for (uint8_t i = 0; i < n; i++) _seq[i] = seq[i];
  _n   = n;
  _idx = 0;
  _startNote(_seq[0]);
}

void HalBeep::update() {
  if (!_playing) return;
  if ((int32_t)(millis() - _offAtMs) < 0) return;
  _idx++;
  if (_idx < _n) _startNote(_seq[_idx]);
  else           _silence();
}

// ---- HalRtc ------------------------------------------------------------
// Days-from-civil (Howard Hinnant) — newlib's timegm() exists but this
// keeps us independent of its presence and of the system TZ.
static uint32_t ymd_to_days(int y, int m, int d) {
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (uint32_t)(era * 146097 + (int)doe - 719468);
}

void HalRtc::_recompute() {
  uint32_t days = ymd_to_days(_y, _mon, _day);
  _epoch  = days * 86400u + _hh * 3600u + _mm * 60u + _ss;
  _baseMs = millis();
}

void HalRtc::_now(struct tm* out) {
  time_t t = (time_t)(_epoch + (millis() - _baseMs) / 1000);
  gmtime_r(&t, out);
}

void HalRtc::SetTime(const RTC_TimeTypeDef* t) {
  _hh = t->Hours; _mm = t->Minutes; _ss = t->Seconds;
  _haveTime = true;
  _recompute();
}

void HalRtc::SetDate(const RTC_DateTypeDef* d) {
  _y = d->Year; _mon = d->Month; _day = d->Date;
  _haveDate = true;
  _recompute();
}

void HalRtc::GetTime(RTC_TimeTypeDef* t) {
  struct tm tmv; _now(&tmv);
  t->Hours = tmv.tm_hour; t->Minutes = tmv.tm_min; t->Seconds = tmv.tm_sec;
}

void HalRtc::GetDate(RTC_DateTypeDef* d) {
  struct tm tmv; _now(&tmv);
  d->WeekDay = tmv.tm_wday;
  d->Month   = tmv.tm_mon + 1;
  d->Date    = tmv.tm_mday;
  d->Year    = tmv.tm_year + 1900;
}

// ---- Touch calibration -------------------------------------------------
#include <Preferences.h>

static void _tcLoadFromNvs() {
  Preferences p;
  p.begin("tcal", true);
  // v=2 stores the four corner raw points (3-point affine). Older v=1
  // records (simple range mapping) are ignored — re-running the modal
  // upgrades them cleanly.
  if (p.isKey("v") && p.getUChar("v", 0) == 2) {
    s_tcRx[0] = p.getShort("rx0", 230);  s_tcRy[0] = p.getShort("ry0", 230);
    s_tcRx[1] = p.getShort("rx1", 3900); s_tcRy[1] = p.getShort("ry1", 230);
    s_tcRx[2] = p.getShort("rx2", 3900); s_tcRy[2] = p.getShort("ry2", 3900);
    s_tcRx[3] = p.getShort("rx3", 230);  s_tcRy[3] = p.getShort("ry3", 3900);
    _tcRebuildBasis();
    s_tcLoaded = true;
    Serial.printf("[tcal] loaded TL(%d,%d) TR(%d,%d) BR(%d,%d) BL(%d,%d)\n",
                  s_tcRx[0],s_tcRy[0], s_tcRx[1],s_tcRy[1],
                  s_tcRx[2],s_tcRy[2], s_tcRx[3],s_tcRy[3]);
  }
  p.end();
}

static void _tcSaveToNvs() {
  Preferences p;
  p.begin("tcal", false);
  p.putUChar("v", 2);
  p.putShort("rx0", (int16_t)s_tcRx[0]); p.putShort("ry0", (int16_t)s_tcRy[0]);
  p.putShort("rx1", (int16_t)s_tcRx[1]); p.putShort("ry1", (int16_t)s_tcRy[1]);
  p.putShort("rx2", (int16_t)s_tcRx[2]); p.putShort("ry2", (int16_t)s_tcRy[2]);
  p.putShort("rx3", (int16_t)s_tcRx[3]); p.putShort("ry3", (int16_t)s_tcRy[3]);
  p.end();
  s_tcLoaded = true;
  Serial.printf("[tcal] saved  TL(%d,%d) TR(%d,%d) BR(%d,%d) BL(%d,%d)\n",
                s_tcRx[0],s_tcRy[0], s_tcRx[1],s_tcRy[1],
                s_tcRx[2],s_tcRy[2], s_tcRx[3],s_tcRy[3]);
}

// Average N stable raw readings while a finger is held — debounces the
// resistive panel's wobbly first-touch and last-touch samples.
static bool _tcSampleHeldTap(int& outRx, int& outRy) {
  // wait for a touch down
  while (!(tp.tirqTouched() && tp.touched())) { delay(5); }
  // accumulate up to N samples while touched
  const int N = 12;
  int sx = 0, sy = 0, n = 0;
  uint32_t deadline = millis() + 1500;   // give ~1.5s to settle
  while (n < N && (int32_t)(millis() - deadline) < 0) {
    if (tp.touched()) {
      TS_Point p = tp.getPoint();
      sx += p.x; sy += p.y; n++;
    }
    delay(25);
  }
  if (n == 0) return false;
  outRx = sx / n; outRy = sy / n;
  // wait for release (with timeout so a stuck-down panel can't hang us)
  uint32_t rel = millis() + 3000;
  while (tp.touched() && (int32_t)(millis() - rel) < 0) delay(20);
  delay(250);   // de-bounce gap before next target
  return true;
}

void touchCalibrate() {
  // Draw directly to the TFT — no sprite needed; the buddy's normal
  // render loop isn't running while we're in this modal.
  TFT_eSPI& tft = M5.Lcd;
  struct Tgt { int x, y; const char* label; };
  const Tgt targets[4] = {
    {  16,  16, "1/4 top-left"     },
    { 224,  16, "2/4 top-right"    },
    { 224, 304, "3/4 bottom-right" },
    {  16, 304, "4/4 bottom-left"  },
  };
  int rx[4] = {0}, ry[4] = {0};

  for (int i = 0; i < 4; i++) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("Touch Calibration", 120, 140);
    tft.setTextSize(1);
    tft.setTextColor(0xC618, TFT_BLACK);    // light grey
    tft.drawString(targets[i].label, 120, 168);
    tft.drawString("Hold the crosshair", 120, 186);
    tft.drawString("until it disappears", 120, 198);
    // Crosshair
    int tx = targets[i].x, ty = targets[i].y;
    tft.drawCircle(tx, ty, 14, TFT_RED);
    tft.drawCircle(tx, ty, 13, TFT_RED);
    tft.drawFastVLine(tx, ty - 18, 36, TFT_RED);
    tft.drawFastHLine(tx - 18, ty, 36, TFT_RED);
    tft.fillCircle(tx, ty, 3, TFT_RED);
    tft.setTextDatum(TL_DATUM);

    if (!_tcSampleHeldTap(rx[i], ry[i])) {
      // Couldn't get a stable sample — bail and keep whatever calibration
      // was loaded before.
      tft.fillScreen(TFT_BLACK);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(0xFA20, TFT_BLACK);
      tft.setTextSize(2);
      tft.drawString("Calibration aborted", 120, 160);
      tft.setTextDatum(TL_DATUM);
      delay(1500);
      return;
    }
    Serial.printf("[tcal] target %d (display %d,%d) -> raw (%d, %d)\n",
                  i, targets[i].x, targets[i].y, rx[i], ry[i]);
  }

  // Store the 4 raw corner points verbatim. The basis used by the touch
  // read (s_tcDxU/DyU/DxV/DyV/Det) is the 3-point affine from TL, TR, BL —
  // BR is recorded for future diagnostics but doesn't participate in the
  // transform.
  for (int i = 0; i < 4; i++) { s_tcRx[i] = rx[i]; s_tcRy[i] = ry[i]; }
  _tcRebuildBasis();
  Serial.printf("[tcal] basis dxU=%d dyU=%d dxV=%d dyV=%d det=%ld\n",
                s_tcDxU, s_tcDyU, s_tcDxV, s_tcDyV, s_tcDet);
  _tcSaveToNvs();

  // Confirmation
  TFT_eSPI& tft2 = M5.Lcd;
  tft2.fillScreen(TFT_BLACK);
  tft2.setTextDatum(MC_DATUM);
  tft2.setTextColor(TFT_GREEN, TFT_BLACK);
  tft2.setTextSize(2);
  tft2.drawString("Calibrated!", 120, 150);
  tft2.setTextColor(0xC618, TFT_BLACK);
  tft2.setTextSize(1);
  char b[40];
  snprintf(b, sizeof(b), "TL(%d,%d) TR(%d,%d)", rx[0], ry[0], rx[1], ry[1]);
  tft2.drawString(b, 120, 178);
  snprintf(b, sizeof(b), "BL(%d,%d) BR(%d,%d)", rx[3], ry[3], rx[2], ry[2]);
  tft2.drawString(b, 120, 192);
  tft2.setTextDatum(TL_DATUM);
  delay(2000);
}

// ---- M5Class -----------------------------------------------------------
void M5Class::begin() {
  Serial.begin(115200);
  Lcd.init();
  Lcd.setRotation(0);          // 240x320 portrait — matches main.cpp W/H
  Lcd.fillScreen(TFT_BLACK);
  Axp.begin();
  Beep.begin();

  touchSPI.begin(CYD_TOUCH_CLK, CYD_TOUCH_MISO, CYD_TOUCH_MOSI, CYD_TOUCH_CS);
  tp.begin(touchSPI);
  tp.setRotation(0);

  pinMode(CYD_LED_PIN, OUTPUT);
  digitalWrite(CYD_LED_PIN, HIGH);   // off (active low)

  // Load saved touch calibration, or run the 4-corner modal on first
  // boot so the keyboard / approval taps land where the user expects.
  _tcLoadFromNvs();
  if (!s_tcLoaded) {
    Serial.println("[tcal] no calibration stored — running modal");
    touchCalibrate();
  }
}

// Touch-zone layout on the 240x320 portrait panel. A press is latched to
// whichever zone it started in for its whole duration, so a stroke that
// drifts across a boundary still does what the user intended.
enum HalZone { Z_NONE, Z_A, Z_B, Z_POWER };

static HalZone classify(int x, int y) {
  if (x >= SCR_W - 46 && y <= 46) return Z_POWER;     // top-right corner
  if (x >= (int)(SCR_W * 0.62f))  return Z_B;         // right ~38% strip
  return Z_A;                                          // the rest
}

// Stroke summary — set on release, consumed by main.cpp.
static int           s_sx = 0, s_sy = 0;
static HalTouchEvent s_evt;
static bool          s_evtValid = false;

bool M5Class::consumeTouchEvent(HalTouchEvent* out) {
  if (!s_evtValid) return false;
  if (out) *out = s_evt;
  s_evtValid = false;
  return true;
}

void M5Class::suppressTouchActions() {
  // Re-run edge math with "up" state so any wasPressed/wasReleased flag
  // we already set for this release is wiped — main.cpp sees a clean slate.
  M5.BtnA._update(false, millis());
  M5.BtnB._update(false, millis());
}

// Synthetic touch injection — populated by halInjectTap / halInjectSwipe,
// consumed by M5Class::update() ahead of the real hardware read so the
// rest of the pipeline (zones, buttons, gestures) treats it identically.
static struct {
  bool     active   = false;
  bool     isSwipe  = false;
  int      x0 = 0, y0 = 0;
  int      x1 = 0, y1 = 0;
  uint32_t startMs  = 0;
  uint32_t endMs    = 0;
} s_injTouch;

void halInjectTap(int sx, int sy, uint32_t durMs) {
  s_injTouch.active  = true;
  s_injTouch.isSwipe = false;
  s_injTouch.x0 = s_injTouch.x1 = sx;
  s_injTouch.y0 = s_injTouch.y1 = sy;
  s_injTouch.startMs = millis();
  s_injTouch.endMs   = millis() + durMs;
}

void halInjectSwipe(int sx0, int sy0, int sx1, int sy1, uint32_t durMs) {
  s_injTouch.active  = true;
  s_injTouch.isSwipe = true;
  s_injTouch.x0 = sx0; s_injTouch.y0 = sy0;
  s_injTouch.x1 = sx1; s_injTouch.y1 = sy1;
  s_injTouch.startMs = millis();
  s_injTouch.endMs   = millis() + durMs;
}

void M5Class::update() {
  uint32_t now = millis();

  static bool     prevDown   = false;
  static uint32_t lastSeenMs = 0;
  static bool     everSeen   = false;
  static HalZone  zone       = Z_NONE;
  static uint32_t pressMs    = 0;

  bool raw = false;

  // Injected touch first — interpolates between (x0,y0) and (x1,y1) for
  // swipes, holds a point for taps. Auto-releases at endMs.
  if (s_injTouch.active) {
    if (now < s_injTouch.endMs) {
      raw = true;
      if (s_injTouch.isSwipe) {
        uint32_t span = s_injTouch.endMs - s_injTouch.startMs;
        float    t    = span ? (float)(now - s_injTouch.startMs) / (float)span : 1.0f;
        if (t > 1.0f) t = 1.0f;
        _tx = (int)(s_injTouch.x0 + (s_injTouch.x1 - s_injTouch.x0) * t);
        _ty = (int)(s_injTouch.y0 + (s_injTouch.y1 - s_injTouch.y0) * t);
      } else {
        _tx = s_injTouch.x0;
        _ty = s_injTouch.y0;
      }
      lastSeenMs = now;
      everSeen   = true;
    } else {
      s_injTouch.active = false;
    }
  }

  // If nothing injected, fall through to the real XPT2046 read.
  if (!raw && tp.tirqTouched() && tp.touched()) {
    TS_Point p = tp.getPoint();
    // 3-point affine inverse: solve for (u, v) in the basis where
    // u runs TL→TR and v runs TL→BL in raw space, then map (u, v) onto
    // the inset target rectangle on the display.
    int dx = p.x - s_tcRx[0];
    int dy = p.y - s_tcRy[0];
    int64_t u_num = (int64_t)dx * s_tcDyV - (int64_t)dy * s_tcDxV;
    int64_t v_num = (int64_t)dy * s_tcDxU - (int64_t)dx * s_tcDyU;
    const int sx_span = (SCR_W - 1) - 2 * TARGET_INSET;
    const int sy_span = (SCR_H - 1) - 2 * TARGET_INSET;
    int sx = TARGET_INSET + (int)((u_num * sx_span) / s_tcDet);
    int sy = TARGET_INSET + (int)((v_num * sy_span) / s_tcDet);
    _tx = constrain(sx, 0, SCR_W - 1);
    _ty = constrain(sy, 0, SCR_H - 1);
    lastSeenMs = now;
    everSeen   = true;
    raw        = true;
#if HAL_TOUCH_DEBUG
    Serial.printf("[touch] raw=(%d,%d) scr=(%d,%d)\n", p.x, p.y, _tx, _ty);
#endif
  }

  // Bridge the ~tens-of-ms dropouts a resistive panel has mid-press, so
  // hold-to-open-menu is reliable.
  bool down = everSeen && (raw || (now - lastSeenMs < 60));

  if (down && !prevDown) {            // touch-down: latch the zone + start
    zone    = classify(_tx, _ty);
    pressMs = now;
    s_sx    = _tx;
    s_sy    = _ty;
  }
  _touched = down;

  M5.BtnA._update(down && zone == Z_A, now);
  M5.BtnB._update(down && zone == Z_B, now);

  if (!down && prevDown) {            // release
    if (zone == Z_POWER && (now - pressMs) < 800) M5.Axp._firePowerTap();
    // Publish the stroke summary so main.cpp can recognise taps / swipes
    // and (if it wants) override the corresponding BtnA/B action.
    s_evt.sx = s_sx; s_evt.sy = s_sy;
    s_evt.ex = _tx;  s_evt.ey = _ty;
    s_evt.durMs = now - pressMs;
    s_evtValid = true;
    zone = Z_NONE;
  }
  prevDown = down;
}
