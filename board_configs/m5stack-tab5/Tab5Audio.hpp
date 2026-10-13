// M5Stack Tab5 speaker output: ES8388 codec (register control over the
// board's internal I2C bus, same bus LGFX_Config.hpp already brings up for
// the PI4IOE5V6408 panel/touch dance) + raw I2S for the actual audio data
// path. The only board in this catalog with real audio hardware -- see
// esp32lcd's main.cpp wire-protocol doc comment (SND1) for how a card push
// triggers this.
//
// The ES8388 register sequence and every pin assignment below (I2C bus,
// I2S MCLK/BCK/WS/DOUT, and the specific PI4IOE1 bit that gates the
// onboard NS4150B amp) are taken directly from M5Stack's own M5Unified
// library source (M5Unified.cpp, board_M5Tab5's speaker pin table and
// _speaker_enabled_cb_tab5's register list) rather than derived from a
// generic ES8388 datasheet/reference -- this project doesn't link
// M5Unified itself (conflicts with LovyanGFX, see platformio.ini's
// m5stack-tab5 env comment), but Tab5's specific wiring/init sequence
// isn't something to re-derive blind when a real, independently-verified
// working Tab5 project already depends on exactly this sequence
// (chipster6502/MiSTer_monitor, via M5Unified's own M5.Speaker).
//
// Reuses LGFX_Config.hpp's already-initialized I2C bus (tab5_i2c_port,
// GPIO31/32) via the same lgfx::i2c:: primitives that file already uses,
// rather than opening a second I2C driver instance on the same physical
// pins.
#pragma once

#include <driver/i2s.h>
#include <math.h>

namespace tab5audio {

constexpr uint8_t es8388_i2c_addr = 0x10;
// Must match LGFX_Config.hpp's pi4io1_i2c_addr -- same physical expander,
// already brought up by panel init before this ever runs.
constexpr uint8_t pi4io1_i2c_addr = 0x43;
constexpr gpio_num_t pin_mclk = GPIO_NUM_30;
constexpr gpio_num_t pin_bck = GPIO_NUM_27;
constexpr gpio_num_t pin_ws = GPIO_NUM_29;
constexpr gpio_num_t pin_dout = GPIO_NUM_26;
constexpr i2s_port_t i2s_port = I2S_NUM_0;
constexpr uint32_t sample_rate = 44100;
// Must match LGFX_Config.hpp's tab5_i2c_port -- kept as a separate literal
// here rather than referencing that file's constant, so this header has no
// compile-order dependency on it.
constexpr int_fast16_t i2c_port = I2C_NUM_1;

inline bool ready = false;

// Register sequence + values taken verbatim from M5Unified's
// _speaker_enabled_cb_tab5 (CHIPPOWER/DACPOWER/I2S-format/DAC-unmute etc.)
// -- see file header.
inline void init() {
    if (ready) return;

    static constexpr uint8_t regs[][2] = {
        {0, 0x80}, {0, 0x00}, {0, 0x00}, {0, 0x0E}, {1, 0x00},
        {2, 0x0A}, {3, 0xFF}, {4, 0x3C}, {5, 0x00}, {6, 0x00},
        {7, 0x7C}, {8, 0x00}, {23, 0x18}, {24, 0x00}, {25, 0x20},
        {26, 0x00}, {27, 0x00}, {28, 0x08}, {29, 0x00}, {38, 0x00},
        {39, 0xB8}, {42, 0xB8}, {43, 0x08}, {45, 0x00}, {46, 0x21},
        {47, 0x21}, {48, 0x21}, {49, 0x21},
    };
    for (auto &r : regs) {
        lgfx::i2c::writeRegister8(i2c_port, es8388_i2c_addr, r[0], r[1], (uint8_t)0, 400000);
    }

    // AMP on -- PI4IOE1 register 0x05, bit 1. mask=0b00000010 means only
    // that bit is touched, the rest of the register (LCD reset, GT911
    // reset, already set by LGFX_Config.hpp's own init) is preserved --
    // same writeRegister8(port, addr, reg, data, mask, freq) primitive
    // that file already uses for its own register-array writes.
    lgfx::i2c::writeRegister8(i2c_port, pi4io1_i2c_addr, (uint8_t)0x05, (uint8_t)0xFF, (uint8_t)0b00000010, (uint32_t)400000);

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = sample_rate,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 4,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,
    };
    i2s_driver_install(i2s_port, &i2s_config, 0, NULL);
    i2s_pin_config_t pin_config = {
        .mck_io_num = pin_mclk,
        .bck_io_num = pin_bck,
        .ws_io_num = pin_ws,
        .data_out_num = pin_dout,
        .data_in_num = I2S_PIN_NO_CHANGE,
    };
    i2s_set_pin(i2s_port, &pin_config);
    ready = true;
}

