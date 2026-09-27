#pragma once
// ============================================================
// hal_m5.h — M5StickC Plus API shim over LovyanGFX.
//
// The buddy firmware was written against the M5StickC Plus: an ESP32
// with an AXP192 PMIC, a 6-axis IMU, an RTC, a buzzer, and two tactile
// buttons. None of the boards this fork targets have those — they have a
// touch panel and (sometimes) a speaker and an RGB LED. This header
// re-implements just enough of the `M5.*` surface the firmware uses, backed
// by whatever the board has (see board.h), so the app code stays close to
// upstream.
//
// What changes behaviourally:
//   * No accelerometer  -> shake/dizzy and face-down nap never trigger.
//   * No PMIC            -> battery telemetry is a best-effort ADC read
//                           where the board has a divider; "power off" is
//                           deep sleep on boards with a touch IRQ to wake
//                           from, and just the backlight off elsewhere.
//   * No RTC chip        -> time is held in the MCU, seeded by the BLE
//                           time-sync; survives until reboot, not power.
//   * No A/B buttons     -> a touch-zone layer drives virtual BtnA/BtnB
//                           and the "power" button (see hal_m5.cpp).
//
// Touch coordinates reported by M5.touch() / consumeTouchEvent() are in
// the Canvas's logical space (see canvas.h), same as the drawing API.
// ============================================================

#include <Arduino.h>
#include <esp_mac.h>
#include <esp_sleep.h>
#include "board.h"

// The firmware uses these bare colour names (M5's display library exposed
// them). RGB565.
#ifndef GREEN
#define GREEN 0x07E0
#endif
#ifndef RED
#define RED 0xF800
#endif

// M5's RTC component structs. Field order/types match what data.h and
// main.cpp construct and read (Hours/Minutes/Seconds, WeekDay/Month/
// Date/Year).
typedef struct {
  uint8_t Hours;
  uint8_t Minutes;
  uint8_t Seconds;
} RTC_TimeTypeDef;

typedef struct {
  uint8_t  WeekDay;   // 0=Sun .. 6=Sat
  uint8_t  Month;     // 1..12
  uint8_t  Date;      // 1..31
  uint16_t Year;      // full year, e.g. 2026
} RTC_DateTypeDef;

// ---- Virtual button (M5.BtnA / M5.BtnB semantics) ----------------------
class HalButton {
 public:
  // Called by M5.update() with the debounced physical state.
  void _update(bool down, uint32_t now);
  bool isPressed() const { return _state; }
  bool wasPressed() const { return _wasPressed; }
  bool wasReleased() const { return _wasReleased; }
  bool pressedFor(uint32_t ms) const { return _state && (millis() - _downMs) >= ms; }

 private:
  bool     _state      = false;
  bool     _last       = false;
  bool     _wasPressed = false;
  bool     _wasReleased= false;
  uint32_t _downMs     = 0;
};

// ---- IMU stub: none of the boards has an accelerometer -----------------
// Reports a perfectly flat, screen-up orientation so shake detection,
// face-down nap, and clock auto-rotate all stay quiescent.
class HalImu {
 public:
  void Init() {}
  void getAccelData(float* ax, float* ay, float* az) {
    if (ax) *ax = 0.0f;
    if (ay) *ay = 0.0f;
    if (az) *az = 1.0f;
  }
};

// ---- AXP192 stand-in: backlight + best-effort battery + sleep ----------
class HalAxp {
 public:
  void begin();
  // 0..100 -> backlight. Mirrors M5.Axp.ScreenBreath().
  void ScreenBreath(int level);
  // LDO2 powered the M5 panel backlight; here it just gates the backlight.
  void SetLDO2(bool on);
  // Deep-sleeps until the touch IRQ fires, on boards that have one wired.
  // Returns false (and does nothing) on boards that can't wake that way —
  // the caller should just blank the screen instead.
  bool PowerOff();
  float GetBatVoltage();      // volts (ADC where the board has a divider, else 0)
  float GetBatCurrent();      // mA (unknown -> 0)
  float GetVBusVoltage();     // volts; ~5.0 so "on USB" UI stays true
  float GetTempInAXP192();    // °C, MCU internal sensor (rough)
  // Returns 0x02 once after a tap in the power touch-zone, else 0.
  uint8_t GetBtnPress();

  // Called by the touch layer.
  void _firePowerTap() { _powerEvent = true; }

 private:
  int  _level     = 80;       // last ScreenBreath value
  bool _on         = true;
  bool _powerEvent = false;
};

