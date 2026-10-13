// Waveshare ESP32-S3-Touch-LCD-3.49 V2 speaker output: ES8311 codec over
// I2C0 (SDA 47 / SCL 48 -- the same bus and Wire instance as the TCA9554
// expander, see LGFX_Config.hpp) + raw I2S for the audio data. A copy of
// board_configs/jc8012p4a1c/JC8012Audio.hpp (same ES8311 register
// sequence, same sound catalog and play(soundId) table, so a card push
// sounds the same as on the other speaker boards) with this board's wiring,
// from Waveshare's own codec table for it (Arduino/examples/08_Audio_Test/
// src/codec_board/board_cfg.h, "Board: S3_LCD_3_49"):
//   - ES8311 at 7-bit I2C address 0x18 (found by an I2C scan of this bus).
//   - I2S: MCLK 7, BCLK 15, WS 46, DOUT 45 (DIN 6 is the ES7210 mic, unused).
//   - No PA GPIO (pa: -1); the speaker amp is enabled by TCA9554 P7, which
//     Waveshare's audio demo drives high ("NS mode").
// The speaker plugs into the 2-pin MX1.25 header on the back.
#pragma once

#include <Wire.h>
#include "LGFX_Config.hpp" // waveshare349::expanderSet
#include <driver/i2s.h>
#include <math.h>

