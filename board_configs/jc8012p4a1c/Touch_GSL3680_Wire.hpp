// Custom GSL3680 touch driver using plain Arduino Wire calls, NOT
// LovyanGFX's own lgfx::i2c:: namespace.
//
// Why this exists instead of just using LovyanGFX's own Touch_GSLx680:
// real-hardware testing found that Touch_GSLx680::_init()'s firmware-
// upload loop HANGS SOLID (no crash, no timeout, no "[boot] ready" --
// totally silent) on the very first multi-byte register write after the
// initial page-select write, on this specific chip/board. A diagnostic
// confirmed the chip and I2C bus are NOT the problem: the exact same two
// writes (0xE0=0x88 then 0x80=0x01), issued via plain Arduino Wire
// instead, do NOT hang -- Wire's endTransmission() returns a clean error
// code (4, i2c_master_transmit ESP_ERR_INVALID_STATE) instantly, and the
// firmware upload proceeds from there. So this is a bug/limitation in
// LGFX's own i2c wrapper for this specific error condition (hangs where
// it should return an error), not anything wrong with the chip, wiring,
// or reset sequence. Rather than patch LovyanGFX itself (which would need
// a permanent library-patch script, like apply_lovyangfx_rgb_patch.py in
// the esp32lcd repo, just to work around one driver's error handling),
// this reimplements the touch driver using Wire directly, which is
// already proven not to hang on this exact chip/board.
//
// Firmware upload sequence (register order, retry logic, error handling)
// mirrors LovyanGFX's own Touch_GSLx680::_init()/getTouchRaw() structure
// closely -- same six pre-init register writes, same firmware-blob
// upload loop shape (page-select entries send 2 bytes, others send 5),
// same touch-data-register read/decode logic -- just issued via Wire
// instead of lgfx::i2c::. Firmware blob is this board's own
// JC8012_GSL3680_FW.hpp (see that file's own header for provenance).
//
// The RST+INT reset dance in begin() is Guition's own
// esp_lcd_touch_new_i2c_gsl3680() sequence (esp_lcd_gsl3680.c, NOT the
// simpler gsl3680_touch.cpp wrapper) -- kept here even though the Wire-vs-
// hang finding suggests it may not be strictly required, since it's real
// vendor behavior and costs only ~70ms at boot.
//
// CONFIRMED WORKING on real hardware, via a RIGOROUS one-at-a-time
// four-corner test (user confirmed which physical corner out loud
// *before* each tap, rather than tapping all four/five points together
// and inferring which numeric result belonged to which corner afterward
// -- that looser methodology is what let a real, sizeable bug ship as
// "confirmed" the first time around, see below). Getting to a genuine
// fix took two separate real-hardware-only bugs beyond the initial hang:
//
// 1. getTouchRaw()'s original version read the touch-data register in
//    TWO separate Wire.requestFrom() calls (a 4-byte header read, then a
//    second read sized by that header's own point count). That's not
//    the same protocol the chip actually needs -- each requestFrom()
//    issues its own fresh START condition, which resets the GSL3680's
//    internal read pointer back to the start of its report register
//    instead of continuing on into the real point data. Symptom: one
//    axis produced real, varying-with-touch-position values while the
//    other stayed stuck near zero regardless of where the panel was
//    actually tapped. Fixed by reading the full worst-case size (4-byte
//    header + up to 10 points * 4 bytes = 44 bytes) as a single
//    continuous transaction, ignoring whatever trailing bytes go unused
//    for a smaller reported point count.
//
// 2. Even after that fix, a LOOSELY-verified "tap all 4 corners plus
//    center" test looked plausible (five distinct, well-spread values)
//    and was wrongly marked confirmed -- but real menu navigation kept
//    landing on the wrong icon. A proper one-at-a-time retest (each
//    corner named out loud before tapping, not inferred from the number
//    afterward) revealed a clean, full Y-axis inversion: raw x (which
//    is this chip's native "short" axis -- it corresponds to this
//    panel's portrait-native 800px memory width, so it maps to the
//    physical top/bottom axis once BOOT_ROTATION=1 presents it as
//    landscape) read LOW (~20) for BOTH top corners and HIGH (~799) for
//    BOTH bottom corners -- backwards from what LGFX's own rotation=1
//    vflip step (Panel_Device::convertRawXY) expects, producing a clean
//    top/bottom swap downstream (physical top landed near screen
//    y=799, physical bottom near y=0). Raw y needed no correction --
//    confirmed low on both left corners, high on both right, already
//    matching what the shared rotation pipeline's swap step expects.
//    LGFX's offset_rotation field can't express "swap axes but skip the
//    vflip" for rotation=1 (checked: no 0-3 value produces that
//    combination), so the fix inverts raw x at the source instead (see
//    the `800 - (...)` in getTouchRaw() below), upstream of the shared
//    rotation/calibration pipeline the panel's own (already-correct)
//    display rendering also relies on. offset_rotation stays at its
//    original starting guess of 0 -- confirmed correct, not needing any
//    further adjustment once the source-level x inversion was in place.
//
// 3. Even after the Y-axis inversion fix, real menu taps still landed on
//    the wrong icon, in a pattern that grew worse toward one edge of the
//    panel. First diagnosis (wrong): genuine sensor non-linearity,
//    "fixed" with a symmetric parabola correction. That parabola matched
//    the SHAPE of the error but not its cause, and broke down entirely
//    for raw values already past 1280 (it pushed them even further out
//    instead of pulling them back), which firmware's own
//    clampTouchCoord() was silently capping before the real number was
//    ever visible in pixelweb's touch log -- so the worst-affected point
//    (the top-right grid icon) still failed after the "fix." Real
//    diagnosis, found via a proper one-at-a-time calibration pass across
//    corners, screen center, and every top-row grid icon, PLUS a
//    temporary raw-value debug print (`Serial.printf` of the pre-clamp
//    number, straight from data[2]/data[3] before any transform -- see
//    this file's git history if reviving it) to see what the two
//    saturating points actually were: this chip's real native range for
//    this axis is roughly 0-1636, not 0-1280 as originally assumed
//    (cfg.y_max=1280 had been copied from the panel's native pixel
//    height without separately verifying the touch chip's own
//    calibration range actually matches it). A single linear scale
//    factor (raw * 0.77, least-squares fit across 6 real-hardware
//    points) fixed every tested point, residuals 0-34px -- see the
//    `kRawToScreenScale` constant in getTouchRaw() below.
//
// Lessons for next time a touch axis looks "confirmed" but isn't:
//   - Confirm each test point's identity BEFORE the tap, not after --
//     inferring identity from proximity to an expected value can't
//     distinguish a correct mapping from one with a clean axis inversion
//     or mirror, since both still produce well-spread, individually
//     plausible-looking values (this is what let the Y-axis inversion
//     ship as "confirmed" the first time).
//   - When a symptom looks like it "grows worse toward one edge," check
//     whether values are getting silently clamped before concluding the
//     sensor itself is non-linear -- read the RAW value (before any
//     wire-protocol clamp) for any point suspected of being out of
//     range, not the already-clamped value the wire protocol delivers.
//     A parabola can look like a good fit to a handful of sub-clamp
//     points while completely misdiagnosing the actual cause.
//   - Don't assume a touch chip's calibrated native range matches the
//     panel's pixel dimensions just because that's the obvious guess --
//     verify it against real hardware specifically, the same as every
//     other "obvious" assumption on this board turned out to need
//     checking (see Panel_JD9365.hpp's Old-vs-New-Panel story for
//     another example of the same lesson on the display side).
#pragma once