// ---- Speaker: LEDC tone on the board's speaker pin, amp active low -----
// A note with freq=0 is a rest (silence for durMs). Sequences cap at 8 notes;
// playing a new sequence while one is in flight cuts it off. A no-op on
// boards without a speaker pin.
struct BeepNote { uint16_t freq; uint16_t durMs; };

class HalBeep {
 public:
  void begin();
  void tone(uint16_t freq, uint16_t durMs);          // single-note convenience
  void play(const BeepNote* seq, uint8_t n);         // multi-note sequence
  void update();                                      // advance/silence on expiry

 private:
  void _startNote(const BeepNote& n);
  void _silence();

  bool     _attached = false;
  bool     _playing  = false;
  uint32_t _offAtMs  = 0;
  BeepNote _seq[8];
  uint8_t  _n        = 0;
  uint8_t  _idx      = 0;
};

// ---- RTC backed by the MCU clock ---------------------------------------
// data.h hands us already-localized time components from the BLE sync.
// We hold them in software (epoch + millis base) and replay on read;
// good until reboot, which is exactly the M5 coin-cell behaviour minus
// the coin cell.
class HalRtc {
 public:
  void SetTime(const RTC_TimeTypeDef* t);
  void SetDate(const RTC_DateTypeDef* d);
  void GetTime(RTC_TimeTypeDef* t);
  void GetDate(RTC_DateTypeDef* d);

 private:
  void     _recompute();
  void     _now(struct tm* out);
  // Pending components (filled by SetTime/SetDate in either order).
  int      _y = 2000, _mon = 1, _day = 1, _hh = 0, _mm = 0, _ss = 0;
  bool     _haveTime = false, _haveDate = false;
  uint32_t _epoch = 0;       // seconds, treated as UTC (already localized)
  uint32_t _baseMs = 0;
};

// Stroke summary captured at each touch release — start/end coords + duration.
// Lets main.cpp recognise taps vs swipes and override the default BtnA/B
// release behaviour for those gestures.
struct HalTouchEvent { int sx; int sy; int ex; int ey; uint32_t durMs; };

// Resistive boards: runs LovyanGFX's 4-corner calibration and persists the
// result to NVS. Auto-invoked from M5.begin() on first boot, re-runnable
// from Settings → calibrate. Capacitive boards need none; there it only
// shows a short "not needed" note.
void touchCalibrate();
bool touchNeedsCalibration();

// Inject a synthetic touch into the HAL — used by tools/sim.py over USB
// serial to drive the UI remotely for screenshots and automated tests.
// Coordinates are logical. The HAL routes injected coords through the same
// zone-classify / button-edge / gesture pipeline as real hardware touches.
void halInjectTap  (int sx, int sy, uint32_t durMs);
void halInjectSwipe(int sx0, int sy0, int sx1, int sy1, uint32_t durMs);

class M5Class {
 public:
  LGFX      Lcd;
  HalButton BtnA;
  HalButton BtnB;
  HalImu    Imu;
  HalAxp    Axp;
  HalBeep   Beep;
  HalRtc    Rtc;

  // Brings up the panel at `rotation` (LovyanGFX 0..3).
  void begin(uint8_t rotation);
  void update();             // poll touch -> virtual buttons

  // Attention LED (active low on the boards that have one). No-op elsewhere.
  void setLed(bool on);

  // Called once the Canvas knows the logical geometry: maps physical touch
  // points into logical space and sizes the tap zones.
  void setGeometry(int w, int h, int k, int ox, int oy);
  // X (logical) where the A zone ends and the B zone begins. Defaults to
  // 62% of the width; landscape layouts move it to line up with their own
  // approve/deny split.
  void setZoneSplit(int x) { _zoneSplit = x; }

  // Optional: latest mapped touch point (for on-screen feedback).
  bool touch(int& x, int& y) const { x = _tx; y = _ty; return _touched; }

  // Returns true exactly once per touch release, filling `out` with the
  // stroke summary. Call this each loop *after* M5.update() and *before*
  // the BtnA/B handlers — it does not consume the virtual button edges
  // on its own (call suppressTouchActions() to do that).
  bool consumeTouchEvent(HalTouchEvent* out);
  // Discard any pending BtnA/B wasPressed/wasReleased flags from the
  // current release. Used by main.cpp when a gesture (pet-tap, swipe)
  // claimed the stroke so the underlying virtual button doesn't also fire.
  void suppressTouchActions();

  // Logical geometry, for the HAL's own use (zones, clamping).
  int  _w = 240, _h = 320, _k = 1, _ox = 0, _oy = 0;
  int  _zoneSplit = 148;

 private:
  int  _tx = 0, _ty = 0;
  bool _touched = false;
};

extern M5Class M5;