// Blocking for durationMs -- fine here, only ever called from loop() in
// direct response to a discrete SND1 command (same tradeoff as
// runTouchCalibration's blocking wait elsewhere in this project).
// ~10ms fade in/out avoids an audible click at the start/end of each note.
inline void playTone(float freqHz, uint32_t durationMs, float volume = 0.3f) {
    init();
    const int totalSamples = (int)(sample_rate * durationMs / 1000);
    const int rampSamples = min(totalSamples / 8, (int)(sample_rate * 0.01f));
    int16_t buf[128 * 2]; // stereo, interleaved
    int written = 0;
    while (written < totalSamples) {
        int chunk = min((int)(sizeof(buf) / sizeof(buf[0]) / 2), totalSamples - written);
        for (int i = 0; i < chunk; i++) {
            int n = written + i;
            float env = 1.0f;
            if (n < rampSamples) {
                env = (float)n / rampSamples;
            } else if (n > totalSamples - rampSamples) {
                env = (float)(totalSamples - n) / rampSamples;
            }
            int16_t sample = (int16_t)(volume * env * 32000.0f * sinf(2.0f * (float)M_PI * freqHz * n / sample_rate));
            buf[i * 2] = sample;
            buf[i * 2 + 1] = sample;
        }
        size_t bytesWritten = 0;
        i2s_write(i2s_port, buf, chunk * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
        written += chunk;
    }
}

// Continuous frequency sweep from startHz to endHz over durationMs, with an
// optional vibrato wobble (wobbleHz/wobbleDepth, both 0 to disable) layered
// on top of the sweep -- used for the tractor-beam warble and can give an
// explosion's rumble a bit more character. Tracks a running phase
// accumulator (integrating instantaneous frequency per sample) rather than
// naively evaluating sin(2*pi*f(t)*t), which would have audible phase
// discontinuities the moment frequency actually changes.
inline void playSweep(float startHz, float endHz, uint32_t durationMs, float volume = 0.3f,
                       float wobbleHz = 0.0f, float wobbleDepth = 0.0f) {
    init();
    const int totalSamples = (int)(sample_rate * durationMs / 1000);
    const int rampSamples = min(totalSamples / 8, (int)(sample_rate * 0.01f));
    int16_t buf[128 * 2];
    float phase = 0.0f;
    int written = 0;
    while (written < totalSamples) {
        int chunk = min((int)(sizeof(buf) / sizeof(buf[0]) / 2), totalSamples - written);
        for (int i = 0; i < chunk; i++) {
            int n = written + i;
            float t = (float)n / totalSamples;
            float freq = startHz + (endHz - startHz) * t;
            if (wobbleHz > 0.0f) {
                freq += wobbleDepth * sinf(2.0f * (float)M_PI * wobbleHz * n / sample_rate);
            }
            phase += 2.0f * (float)M_PI * freq / sample_rate;
            float env = 1.0f;
            if (n < rampSamples) {
                env = (float)n / rampSamples;
            } else if (n > totalSamples - rampSamples) {
                env = (float)(totalSamples - n) / rampSamples;
            }
            int16_t sample = (int16_t)(volume * env * 32000.0f * sinf(phase));
            buf[i * 2] = sample;
            buf[i * 2 + 1] = sample;
        }
        size_t bytesWritten = 0;
        i2s_write(i2s_port, buf, chunk * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
        written += chunk;
    }
}

// Lowpassed white noise with an exponential decay envelope, optionally
// mixed with a descending sine rumble underneath (rumbleStartHz/EndHz, both
// 0 to disable) -- real explosions are noise-dominated, not tonal, so
// playTone/playSweep alone sound weak for this; a plain xorshift PRNG is
// more than enough randomness for a sound effect. The one-pole lowpass
// (lpAlpha, smaller = darker/boomier) softens the raw noise's harsh
// high-frequency hiss into something closer to a real boom.
inline void playExplosion(uint32_t durationMs, float volume = 0.35f, float lpAlpha = 0.15f,
                           float rumbleStartHz = 0.0f, float rumbleEndHz = 0.0f) {
    init();
    const int totalSamples = (int)(sample_rate * durationMs / 1000);
    int16_t buf[128 * 2];
    uint32_t rngState = 0x2A17C0DEu; // fixed seed -- a sound effect doesn't need true randomness
    float lp = 0.0f;
    float phase = 0.0f;
    int written = 0;
    while (written < totalSamples) {
        int chunk = min((int)(sizeof(buf) / sizeof(buf[0]) / 2), totalSamples - written);
        for (int i = 0; i < chunk; i++) {
            int n = written + i;
            // xorshift32
            rngState ^= rngState << 13;
            rngState ^= rngState >> 17;
            rngState ^= rngState << 5;
            float noise = ((float)(rngState & 0xFFFF) / 32768.0f) - 1.0f;
            lp += lpAlpha * (noise - lp);
            float env = expf(-4.0f * (float)n / totalSamples); // fast attack, exponential decay
            float sample = lp;
            if (rumbleStartHz > 0.0f) {
                float t = (float)n / totalSamples;
                float freq = rumbleStartHz + (rumbleEndHz - rumbleStartHz) * t;
                phase += 2.0f * (float)M_PI * freq / sample_rate;
                sample = 0.6f * lp + 0.4f * sinf(phase);
            }
            int16_t s = (int16_t)(volume * env * 32000.0f * sample);
            buf[i * 2] = s;
            buf[i * 2 + 1] = s;
        }
        size_t bytesWritten = 0;
        i2s_write(i2s_port, buf, chunk * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
        written += chunk;
    }
}

inline void playHighScoreChime() {
    playTone(1046.5f, 90);  // C6
    playTone(1318.5f, 90);  // E6
    playTone(1568.0f, 90);  // G6
    playTone(2093.0f, 90);  // C7
    playTone(2637.0f, 90);  // E7
    playTone(3136.0f, 120); // G7
    playTone(2093.0f, 100); // C7 -- little bounce back down
    playTone(3136.0f, 320); // G7 -- big sustained celebratory finish
}

inline void playAchievementChime() {
    playTone(659.3f, 80);   // E5
    playTone(783.99f, 80);  // G5
    playTone(987.77f, 80);  // B5
    playTone(1318.5f, 80);  // E6
    playTone(1568.0f, 80);  // G6
    // Sparkly bell-cascade flourish -- distinct character from the high
    // score fanfare's straight ascending run.
    playTone(2093.0f, 60);  // C7
    playTone(2637.0f, 60);  // E7
    playTone(2093.0f, 60);  // C7
    playTone(2637.0f, 260); // E7 -- held finish
}

inline void playBigExplosion() {
    playExplosion(350, 0.7f, 0.15f, 150.0f, 40.0f);
}

// Deeper and longer than the first, with a sharper initial crack (a brief
// less-filtered burst) before settling into a lower, longer rumble --
// meant to read as "bigger," not just a repeat of the first.
inline void playBigExplosion2() {
    playExplosion(80, 0.35f, 0.4f, 0.0f, 0.0f);  // crack
    playExplosion(550, 0.4f, 0.12f, 110.0f, 25.0f); // boom
}

inline void playBonusShip() {
    playTone(1046.5f, 60); // C6
    playTone(1318.5f, 60); // E6
    playTone(1568.0f, 60); // G6
    playTone(2093.0f, 120); // C7
}

inline void playNextLevel() {
    playTone(523.25f, 80);  // C5
    playTone(659.3f, 80);   // E5
    playTone(783.99f, 80);  // G5
    playTone(1046.5f, 80);  // C6
    playTone(1318.5f, 220); // E6
}

// Real Galaga tractor beam is a rising, warbling "wooOOOoo" -- a sweep with
// vibrato layered on top captures that better than any fixed-pitch chime.
inline void playTractorBeam() {
    playSweep(250.0f, 650.0f, 900, 0.45f, 9.0f, 40.0f);
}

inline void playExtraLife() {
    playTone(1318.5f, 50); // E6
    playTone(1568.0f, 50); // G6
    playTone(2093.0f, 50); // C7
    playTone(2637.0f, 140); // E7
}

inline void playGameOver() {
    // Original G4-E4-C4-G3 voicing was inaudible on real hardware -- the
    // Tab5's small speaker has weak bass response and a pure low sine tone
    // (down to 196Hz) barely produces any audible output even though the
    // I2S/DMA path is genuinely running (confirmed via timing diagnostics).
    // Transposed up one octave, same descending shape.
    playTone(784.0f, 200);  // G5
    playTone(659.3f, 200);  // E5
    playTone(523.25f, 200); // C5
    playTone(392.0f, 400);  // G4
}

// Classic "Super Mario Bros" coin ding -- a quick B5 into a bright,
// sustained E6.
inline void playCoinInsert() {
    playTone(987.77f, 90);  // B5
    playTone(1318.51f, 380, 0.3f); // E6, held
}

// Button-press feedback -- deliberately a single short blip, not a melodic
// chime: these can fire rapidly on repeated presses, and play() blocks the
// device's serial loop for the duration, so staying brief keeps input
// feedback feeling responsive rather than laggy. P1/P2 use different
// pitches so the two are distinguishable by ear alone.
inline void playPlayer1Button() {
    playTone(700.0f, 55, 0.3f);
    playTone(1100.0f, 55, 0.32f);
    playTone(1500.0f, 70, 0.35f);
}

inline void playPlayer2Button() {
    playTone(1500.0f, 55, 0.3f);
    playTone(1100.0f, 55, 0.32f);
    playTone(700.0f, 70, 0.35f);
}

// soundId: 1=new high score, 2=achievement triggered, 3=big explosion,
// 4=big explosion #2, 5=bonus ship, 6=next level, 7=tractor beam,
// 8=extra life, 9=game over, 10=coin insert, 11=player 1 button,
// 12=player 2 button -- see main.cpp's SND1 handling. Unknown IDs are
// silently ignored (forward-compatible with a future firmware adding more
// without breaking on an older one).
inline void play(uint8_t soundId) {
    switch (soundId) {
        case 1:
            playHighScoreChime();
            break;
        case 2:
            playAchievementChime();
            break;
        case 3:
            playBigExplosion();
            break;
        case 4:
            playBigExplosion2();
            break;
        case 5:
            playBonusShip();
            break;
        case 6:
            playNextLevel();
            break;
        case 7:
            playTractorBeam();
            break;
        case 8:
            playExtraLife();
            break;
        case 9:
            playGameOver();
            break;
        case 10:
            playCoinInsert();
            break;
        case 11:
            playPlayer1Button();
            break;
        case 12:
            playPlayer2Button();
            break;
        default:
            break;
    }
}

} // namespace tab5audio
