// Backlight/GPIO-expander/audio-enable control for the Elecrow CrowPanel
// AI 5.0" HMI's (SKU DHE04005D/DHE0400SD) onboard STC8H1KXX co-processor
// -- I2C address 0x2F. NOT interchangeable with
// elecrow-crowpanel-5-0-advance's Backlight_STC8H1K28.hpp: that board's
// co-processor is a different chip/firmware variant at address 0x30,
// using a single-command-byte protocol (0=brightest..245=off). This one
// uses a genuine register-address + value protocol instead.
//
// Shares the GT911 touch controller's I2C bus (SDA=45, SCL=46, I2C_NUM_0)
// -- see LGFX_Config.hpp.
//
// Protocol and register map confirmed from the vendor's own reference
// firmware (bsp_stc8h1kxx.c/.h, example/V1.0/Arduino_Code_5inch/
// Lesson07-Turn_on_the_screen/): simple single-register-address-byte +
// single-value-byte writes (i2c_write_reg(addr, reg, value)), no
// read-modify-write dance needed since each "offset" below is its own
// full register, not a shared bitfield.
//   0x00 + offset -- battery info read (5 bytes: adc_voltage,
//     bat_voltage, bat_level, bat_state, led_state) -- NOT implemented
//     here, out of scope for this board's initial display/touch/
//     backlight/sound bring-up.
//   0x10 + offset -- GPIO input get (offset 0 = UART/SPI switch detect)
//     -- NOT implemented here, not needed for Sidekick use.
//   0x18 + offset -- GPIO output set: 0=TP_RST (touch reset), 1=CSI_RST
//     (camera reset, unused here), 2=AUDIO_SD (speaker amp enable/
//     shutdown per this register's name -- see CrowPanelAIAudio.hpp's own
//     header for why real-hardware schematic tracing found this may not
//     actually gate the analog audio path at all on this board revision),
//     3=LCD_BL_POWER (backlight power rail on/off, separate from the PWM
//     brightness level below -- CONFIRMED on real hardware, unlike
//     AUDIO_SD).
//   0x20 + offset -- PWM set: 0=LCD_BL_EN (backlight brightness). Value
//     is 0-100 PERCENTAGE -- NOT 0-255, NOT inverted. Confirmed via the
//     vendor's own .ino calls: stc8_set_pwm_duty(STC8_PWM_LCD_BL_EN, 0)
//     for off, (..., 100) for full brightness, called AFTER real content
//     is already drawn to avoid a visible flash of an uninitialized
//     framebuffer at boot.
//
// Reuses LovyanGFX's own low-level lgfx::i2c:: driver (the exact same one
// Touch_GT911 already brings up on this port/pins) instead of a second,
// independent Arduino Wire instance on the same physical pins -- same
// driver-stack-collision concern documented in
// elecrow-crowpanel-5-0-advance/Backlight_STC8H1K28.hpp's own header
// (this project has a confirmed real bug where two different I2C driver
// stacks fighting over the same ESP32 port can hang solid). Since
// Touch_GT911::init() already calls lgfx::i2c::init() for this same
// (port, sda, scl) before setup() ever reaches here, this file only ever
// piggybacks on that already-initialized port -- never installs its own.
//
// Touch reset (TP_RST, offset 0) is deliberately NOT wired up as a real
// function here yet -- this board's GT911 also has a direct ESP32 GPIO36
// reset pin (see LGFX_Config.hpp), matching LovyanGFX's native
// Touch_GT911 convention already used elsewhere in this catalog. The plan
// is to rely on that direct pin first; if touch doesn't respond on real
// hardware, a TP_RST "kick" through this co-processor (mirroring
// Backlight_STC8H1K28.hpp's own documented fallback-kick pattern) is the
// next thing to try -- setGpioOutput() below is exposed generically so
// that fallback can be added without a protocol change.
#pragma once

#include <Arduino.h>
#include "lgfx/v1/platforms/common.hpp"

namespace stc8ai {

constexpr int kI2CPort = I2C_NUM_0;
constexpr uint8_t kAddr = 0x2F;
constexpr uint32_t kFreq = 400000;

constexpr uint8_t kRegGpioOut = 0x18;
constexpr uint8_t kRegPwm = 0x20;

constexpr uint8_t kGpioTpRst = 0;
constexpr uint8_t kGpioCsiRst = 1;
constexpr uint8_t kGpioAudioSd = 2;
constexpr uint8_t kGpioLcdBlPower = 3;

constexpr uint8_t kPwmLcdBlEn = 0;

// mask=0xFF -- full-byte replace, not a bit-preserving read-modify-write.
// Each register here is single-purpose (see file header), so there's
// nothing to preserve.
inline bool writeReg(uint8_t reg, uint8_t value) {
    return lgfx::i2c::writeRegister8(kI2CPort, kAddr, reg, value, (uint8_t)0xFF, kFreq).has_value();
}

inline bool setGpioOutput(uint8_t offset, bool value) {
    return writeReg(kRegGpioOut + offset, value ? 1 : 0);
}

// level: 0-100 percentage, 0=off, 100=full brightness. NOT inverted, NOT
// 0-255 -- see file header.
inline void setBrightness(uint8_t level) {
    if (level > 100) level = 100;
    // TEMPORARY diagnostic -- brightness commands are reaching this
    // function (confirmed via applyBrightness()'s own debug print in
    // main.cpp) but have no visible effect on real hardware. Logging the
    // I2C write's own success/failure to tell apart "write is silently
    // failing" from "write succeeds but the co-processor doesn't act on
    // it" -- remove once the real cause is found.
    bool ok = writeReg(kRegPwm + kPwmLcdBlEn, level);
    Serial.printf("[stc8ai] setBrightness(%u) -> %s\n", (unsigned)level, ok ? "ok" : "FAILED");
}

inline bool setBacklightPower(bool on) { return setGpioOutput(kGpioLcdBlPower, on); }

// AUDIO_SD is ACTIVE-LOW -- confirmed via the vendor's own board_config.h
// (Lesson12-Playing_Loca_Music_from_SD_Card): "#define AUDIO_POWER_ENABLE
// (LOW)" / "#define AUDIO_POWER_DISABLE (HIGH)". An earlier version of
// this function wrote enable straight through (true->1/HIGH), which per
// the vendor's own convention actually DISABLES the amp -- a real,
// necessary fix, though NOT by itself sufficient to produce audible
// sound on real hardware (still silent after this fix alone; the I2S
// driver itself also needed to switch from the legacy driver/i2s.h API
// to ESP_I2S.h/I2SClass -- see CrowPanelAIAudio.hpp's own header).
// setGpioOutput(offset, !enable) inverts at the call site so callers can
// still reason in plain "true=audible" terms. Returns the underlying I2C
// write's success/failure for debugging.
inline bool setAudioEnable(bool enable) { return setGpioOutput(kGpioAudioSd, !enable); }

// Doubles as a safe, idempotent real command (audio amp disabled -- the
// correct boot-time default until CrowPanelAIAudio::init() explicitly
// enables it) rather than a read-only probe, same "the probe IS a real
// harmless command" approach Backlight_STC8H1K28.hpp's own detected()
// uses.
inline bool detected() {
    return writeReg(kRegGpioOut + kGpioAudioSd, 0);
}

// Bounded retry (not an infinite loop) -- if the co-processor genuinely
// never responds, the board still boots; backlight simply stays at
// whatever its own power-on default happens to be and sound stays
// disabled, rather than hanging the whole device.
inline bool begin() {
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (detected()) return true;
        delay(50);
    }
    return false;
}

} // namespace stc8ai
