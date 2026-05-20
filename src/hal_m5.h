#pragma once
// ============================================================
// hal_m5.h — M5StickC Plus API shim for the CYD (ESP32-2432S028R)
//
// The buddy firmware was written against the M5StickC Plus: an ESP32
// with an AXP192 PMIC, a 6-axis IMU, an RTC, a buzzer, and two tactile
// buttons. The CYD has none of those — it has an ILI9341 320x240 panel,
// an XPT2046 resistive touchscreen, a speaker behind a GPIO4 amp, and
// one BOOT button. This header re-implements just enough of the `M5.*`
// surface the firmware actually uses, backed by CYD hardware, so the
// app and renderers compile and run unmodified.
//
// What changes behaviourally on the CYD:
//   * No accelerometer  -> shake/dizzy and face-down nap never trigger,
//                           the clock stays portrait (Imu reads "flat").
//   * No PMIC            -> battery telemetry is a best-effort ADC read,
//                           "power off" is deep-sleep (touch to wake).
//   * No RTC chip        -> time is held in the MCU, seeded by the BLE
//                           time-sync; survives until reboot, not power.
//   * No A/B buttons     -> a touch-zone layer drives virtual BtnA/BtnB
//                           and the "power" button (see hal_m5.cpp).
// ============================================================

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <esp_mac.h>
#include <esp_sleep.h>

// The firmware uses these bare color names (M5's display library exposed
// them). TFT_eSPI only ships the TFT_-prefixed set, so define the few
// the buddy code references. RGB565.
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

// ---- CYD pin map (ESP32-2432S028R, USB-C variant) ----------------------
// Display SPI pins come from build_flags (TFT_eSPI). These are the rest.
#define CYD_BL_PIN        21    // LCD backlight (PWM)
#define CYD_TOUCH_CLK     25
#define CYD_TOUCH_MISO    39
#define CYD_TOUCH_MOSI    32
#define CYD_TOUCH_CS      33
#define CYD_TOUCH_IRQ     36
#define CYD_SPK_PIN       26    // speaker via on-board amp
#define CYD_AMP_EN_PIN     4    // amp enable, ACTIVE LOW (shared w/ red LED)
// Attention indicator: the CYD RGB-LED red leg is GPIO4 (== amp enable),
// so use the BLUE leg instead. Active-low. main.cpp drives this directly.
#define CYD_LED_PIN       17

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

// ---- IMU stub: the CYD has no accelerometer ----------------------------
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
  // 0..100 -> backlight PWM duty. Mirrors M5.Axp.ScreenBreath().
  void ScreenBreath(int level);
  // LDO2 powered the M5 panel backlight; here it just gates the PWM.
  void SetLDO2(bool on);
  // No PMIC: best we can do is deep-sleep until the touch IRQ fires.
  void PowerOff();
  float GetBatVoltage();      // volts (ADC on GPIO34 if wired, else ~0)
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

// ---- Buzzer: LEDC tone on GPIO26, amp on GPIO4 (active low) ------------
// A note with freq=0 is a rest (silence for durMs). Sequences cap at 8 notes;
// playing a new sequence while one is in flight cuts it off.
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

// Runs the 4-corner touch calibration modal. Records raw XPT2046 values at
// each target tap, derives axis swap / inversion / range, persists to NVS,
// applies immediately. Auto-invoked from M5.begin() on first boot when no
// calibration is stored, and re-runnable from Settings → calibrate.
void touchCalibrate();

// Inject a synthetic touch into the HAL — used by tools/sim.py over USB
// serial to drive the UI remotely for screenshots and automated tests.
// The HAL routes injected coords through the same zone-classify / button-
// edge / gesture pipeline as real hardware touches, so anything that
// works under a finger works under sim.
void halInjectTap  (int sx, int sy, uint32_t durMs);
void halInjectSwipe(int sx0, int sy0, int sx1, int sy1, uint32_t durMs);

class M5Class {
 public:
  TFT_eSPI  Lcd;             // &M5.Lcd is a valid TFT_eSPI* for sprites
  HalButton BtnA;
  HalButton BtnB;
  HalImu    Imu;
  HalAxp    Axp;
  HalBeep   Beep;
  HalRtc    Rtc;

  void begin();
  void update();             // poll touch -> virtual buttons

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

 private:
  int  _tx = 0, _ty = 0;
  bool _touched = false;
};

extern M5Class M5;
