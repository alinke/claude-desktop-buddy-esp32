// ============================================================
// hal_m5.cpp — LovyanGFX-backed implementation of the M5 shim.
// See hal_m5.h for the rationale and the behavioural deltas.
// ============================================================
#include "hal_m5.h"
#include <Preferences.h>

// Flip to 1 to log physical + logical touch points while debugging.
#define HAL_TOUCH_DEBUG 0

M5Class M5;

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
  _on = true;
  ScreenBreath(_level);
}

void HalAxp::ScreenBreath(int level) {
  if (level < 0)   level = 0;
  if (level > 100) level = 100;
  _level = level;
  if (_on) boardSetBrightness(M5.Lcd, (uint8_t)level);
}

void HalAxp::SetLDO2(bool on) {
  _on = on;
  boardSetBrightness(M5.Lcd, on ? (uint8_t)_level : 0);
}

bool HalAxp::PowerOff() {
#if BUDDY_WAKE_PIN >= 0
  // No PMIC to cut power. Blank the panel and deep sleep until the touch
  // IRQ goes low, which wakes via a clean reboot.
  boardSetBrightness(M5.Lcd, 0);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BUDDY_WAKE_PIN, 0);
  esp_deep_sleep_start();
  return true;
#else
  return false;
#endif
}

float HalAxp::GetBatVoltage() {
#if BUDDY_BAT_ADC_PIN >= 0
  // VBAT through a 2:1 divider on the variants that wire a battery at all.
  // Returns ~0 (or a floating reading) on USB-only units.
  uint32_t mv = analogReadMilliVolts(BUDDY_BAT_ADC_PIN);
  return (mv * 2.0f) / 1000.0f;
#else
  return 0.0f;
#endif
}
float HalAxp::GetBatCurrent()    { return 0.0f; }
float HalAxp::GetVBusVoltage()   { return 5.0f; }   // USB-powered: keep "on USB" true
float HalAxp::GetTempInAXP192()  { return temperatureRead(); }

uint8_t HalAxp::GetBtnPress() {
  if (_powerEvent) { _powerEvent = false; return 0x02; }
  return 0;
}

// ---- HalBeep -----------------------------------------------------------
#define SPK_DUTY 128   // 50% square wave

void HalBeep::begin() {
#if BUDDY_AMP_EN_PIN >= 0
  pinMode(BUDDY_AMP_EN_PIN, OUTPUT);
  digitalWrite(BUDDY_AMP_EN_PIN, HIGH);   // amp off (active low)
#endif
}

void HalBeep::_silence() {
#if BUDDY_SPK_PIN >= 0
  if (_attached) ledcWrite(BUDDY_SPK_PIN, 0);
#endif
#if BUDDY_AMP_EN_PIN >= 0
  digitalWrite(BUDDY_AMP_EN_PIN, HIGH);
#endif
  _playing = false;
}