#include <Wire.h>
#include <lgfx/v1/Touch.hpp>
#include "JC8012_GSL3680_FW.hpp"

class Touch_GSL3680_Wire : public lgfx::ITouch {
public:
    void setPins(int8_t sda, int8_t scl, int8_t rst, int8_t irq) {
        _sda = sda;
        _scl = scl;
        _rst = rst;
        _irq = irq;
    }

    bool init(void) override {
        _inited = false;

        // Guition's own esp_lcd_touch_new_i2c_gsl3680() RST+INT dance --
        // see this file's own header for why it's kept despite the
        // Wire-doesn't-hang finding.
        pinMode(_rst, OUTPUT);
        pinMode(_irq, OUTPUT);
        digitalWrite(_rst, LOW); // assert reset (active-low)
        digitalWrite(_irq, LOW);
        delay(10);
        digitalWrite(_irq, LOW);
        delay(1);
        digitalWrite(_rst, HIGH); // release reset
        delay(10);
        delay(50);

        Wire.begin(_sda, _scl);
        Wire.setClock(400000);

        static constexpr uint8_t addr = 0x40; // ESP_LCD_TOUCH_IO_I2C_GSL3680_ADDRESS

        auto writeReg = [&](uint8_t reg, uint8_t val) -> bool {
            Wire.beginTransmission(addr);
            Wire.write(reg);
            Wire.write(val);
            return Wire.endTransmission() == 0;
        };
        // Matches Touch_GSLx680::_init()'s own pre-firmware-upload steps
        // exactly, just via Wire.
        writeReg(0xE0, 0x88); delay(20);
        writeReg(0x80, 0x01); delay(5);
        writeReg(0xE4, 0x04); delay(5);
        writeReg(0xE0, 0x00); delay(20);
        writeReg(0xE0, 0x88); delay(20);
        writeReg(0xE4, 0x04); delay(10);
        Wire.beginTransmission(addr);
        Wire.write(0xBC);
        Wire.write((uint8_t)0);
        Wire.write((uint8_t)0);
        Wire.write((uint8_t)0);
        Wire.write((uint8_t)0);
        Wire.endTransmission();
        delay(10);

        // Firmware upload -- same {offset,val} pair format and same
        // "page-select entries (offset 0xF0) send 2 bytes, others send 5"
        // rule as LGFX's own Touch_GSLx680::_init().
        constexpr size_t length = sizeof(jc8012p4a1c_gsl3680_fw) / sizeof(gsl_fw_data);
        for (size_t i = 0; i < length; ++i) {
            const auto &e = jc8012p4a1c_gsl3680_fw[i];
            uint8_t bytes = (e.offset == 0xF0) ? 2 : 5;
            int retry = 3;
            bool ok = false;
            while (retry-- > 0 && !ok) {
                Wire.beginTransmission(addr);
                Wire.write(e.offset);
                Wire.write((uint8_t)(e.val & 0xFF));
                if (bytes >= 5) {
                    Wire.write((uint8_t)((e.val >> 8) & 0xFF));
                    Wire.write((uint8_t)((e.val >> 16) & 0xFF));
                    Wire.write((uint8_t)((e.val >> 24) & 0xFF));
                }
                ok = (Wire.endTransmission() == 0);
            }
            if (!ok) {
                return false;
            }
        }

        writeReg(0xE0, 0x88); delay(20);
        writeReg(0xE4, 0x04); delay(10);
        writeReg(0xE0, 0x00); delay(20);

        pinMode(_irq, INPUT_PULLUP); // hand INT back to interrupt duty
        _inited = true;
        return true;
    }

