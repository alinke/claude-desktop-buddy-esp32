// Backlight/buzzer/speaker control for the Elecrow CrowPanel Advance 5.0"
// HMI's onboard STC8H1K28 microcontroller -- this board has NO backlight
// GPIO at all (see LGFX_Config.hpp's own header comment); brightness is
// instead commanded over I2C to this co-processor at address 0x30, on the
// SAME bus/pins (SDA=15, SCL=16) the GT911 touch controller uses.
//
// Deliberately reuses LovyanGFX's own low-level lgfx::i2c:: driver (the
// exact same one Touch_GT911 already brings up on I2C_NUM_0) instead of a
// second, independent Arduino Wire instance on the same physical pins --
// this project already has a confirmed real bug where two different I2C
// driver stacks fighting over the same ESP32 port can hang solid (see
// jc8012p4a1c/Touch_GSL3680_Wire.hpp's own header: LGFX's lgfx::i2c::
// hangs on ESP_ERR_INVALID_STATE, which a second i2c_driver_install on an
// already-installed port is exactly the kind of condition that can
// trigger). Since Touch_GT911::init() already calls lgfx::i2c::init() for
// this same (port, sda, scl) before setup() ever reaches here, this file
// only ever piggybacks on that already-initialized port -- never installs
// its own.
//
// Command values and the vendor's own "not detected yet" fallback kick
// sequence are straight from Elecrow's own factory main.cpp
// (example/V1.2_and_V1.3/PlatformIO/src/main.cpp, the "latest" hardware
// revision per their readme's own version table) -- 0=brightest,
// 244=dimmest, 245=off (0-245 range); 250 is their own documented
// "activate touch screen" kick sent when the co-processor doesn't ACK on
// first scan, paired with a brief LOW pulse on GPIO1 (an otherwise
// undocumented pin in their reference -- kept as-is since it's real vendor
// behavior, not independently reverse-engineered, and only ever fires
// after detection has already failed).
//
// THIS FILE IS v1.2/v1.3 ONLY. A real customer reported a v1.1 board (this
// project has never had one in hand) -- per Elecrow's own readme.md version
// table, v1.1's STC8H1K28 speaks a DIFFERENT, coarser protocol on the same
// I2C address (0x30): only 6 discrete brightness steps (0x05=off, 0x10=max,
// vs. this file's fine 0-245 range), a different "not detected" kick byte
// (0x19, not 250), and separate buzzer (0x15/0x16) and speaker (0x17/0x18)
// commands where v1.2/1.3 only exposes one combined buzzer control
// (246/247). Confirmed via Elecrow's own v1.1 example source
// (example/V1.1/PlatformIO/.../src/main.cpp in the same GitHub repo) -- the
// RGB panel pins/timing and GT911 touch config are IDENTICAL across v1.1
// and v1.2/v1.3 (verified by diffing the actual driver headers, not just
// the readme text), so only the backlight protocol needed a separate file:
// see Backlight_STC8H1K28_V1_1.hpp, wired in via the
// `elecrow-crowpanel-5-0-advance-v1_1` PlatformIO env, and reports its own
// distinct catalog ID (esp32-5-elecrow-advance-v1-1) -- CONFIRMED working
// on real v1.1 hardware by that customer.
#pragma once

#include <Arduino.h>
#include "lgfx/v1/platforms/common.hpp"

namespace stc8h1k28 {

constexpr int kI2CPort = I2C_NUM_0;
constexpr uint8_t kAddr = 0x30;
constexpr uint32_t kFreq = 400000;
constexpr uint8_t kBrightest = 0; // this hardware rev (v1.2/v1.3): 0 = brightest
constexpr uint8_t kOff = 245;     // this hardware rev (v1.2/v1.3): 245 = backlight off

inline bool detected() {
    uint8_t probe = 0; // command 0 doubles as a harmless "set brightest" probe
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
        // Vendor's own documented kick: command 250 ("activate touch
        // screen"), then briefly pull GPIO1 low.
        uint8_t kick = 250;
        lgfx::i2c::transactionWrite(kI2CPort, kAddr, &kick, 1, kFreq);
        pinMode(1, OUTPUT);
        digitalWrite(1, LOW);
        delay(120);
        pinMode(1, INPUT);
        delay(100);
    }
    return false;
}

// level: 0 = brightest, 244 = dimmest, 245 = backlight off.
inline void setBrightness(uint8_t level) {
    lgfx::i2c::transactionWrite(kI2CPort, kAddr, &level, 1, kFreq);
}

} // namespace stc8h1k28