void HalBeep::_startNote(const BeepNote& note) {
  _offAtMs = millis() + note.durMs;
  _playing = true;
#if BUDDY_SPK_PIN >= 0
  if (note.freq == 0) {                          // rest
    if (_attached) ledcWrite(BUDDY_SPK_PIN, 0);
    return;
  }
  if (!_attached) {
    _attached = ledcAttach(BUDDY_SPK_PIN, note.freq, 8);
    if (!_attached) return;
  } else {
    ledcChangeFrequency(BUDDY_SPK_PIN, note.freq, 8);
  }
  ledcWrite(BUDDY_SPK_PIN, SPK_DUTY);
#if BUDDY_AMP_EN_PIN >= 0
  digitalWrite(BUDDY_AMP_EN_PIN, LOW);           // amp on
#endif
#endif
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

// ---- Touch calibration (resistive boards) ------------------------------
// LovyanGFX's calibrateTouch() draws a crosshair in each corner, waits for
// a tap on each, and returns 8 values that setTouchCalibrate() applies to
// every later getTouch(). It calibrates in the touch controller's native
// orientation and applies the screen rotation afterwards, so one stored
// calibration covers every rotation.
static const char* TCAL_KEY = "lgfx";

static bool _tcLoad() {
  uint16_t params[8];
  Preferences p;
  p.begin("tcal", true);
  size_t n = p.getBytes(TCAL_KEY, params, sizeof(params));
  // Early builds of this fork stored it per rotation ("p0".."p3").
  for (int r = 0; r < 4 && n != sizeof(params); r++) {
    char k[4]; snprintf(k, sizeof(k), "p%d", r);
    n = p.getBytes(k, params, sizeof(params));
  }
  p.end();
  if (n != sizeof(params)) return false;
  M5.Lcd.setTouchCalibrate(params);
  Serial.println("[tcal] loaded");
  return true;
}

bool touchNeedsCalibration() {
#if BUDDY_TOUCH_RESISTIVE
  Preferences p;
  p.begin("tcal", true);
  bool has = p.isKey(TCAL_KEY);
  p.end();
  return !has;
#else
  return false;
#endif
}

void touchCalibrate() {
  LGFX& lcd = M5.Lcd;
  int cx = lcd.width() / 2, cy = lcd.height() / 2;
  int ts = lcd.width() >= 480 ? 2 : 1;
#if BUDDY_TOUCH_RESISTIVE
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setTextSize(2 * ts);
  lcd.drawString("Touch Calibration", cx, cy - 20 * ts);
  lcd.setTextSize(ts);
  lcd.setTextColor(0xC618, TFT_BLACK);
  lcd.drawString("Tap each corner marker", cx, cy + 6 * ts);
  lcd.drawString("as it appears", cx, cy + 18 * ts);
  uint16_t params[8];
  lcd.calibrateTouch(params, TFT_RED, TFT_BLACK, 16);
  Preferences p;
  p.begin("tcal", false);
  p.putBytes(TCAL_KEY, params, sizeof(params));
  p.end();
  Serial.println("[tcal] saved");
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  lcd.setTextSize(2 * ts);
  lcd.drawString("Calibrated!", cx, cy);
#else
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextDatum(middle_center);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setTextSize(2 * ts);
  lcd.drawString("Capacitive touch", cx, cy - 10 * ts);
  lcd.setTextSize(ts);
  lcd.setTextColor(0xC618, TFT_BLACK);
  lcd.drawString("no calibration needed", cx, cy + 12 * ts);
#endif
  lcd.setTextDatum(top_left);
  delay(1500);
}

// ---- M5Class -----------------------------------------------------------
void M5Class::begin(uint8_t rotation) {
  boardPreInit();
  Lcd.init();
  boardPostInit(Lcd);
  Lcd.setRotation(rotation);
  Lcd.fillScreen(TFT_BLACK);
  Axp.begin();
  Beep.begin();
  setLed(false);

#if BUDDY_TOUCH_RESISTIVE
  // Load saved calibration, or run the modal on first boot so taps land
  // where the user expects.
  if (!_tcLoad()) {
    Serial.println("[tcal] no calibration stored — running modal");
    touchCalibrate();
  }
#endif
}

void M5Class::setLed(bool on) {
#if BUDDY_LED_PIN >= 0
  digitalWrite(BUDDY_LED_PIN, on ? LOW : HIGH);   // active low
#else
  (void)on;
#endif
}

void M5Class::setGeometry(int w, int h, int k, int ox, int oy) {
  _w = w; _h = h; _k = k; _ox = ox; _oy = oy;
  _zoneSplit = (int)(w * 0.62f);
}

// Touch zones. A press is latched to whichever zone it started in for its
// whole duration, so a stroke that drifts across a boundary still does what
// the user intended.
enum HalZone { Z_NONE, Z_A, Z_B, Z_POWER };

static HalZone classify(int x, int y) {
  if (x >= M5._w - 46 && y <= 46) return Z_POWER;     // top-right corner
  if (x >= M5._zoneSplit)         return Z_B;         // right strip
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

  // If nothing injected, fall through to the real touch read. LovyanGFX
  // returns rotated, calibrated panel coordinates; map them into the
  // Canvas's logical space.
  if (!raw) {
    int32_t px, py;
    if (Lcd.getTouch(&px, &py)) {
      int lx = (px - _ox) / _k;
      int ly = (py - _oy) / _k;
      _tx = constrain(lx, 0, _w - 1);
      _ty = constrain(ly, 0, _h - 1);
      lastSeenMs = now;
      everSeen   = true;
      raw        = true;
#if HAL_TOUCH_DEBUG
      Serial.printf("[touch] phys=(%ld,%ld) logical=(%d,%d)\n", (long)px, (long)py, _tx, _ty);
#endif
    }
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