    void wakeup(void) override {}
    void sleep(void) override {}

    uint_fast8_t getTouchRaw(lgfx::touch_point_t *tp, uint_fast8_t count) override {
        if (!_inited || count == 0) return 0;

        static constexpr uint8_t addr = 0x40;
        static constexpr uint8_t DATA_REG = 0x80;
        // Fixed max-size read (4 header bytes + up to 10 points * 4 bytes)
        // done as ONE continuous I2C transaction -- the original version
        // here split this into two separate requestFrom() calls (one for
        // the 4-byte header, a second sized by the header's own point
        // count), which is NOT the same protocol as the chip actually
        // needs: each requestFrom() issues its own fresh START, which
        // resets the GSL3680's internal read pointer back to the start of
        // its report register instead of continuing on to the point data.
        // Confirmed on real hardware: that version produced one axis with
        // real, varying-with-touch-position values and the other stuck
        // near zero regardless of where the panel was actually tapped --
        // consistent with the "second read" silently re-fetching the same
        // 4 header bytes instead of the real point bytes. A single
        // requestFrom() for the full worst-case size avoids ever needing a
        // second transaction; trailing bytes beyond the real point count
        // are simply unused.
        static constexpr int kMaxBytes = 4 + 10 * 4;
        uint8_t buf[kMaxBytes] = {0};

        Wire.beginTransmission(addr);
        Wire.write(DATA_REG);
        if (Wire.endTransmission(false) != 0) return 0; // repeated start, no stop
        if (Wire.requestFrom((int)addr, kMaxBytes) != kMaxBytes) return 0;
        for (int i = 0; i < kMaxBytes; ++i) buf[i] = Wire.read();
        if (buf[0] == 0 || buf[0] > 10) return 0;

        uint32_t points = std::min<uint_fast8_t>(count, buf[0]);
        if (points > 10) points = 10;
        for (size_t idx = 0; idx < points; ++idx) {
            auto data = &buf[4 + idx * 4];
            tp[idx].id = data[3] >> 4;
            // Raw x is inverted here (800 - raw) -- confirmed on real
            // hardware via a rigorous one-at-a-time four-corner test
            // (user confirmed which physical corner before each tap,
            // rather than inferring it from proximity after the fact,
            // which is what an earlier "corners looked fine" pass had
            // relied on and which turned out to hide this exact bug).
            // Result: raw x tracks this board's physical top/bottom axis
            // (it's the chip's native "short" axis, matching this panel's
            // portrait-native 800px memory width) and was low (~20) for
            // BOTH top corners, high (~799) for BOTH bottom corners --
            // the opposite of what LGFX's own rotation=1 vflip step
            // (Panel_Device::convertRawXY) expects, so the two together
            // produced a clean full top/bottom inversion downstream (a
            // physical top tap landed near screen y=799, bottom near
            // y=0). Raw y needed no correction -- it already tracks
            // left/right correctly (confirmed low on both left corners,
            // high on both right corners, matching what convertRawXY's
            // swap step expects with no further change). LGFX's
            // offset_rotation can't express "swap axes but skip the
            // vflip" for this rotation (checked: no 0-3 value produces
            // that combination), so this is corrected at the raw-read
            // source instead, upstream of the shared rotation/calibration
            // pipeline the panel's own (already-correct) display
            // rendering also relies on.
            tp[idx].x = 800 - (int)(data[0] + ((data[1] & 0x0F) << 8));
            {
                // Raw y here becomes screen X after rotation=1's swap (see
                // the x-inversion comment above for the full pipeline).
                // ROOT CAUSE, found via a proper multi-point real-hardware
                // calibration pass (corners, screen center, and all four
                // top-row grid icons -- Description/ControllerInfo/
                // HighScores/Logo -- each confirmed one-at-a-time, plus a
                // temporary raw-value debug print to see PRE-CLAMP numbers
                // for the two points that were saturating): this chip's
                // real native range for this axis is roughly 0-1636, NOT
                // 0-1280 as originally assumed (matching cfg.y_max=1280,
                // copied from this panel's native pixel height without
                // separately verifying the TOUCH chip's own native
                // calibration range matches it). Two earlier "fixes" (a
                // symmetric parabola, and before that nothing) both missed
                // this because most real-hardware taps sit well under 1280
                // and looked locally plausible; only a tap very close to
                // the true right edge (raw ~1636) or several other
                // right-of-center points revealed the true scale. A
                // symmetric parabola diagnosed the SHAPE of the resulting
                // error correctly (worse toward the right) but the wrong
                // CAUSE (assumed non-linear sensor response, when it's
                // just a linear range mismatch) -- and broke down entirely
                // for raw values already past 1280, since firmware's own
                // clampTouchCoord() silently ate the real number there
                // before it was ever visible in pixelweb's touch log.
                // Simple least-squares fit (true_screen_x = raw * scale)
                // across those 6 points: scale=0.77, residuals 0-34px
                // (previous parabola residuals on the same points: 0 at
                // both edges by construction, but 150-230px in between,
                // and it actively made the near-1636-raw points worse).
                // If ever refit again: gather RAW values via a debug print
                // like the one this replaced (data[2]+((data[3]&0x0F)<<8)
                // straight to Serial, before any transform) for any point
                // that might exceed screen range -- reading the already-
                // clamped wire-protocol value instead silently discards
                // exactly the data needed to diagnose this class of bug.
                constexpr float kRawToScreenScale = 0.77f;
                float raw = (float)(data[2] + ((data[3] & 0x0F) << 8));
                float corrected = raw * kRawToScreenScale;
                if (corrected < 0) corrected = 0;
                if (corrected > 1280) corrected = 1280;
                tp[idx].y = (int16_t)corrected;
            }
            tp[idx].size = 1;
        }
        return points;
    }

private:
    int8_t _sda = -1, _scl = -1, _rst = -1, _irq = -1;
    bool _inited = false;
};
