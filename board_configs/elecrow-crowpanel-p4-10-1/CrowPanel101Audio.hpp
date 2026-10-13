// Speaker driver for the Elecrow CrowPanel Advanced 10.1" P4. Adapted from
// ../elecrow-crowpanel-ai-p4/CrowPanelAIAudio.hpp: same I2S pins (BCLK 22,
// LRCLK 21, DOUT 23 -- Elecrow V1.2 Lesson12 board_config.h) and the same
// play(soundId) table, so main.cpp's SND1 dispatch works the same; the
// difference is the amplifier enable, a plain GPIO (30, active LOW) here
// instead of the AI board's STC8 co-processor.
// Elecrow CrowPanel AI 5.0" speaker output: NO external codec chip
// (unlike Guition 10.1"'s ES8311 or M5Stack Tab5's ES8388) -- ESP32-P4's
// native I2S peripheral drives two onboard NS4168 I2S-input Class-D amps
// directly (one per channel, per the board's "dual-channel speakers"
// spec). Pins are straight from the vendor's own reference
// (example/V1.0/Arduino_Code_5inch/
// Lesson12-Playing_Loca_Music_from_SD_Card.ino): BCLK=GPIO22,
// LRCLK(WS)=GPIO21, SDOUT=GPIO23, no MCLK pin used.
//
// STATUS: implemented and verified correct at every layer this project
// has visibility into, but NOT confirmed to produce audible sound on the
// one bench unit tested (zero output, not even faint vibration felt on
// the speaker cone, across multiple real-hardware test rounds). This is
// NOT believed to be a firmware bug -- see the verification trail below
// -- most likely a hardware issue specific to that unit (an unpopulated
// component, given how early/new this product is, or a wiring/connector
// fault) rather than something fixable in software. If a future unit
// shows the same symptom, that would point back at a real design issue
// instead. Left wired up (HasSpeaker=true in pixelweb's catalog.go)
// since the mechanism itself is correct and should work on functioning
// hardware.
//
// Verification trail (each ruled out in turn on real hardware):
//   - SND1 dispatch reaches this file's play() -- confirmed via direct
//     serial monitoring (main.cpp's WAIT_SOUND handling is correct).
//   - STC8's AUDIO_SD register write succeeds (I2C ACK confirmed) --
//     see Backlight_STC8HAI.hpp for the active-low polarity bug found
//     and fixed here (a real, necessary fix, though not by itself
//     sufficient -- see below). NOTE: tracing the real schematic all the
//     way to the NS4168/LDO chip pins afterward found the entire analog
//     audio power path (LDO enable, amp CTRL/channel-select) is actually
//     HARDWIRED always-on with no GPIO in the path at all on this board
//     revision -- so this register may not even gate anything on real
//     hardware; kept anyway since it's a harmless, correct-per-datasheet
//     write either way.
//   - i2s_spk.begin() reports success, and every i2s_spk.write() call
//     reports writing its full expected byte count, every time -- no
//     underrun, no error, confirmed via direct serial monitoring.
//   - Switched from the legacy driver/i2s.h API (used successfully by
//     Tab5Audio.hpp/JC8012Audio.hpp, but those drive EXTERNAL CODEC
//     CHIPS that tolerate minor ESP32-side I2S protocol deviations) to
//     ESP_I2S.h's I2SClass (driver/i2s_std.h) -- the exact same API the
//     vendor's own confirmed-working reference firmware uses to drive
//     this exact bare-amp hardware. Needed an explicit `ESP_I2S` entry
//     in platformio.ini's lib_deps (a real Arduino-ESP32 core library,
//     unlike the raw driver/i2s.h header the other two files use, which
//     needs no such declaration).
//
// Same public API/sound catalog as Tab5Audio.hpp/JC8012Audio.hpp
// (playTone/playSweep/playExplosion, the same named chime functions, and
// play(soundId) with the identical soundId table) so main.cpp's SND1
// handling is a straight swap and every speaker-equipped Sidekick board
// sounds the same on a given card push -- see Tab5Audio.hpp's own header
// for the full soundId list and why SND1 is a documented no-op on boards
// without speaker hardware.
#pragma once

#include <ESP_I2S.h>
#include <math.h>

