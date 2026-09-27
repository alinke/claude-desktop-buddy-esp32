// Backlight/buzzer/speaker control for the Elecrow CrowPanel Advance 5.0"
// HMI's onboard STC8H1K28 microcontroller -- HARDWARE REVISION v1.1
// variant. See Backlight_STC8H1K28.hpp (the v1.2/v1.3 file this project's
// own bench unit uses) for the full background on why this co-processor
// exists and the I2C-driver-sharing rationale; that header's own comment
// also explains why this separate file exists at all.
//
// CONFIRMED WORKING on real v1.1 hardware. This project has never had a
// v1.1 board in hand itself -- a customer reported getting one instead of
// the v1.3 this project originally developed against, and this file was
// written directly from Elecrow's own v1.1 vendor source
// (example/V1.1/PlatformIO/platfromIO_70_50_43/src/main.cpp in
// Elecrow-RD/CrowPanel-Advance-5-HMI-ESP32-S3-AI-Powered-IPS-Touch-Screen-
// 800x480) for that customer to bench-test -- confirmed by that customer:
// backlight lights up correctly, no black screen. If a future report says
// otherwise, the vendor main.cpp above is the next thing to re-check
// line-by-line, not these values re-guessed.
//
// Same I2C address (0x30) as v1.2/v1.3, but a coarser, different command
// set confirmed straight from that vendor main.cpp (which matches their
// readme.md version-table description exactly):
//   - Brightness: exactly 6 discrete commands -- 0x05 (off), 0x06, 0x07,
//     0x08, 0x09, 0x10 (brightest). NOT a continuous 0x05-0x10 range: the
//     last one is hex 0x10, nothing exists between 0x09 and 0x10 (Elecrow's
//     factory v1.1 HMI-bigInch5.ino PWM(): '1'..'6' -> 05,06,07,08,09,10).
//     Elecrow says to send 0x10 before switching to another level. Not the
//     fine 0-245 range v1.2/v1.3 uses. See kLevels/setBrightnessPercent().
//   - "Not detected yet" kick command: 0x19 ("activate touch screen" per
//     the vendor's own comment), not v1.2/v1.3's 250 -- paired with the
//     same brief LOW pulse on GPIO1 the v1.2/v1.3 file also does (that part
//     IS confirmed identical in the vendor's v1.1 main.cpp).
//   - Buzzer: 0x15 on / 0x16 off (separate from the speaker, unlike
//     v1.2/v1.3's single combined 246/247 buzzer control). Speaker: 0x17
//     on / 0x18 off. Neither is wired into this project's firmware today
//     (main.cpp has no buzzer/speaker feature on this board yet), so these
//     are provided but unused -- kept here since they're real, confirmed
//     vendor values, not because anything currently calls them.
//
// RGB panel pins/timing and GT911 touch config are IDENTICAL to v1.2/v1.3
// (confirmed by diffing the vendor's own v1.1 LovyanGFX_Driver.h against
// the v1.2/v1.3 one -- same pins, same 18MHz/4/8/8 timing, same GT911 on
// I2C_NUM_0 SDA=15/SCL=16 addr=0x5D), so LGFX_Config.hpp is shared
// unchanged between both hardware revisions -- only this backlight file
// differs.
#pragma once

#include <Arduino.h>
#include "lgfx/v1/platforms/common.hpp"

namespace stc8h1k28 {

constexpr int kI2CPort = I2C_NUM_0;
constexpr uint8_t kAddr = 0x30;
constexpr uint32_t kFreq = 400000;
constexpr uint8_t kBrightest = 0x10; // v1.1: 0x10 = brightest
constexpr uint8_t kOff = 0x05;       // v1.1: 0x05 = backlight off
constexpr uint8_t kBuzzerOn = 0x15;
constexpr uint8_t kBuzzerOff = 0x16;
constexpr uint8_t kSpeakerOn = 0x17;
constexpr uint8_t kSpeakerOff = 0x18;

inline bool detected() {
    // kBrightest doubles as a harmless "set brightest" probe, same pattern
    // as the v1.2/v1.3 file's command-0 probe.
    uint8_t probe = kBrightest;
    return lgfx::i2c::transactionWrite(kI2CPort, kAddr, &probe, 1, kFreq).has_value();
}

// begin() assumes Touch_GT911::init() (or some other lgfx::i2c:: caller on
// this same port) has already run lgfx::i2c::init() for I2C_NUM_0/pins
// 15/16 -- call this AFTER lcd.init()/lcd.begin(), not before. Bounded
// retry (not the vendor's own infinite while(1)) -- if the co-processor
// genuinely never responds, the board still boots and simply shows
// whatever brightness its own power-on default happens to be, rather than
// hanging the whole device.
inline bool begin() {
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (detected()) {
            return true;
        }
        // Vendor's own documented v1.1 kick: command 0x19 ("activate touch
        // screen"), then briefly pull GPIO1 low -- same GPIO1 dance as
        // v1.2/v1.3, just a different kick byte.
        uint8_t kick = 0x19;
        lgfx::i2c::transactionWrite(kI2CPort, kAddr, &kick, 1, kFreq);
        pinMode(1, OUTPUT);
        digitalWrite(1, LOW);
        delay(120);
        pinMode(1, INPUT);
        delay(100);
    }
    return false;
}

// Sends one raw command byte. Returns false if the I2C write failed.
inline bool setBrightness(uint8_t level) {
    return lgfx::i2c::transactionWrite(kI2CPort, kAddr, &level, 1, kFreq).has_value();
}

// The five lit levels, dimmest to brightest (off is kOff).
constexpr uint8_t kLevels[5] = {0x06, 0x07, 0x08, 0x09, 0x10};

// 0-100% -> off, or one of the five lit levels (1-20%, 21-40%, 41-60%,
// 61-80%, 81-100%). Fixed 2026-09-26 from a customer report on real v1.1
// hardware: the old code treated 0x05-0x10 as a continuous range
// (5 + 11*p/100), so 1-9% sent 0x05 (screen OFF) and 46-99% sent
// undocumented 0x0A-0x0F; 20% (0x07, a valid level) also didn't visibly
// dim, which fits the missing 0x10 priming step. Their patched unit with
// both fixes dims correctly at 5/20/40/60/80/100%. No priming before off:
// priming would flash the backlight to full for a moment first.
// Returns false if an I2C write failed.
inline bool setBrightnessPercent(uint8_t percent) {
    if (percent == 0) {
        return setBrightness(kOff);
    }
    if (percent > 100) percent = 100;
    uint8_t level = kLevels[(percent - 1) / 20];
    bool ok = true;
    if (level != kBrightest) {
        ok = setBrightness(kBrightest); // Elecrow: send 0x10 before another level
    }
    return setBrightness(level) && ok;
}

} // namespace stc8h1k28