namespace waveshare349audio {

constexpr uint8_t es8311_i2c_addr = 0x18;
constexpr gpio_num_t pin_mclk = GPIO_NUM_7;
constexpr gpio_num_t pin_bck = GPIO_NUM_15;
constexpr gpio_num_t pin_ws = GPIO_NUM_46;
constexpr gpio_num_t pin_dout = GPIO_NUM_45;
constexpr i2s_port_t i2s_port = I2S_NUM_0;
constexpr uint32_t sample_rate = 44100;

// ES8311 register addresses -- from es8311_reg.h in the same reference
// component, names kept identical for easy cross-checking against that
// source.
constexpr uint8_t REG_RESET00 = 0x00;
constexpr uint8_t REG_CLK01 = 0x01;
constexpr uint8_t REG_CLK02 = 0x02;
constexpr uint8_t REG_CLK03 = 0x03;
constexpr uint8_t REG_CLK04 = 0x04;
constexpr uint8_t REG_CLK05 = 0x05;
constexpr uint8_t REG_CLK06 = 0x06;
constexpr uint8_t REG_CLK07 = 0x07;
constexpr uint8_t REG_CLK08 = 0x08;
constexpr uint8_t REG_SDPIN09 = 0x09;
constexpr uint8_t REG_SDPOUT0A = 0x0A;
constexpr uint8_t REG_SYS0B = 0x0B;
constexpr uint8_t REG_SYS0C = 0x0C;
constexpr uint8_t REG_SYS0D = 0x0D;
constexpr uint8_t REG_SYS0E = 0x0E;
constexpr uint8_t REG_SYS10 = 0x10;
constexpr uint8_t REG_SYS11 = 0x11;
constexpr uint8_t REG_SYS12 = 0x12;
constexpr uint8_t REG_SYS13 = 0x13;
constexpr uint8_t REG_SYS14 = 0x14;
constexpr uint8_t REG_ADC15 = 0x15;
constexpr uint8_t REG_ADC16 = 0x16;
constexpr uint8_t REG_ADC17 = 0x17;
constexpr uint8_t REG_ADC1B = 0x1B;
constexpr uint8_t REG_ADC1C = 0x1C;
constexpr uint8_t REG_DAC31 = 0x31;
constexpr uint8_t REG_DAC32 = 0x32;
constexpr uint8_t REG_DAC37 = 0x37;
constexpr uint8_t REG_GPIO44 = 0x44;
constexpr uint8_t REG_GP45 = 0x45;

inline bool ready = false;

inline bool writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(es8311_i2c_addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

// Returns the last-read value on failure too (0), matching the reference
// driver's own "*value = 0" default-on-entry behavior closely enough for
// this use (every call site here ORs/ANDs the result, so a 0 on a
// transient read failure just means "no prior bits preserved" rather than
// a hang or crash).
inline uint8_t readReg(uint8_t reg) {
    Wire.beginTransmission(es8311_i2c_addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return 0;
    if (Wire.requestFrom((int)es8311_i2c_addr, 1) != 1) return 0;
    return Wire.read();
}

inline void setMute(bool mute) {
    uint8_t regv = readReg(REG_DAC31) & 0x9F;
    writeReg(REG_DAC31, mute ? (regv | 0x60) : regv);
}

inline void paPower(bool enable) {
    waveshare349::expanderSet(waveshare349::kExioAmpEn, enable);
}

inline void init() {
    if (ready) return;

    // Wire is already up on SDA 47 / SCL 48 (waveshare349::expanderInit()).
    paPower(false); // ES_PA_SETUP | ES_PA_DISABLE, before any codec register write

    // --- es8311_open() ---
    if (readReg(REG_SYS0D) != 0xFA) writeReg(REG_SYS0D, 0xFA);
    // "Due to occasional failures during the first I2C write with the
    // ES8311 chip, a second write is performed" -- comment straight from
    // the reference driver, kept verbatim including the double write.
    writeReg(REG_GPIO44, 0x08);
    writeReg(REG_GPIO44, 0x08);
    writeReg(REG_CLK01, 0x30);
    writeReg(REG_CLK02, 0x00);
    writeReg(REG_CLK03, 0x10);
    writeReg(REG_ADC16, 0x24);
    writeReg(REG_CLK04, 0x10);
    writeReg(REG_CLK05, 0x00);
    writeReg(REG_SYS0B, 0x00);
    writeReg(REG_SYS0C, 0x00);
    writeReg(REG_SYS10, 0x1F);
    writeReg(REG_SYS11, 0x7F);
    writeReg(REG_RESET00, 0x80);

    // master_mode=false -> clear bit6
    writeReg(REG_RESET00, readReg(REG_RESET00) & 0xBF);

    // Select MCLK source: use_mclk=true -> clear bit7; invert_mclk=false ->
    // clear bit6. Starting mask 0x3F already has both bits clear, kept as
    // the reference's own explicit regv=0x3F/&=/|= dance for traceability
    // against that source rather than pre-folding the constant.
    {
        uint8_t regv = 0x3F;
        regv &= 0x7F; // use_mclk
        regv &= (uint8_t)~0x40; // !invert_mclk
        writeReg(REG_CLK01, regv);
    }
    // invert_sclk=false -> clear bit5, preserving whatever else is there
    writeReg(REG_CLK06, readReg(REG_CLK06) & (uint8_t)~0x20);

    writeReg(REG_SYS13, 0x10);
    writeReg(REG_ADC1B, 0x0A);
    writeReg(REG_ADC1C, 0x6A);
    // no_dac_ref=false -> internal reference signal (ADCL + DACR)
    writeReg(REG_GPIO44, 0x58);

    // --- es8311_set_fs(): set_bits_per_sample(16) ---
    {
        uint8_t dac_iface = readReg(REG_SDPIN09) | 0x0C;
        uint8_t adc_iface = readReg(REG_SDPOUT0A) | 0x0C;
        writeReg(REG_SDPIN09, dac_iface);
        writeReg(REG_SDPOUT0A, adc_iface);
    }
    // --- config_fmt(ES_I2S_NORMAL) ---
    {
        uint8_t dac_iface = readReg(REG_SDPIN09) & 0xFC;
        uint8_t adc_iface = readReg(REG_SDPOUT0A) & 0xFC;
        writeReg(REG_SDPIN09, dac_iface);
        writeReg(REG_SDPOUT0A, adc_iface);
    }
    // --- config_sample(44100), coeff row for mclk=11289600 (44100*256):
    //     pre_div=1 pre_multi=1 adc_div=1 dac_div=1 fs_mode=0 lrck_h=0x00
    //     lrck_l=0xFF bclk_div=4 adc_osr=0x10 dac_osr=0x10 ---
    {
        uint8_t regv = readReg(REG_CLK02) & 0x07;
        regv |= (1 - 1) << 5; // pre_div
        regv |= 0 << 3;        // pre_multi=1 -> datmp=0
        writeReg(REG_CLK02, regv);

        regv = ((1 - 1) << 4) | ((1 - 1) << 0); // adc_div, dac_div
        writeReg(REG_CLK05, regv);

        regv = readReg(REG_CLK03) & 0x80;
        regv |= (0 << 6) | 0x10; // fs_mode, adc_osr
        writeReg(REG_CLK03, regv);

        regv = readReg(REG_CLK04) & 0x80;
        regv |= 0x10; // dac_osr
        writeReg(REG_CLK04, regv);

        regv = readReg(REG_CLK07) & 0xC0;
        regv |= 0x00; // lrck_h
        writeReg(REG_CLK07, regv);

        writeReg(REG_CLK08, 0xFF); // lrck_l

        regv = readReg(REG_CLK06) & 0xE0;
        regv |= (4 - 1); // bclk_div (< 19, so bclk_div - 1)
        writeReg(REG_CLK06, regv);
    }

    // --- es8311_enable(true) -> es8311_start() ---
    writeReg(REG_RESET00, 0x80 & 0xBF); // master_mode=false
    {
        uint8_t regv = 0x3F;
        regv &= 0x7F; // use_mclk
        regv &= (uint8_t)~0x40; // !invert_mclk
        writeReg(REG_CLK01, regv);
    }
    {
        uint8_t dac_iface = readReg(REG_SDPIN09) & 0xBF;
        uint8_t adc_iface = readReg(REG_SDPOUT0A) & 0xBF;
        // codec_mode=BOTH -> clear bit6 on both (already clear via 0xBF
        // mask above; kept as two steps to mirror the source's ADC/DAC
        // mode-gated branches, which happen to both apply for BOTH mode).
        writeReg(REG_SDPIN09, dac_iface);
        writeReg(REG_SDPOUT0A, adc_iface);
    }
    writeReg(REG_ADC17, 0xBF);
    writeReg(REG_SYS0E, 0x02);
    writeReg(REG_SYS12, 0x00); // codec_mode BOTH/DAC
    writeReg(REG_SYS14, 0x1A);
    // digital_mic=false -> clear bit6
    writeReg(REG_SYS14, readReg(REG_SYS14) & (uint8_t)~0x40);
    writeReg(REG_SYS0D, 0x01);
    writeReg(REG_ADC15, 0x40);
    writeReg(REG_DAC37, 0x08);
    writeReg(REG_GP45, 0x00);

    paPower(true);
    setMute(false);
    // DAC volume, ~0.5dB per step: the Guition's 0xA7 (~-12dB) was too
    // quiet on this board's speaker, so 0xBF (~0dB, the codec's full-scale
    // level). The single per-board gain knob -- adjust this, not the
    // per-effect volumes below, which match the other boards.
    writeReg(REG_DAC32, 0xBF);

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

// Everything below (playTone/playSweep/playExplosion, every named chime,
// and play(soundId)'s dispatch table) is intentionally IDENTICAL to
// Tab5Audio.hpp / JC8012Audio.hpp -- same synthesis math, same soundId
// numbering. See Tab5Audio.hpp for the reasoning behind each effect.

inline void playTone(float freqHz, uint32_t durationMs, float volume = 0.3f) {
    init();
    const int totalSamples = (int)(sample_rate * durationMs / 1000);
    const int rampSamples = min(totalSamples / 8, (int)(sample_rate * 0.01f));
    int16_t buf[128 * 2];
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
        size_t bytesWritten = 0;
        i2s_write(i2s_port, buf, chunk * 2 * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
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

// soundId table matches Tab5Audio.hpp's exactly -- see main.cpp's SND1
// doc comment for the authoritative list.
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

} // namespace waveshare349audio