namespace crowpanel101audio {

// Speaker amplifier power: GPIO30, LOW = on (Elecrow V1.2 Lesson12 board_config.h:
// AUDIO_GPIO_CTRL 30, AUDIO_POWER_ENABLE LOW).
constexpr int8_t pin_amp_enable = 30;

constexpr int8_t pin_bck = 22;
constexpr int8_t pin_ws = 21;
constexpr int8_t pin_dout = 23;
constexpr uint32_t sample_rate = 44100;

inline I2SClass i2s_spk;
inline bool ready = false;

inline void init() {
    if (ready) return;

    // Amp enable -- MUST happen before any I2S data flows, per the
    // vendor firmware's own sequencing. Assumes stc8ai::begin() already
    // ran in setup() (see main.cpp) -- a harmless no-op write even if the
    // co-processor was never detected. See this file's own header for
    // why this may not actually gate anything on real hardware.
    pinMode(pin_amp_enable, OUTPUT);
    digitalWrite(pin_amp_enable, LOW); // amplifier on

    i2s_spk.setPins(pin_bck, pin_ws, pin_dout);
    if (!i2s_spk.begin(I2S_MODE_STD, sample_rate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
        return; // leave ready=false so a later play() call retries init()
    }
    ready = true;
}

// Everything below is intentionally IDENTICAL to Tab5Audio.hpp/
// JC8012Audio.hpp -- same synthesis math, same soundId numbering -- so a
// card push sounds the same across every speaker-equipped board. See
// Tab5Audio.hpp for the reasoning behind each effect's specific
// parameters.

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
        i2s_spk.write((const uint8_t *)buf, chunk * 2 * sizeof(int16_t));
        written += chunk;
    }
}

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
        i2s_spk.write((const uint8_t *)buf, chunk * 2 * sizeof(int16_t));
        written += chunk;
    }
}

inline void playExplosion(uint32_t durationMs, float volume = 0.35f, float lpAlpha = 0.15f,
                           float rumbleStartHz = 0.0f, float rumbleEndHz = 0.0f) {
    init();
    const int totalSamples = (int)(sample_rate * durationMs / 1000);
    int16_t buf[128 * 2];
    uint32_t rngState = 0x2A17C0DEu;
    float lp = 0.0f;
    float phase = 0.0f;
    int written = 0;
    while (written < totalSamples) {
        int chunk = min((int)(sizeof(buf) / sizeof(buf[0]) / 2), totalSamples - written);
        for (int i = 0; i < chunk; i++) {
            int n = written + i;
            rngState ^= rngState << 13;
            rngState ^= rngState >> 17;
            rngState ^= rngState << 5;
            float noise = ((float)(rngState & 0xFFFF) / 32768.0f) - 1.0f;
            lp += lpAlpha * (noise - lp);
            float env = expf(-4.0f * (float)n / totalSamples);
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
        i2s_spk.write((const uint8_t *)buf, chunk * 2 * sizeof(int16_t));
        written += chunk;
    }
}

inline void playHighScoreChime() {
    playTone(1046.5f, 90);
    playTone(1318.5f, 90);
    playTone(1568.0f, 90);
    playTone(2093.0f, 90);
    playTone(2637.0f, 90);
    playTone(3136.0f, 120);
    playTone(2093.0f, 100);
    playTone(3136.0f, 320);
}

inline void playAchievementChime() {
    playTone(659.3f, 80);
    playTone(783.99f, 80);
    playTone(987.77f, 80);
    playTone(1318.5f, 80);
    playTone(1568.0f, 80);
    playTone(2093.0f, 60);
    playTone(2637.0f, 60);
    playTone(2093.0f, 60);
    playTone(2637.0f, 260);
}

inline void playBigExplosion() {
    playExplosion(350, 0.7f, 0.15f, 150.0f, 40.0f);
}

inline void playBigExplosion2() {
    playExplosion(80, 0.35f, 0.4f, 0.0f, 0.0f);
    playExplosion(550, 0.4f, 0.12f, 110.0f, 25.0f);
}

inline void playBonusShip() {
    playTone(1046.5f, 60);
    playTone(1318.5f, 60);
    playTone(1568.0f, 60);
    playTone(2093.0f, 120);
}

inline void playNextLevel() {
    playTone(523.25f, 80);
    playTone(659.3f, 80);
    playTone(783.99f, 80);
    playTone(1046.5f, 80);
    playTone(1318.5f, 220);
}

inline void playTractorBeam() {
    playSweep(250.0f, 650.0f, 900, 0.45f, 9.0f, 40.0f);
}

inline void playExtraLife() {
    playTone(1318.5f, 50);
    playTone(1568.0f, 50);
    playTone(2093.0f, 50);
    playTone(2637.0f, 140);
}

inline void playGameOver() {
    playTone(784.0f, 200);
    playTone(659.3f, 200);
    playTone(523.25f, 200);
    playTone(392.0f, 400);
}

inline void playCoinInsert() {
    playTone(987.77f, 90);
    playTone(1318.51f, 380, 0.3f);
}

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

// soundId table matches Tab5Audio.hpp/JC8012Audio.hpp exactly -- see
// main.cpp's SND1 doc comment for the authoritative list.
inline void play(uint8_t soundId) {
    switch (soundId) {
        case 1: playHighScoreChime(); break;
        case 2: playAchievementChime(); break;
        case 3: playBigExplosion(); break;
        case 4: playBigExplosion2(); break;
        case 5: playBonusShip(); break;
        case 6: playNextLevel(); break;
        case 7: playTractorBeam(); break;
        case 8: playExtraLife(); break;
        case 9: playGameOver(); break;
        case 10: playCoinInsert(); break;
        case 11: playPlayer1Button(); break;
        case 12: playPlayer2Button(); break;
        default: break;
    }
}

} // namespace crowpanel101audio
