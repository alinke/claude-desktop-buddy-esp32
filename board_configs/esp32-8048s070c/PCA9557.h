// Vendored from Elecrow's own CrowPanel 7.0 HMI V3.0 reference example
// (github.com/Elecrow-RD/CrowPanel-7.0-HMI-ESP32-Display-800x480,
// example/V3.0/Arduino/libraries/PCA9557/src/PCA9557.h), which is itself
// "PCA9557 Driver (8-Channel GPIO I2C Expander)" by Patryk Wagner, based on
// Nadav Matalon's PCA9536 driver -- MIT licensed, see LICENSE comment
// below. Vendored (not pulled via lib_deps) so the exact API this board's
// touch/panel-reset sequence was written against doesn't drift -- several
// unrelated "PCA9557" Arduino libraries exist with different APIs.
//
// This board (V3.0 hardware revision) routes both the RGB panel's reset
// line and the GT911 touch controller's reset line through this I2C GPIO
// expander instead of direct ESP32 GPIO pins -- without running reset()
// through it once at boot (see resetPanelAndTouch() in main.cpp), the
// touch controller stays held in reset indefinitely and never reports a
// single touch event, confirmed on real hardware.
/*==============================================================================================================*

    @file     PCA9557.h
    @author   Patryk Wagner
    @license  MIT (c) 2016 Nadav Matalon
    PCA9557 Driver (8-Channel GPIO I2C Expander) based on Madav Matalon PCA9536 driver
 *===============================================================================================================*
    I2C ADDRESS: 0x18 (fixed, PCA9557D)
 *===============================================================================================================*
    LICENSE

    The MIT License (MIT)
    Copyright (c) 2016 Nadav Matalon
    Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
    documentation files (the "Software"), to deal in the Software without restriction, including without
    limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
    the Software, and to permit persons to whom the Software is furnished to do so, subject to the following
    conditions:
    The above copyright notice and this permission notice shall be included in all copies or substantial
    portions of the Software.
    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
    LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
    IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
    SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*==============================================================================================================*/

#pragma once

#include <Arduino.h>
#include <Wire.h>

const byte PCA9557_DEV_ADDR   = 0x18;
const byte PCA9557_NUM_BYTES  = 0x01;
const byte PCA9557_ALL_INPUT  = 0xFF;
const byte PCA9557_ALL_OUTPUT = 0x00;
const byte PCA9557_ALL_LOW    = 0x00;
const byte PCA9557_ALL_NON_INVERTED = 0x00;
const byte PCA9557_ALL_HIGH   = 0xFF;
const byte PCA9557_ALL_INVERTED = 0xFF;
const byte PCA9557_COM_SUCCESS = 0x00;

typedef enum : byte {
    PCA9557_REG_INPUT    = 0,
    PCA9557_REG_OUTPUT   = 1,
    PCA9557_REG_POLARITY = 2,
    PCA9557_REG_CONFIG   = 3
} pca9557_reg_ptr_t;

typedef enum : byte { PCA9557_IO0 = 0, PCA9557_IO1 = 1, PCA9557_IO2 = 2, PCA9557_IO3 = 3,
                       PCA9557_IO4 = 4, PCA9557_IO5 = 5, PCA9557_IO6 = 6, PCA9557_IO7 = 7 } pca9557_pin_t;
typedef enum : byte { PCA9557_IO_OUTPUT = 0, PCA9557_IO_INPUT = 1 } pca9557_mode_t;
typedef enum : byte { PCA9557_IO_LOW = 0, PCA9557_IO_HIGH = 1 } pca9557_state_t;
typedef enum : byte { PCA9557_IO_NON_INVERTED = 0, PCA9557_IO_INVERTED = 1 } pca9557_polarity_t;

// Header-only (methods defined inline right here) rather than a separate
// .cpp -- PlatformIO only auto-compiles .cpp files under src/ or lib/*/,
// not arbitrary -I include paths like board_configs/esp32-8048s070c/, and
// this is only needed by one board/env, so a proper lib/ entry (which
// would get pulled into every environment's build) isn't worth it for ~90
// lines used by a single translation unit (main.cpp).
class PCA9557 {
public:
    PCA9557() {}
    ~PCA9557() {}

    byte ping() {
        Wire.beginTransmission(PCA9557_DEV_ADDR);
        return Wire.endTransmission();
    }

    byte getMode(pca9557_pin_t pin) { return getPin(pin, PCA9557_REG_CONFIG); }
    byte getPolarity(pca9557_pin_t pin) { return getPin(pin, PCA9557_REG_POLARITY); }

    void setMode(pca9557_pin_t pin, pca9557_mode_t newMode) { setPin(pin, PCA9557_REG_CONFIG, newMode); }
    void setMode(pca9557_mode_t newMode) { setReg(PCA9557_REG_CONFIG, newMode ? PCA9557_ALL_INPUT : PCA9557_ALL_OUTPUT); }

    void setState(pca9557_pin_t pin, pca9557_state_t newState) { setPin(pin, PCA9557_REG_OUTPUT, newState); }
    void setState(pca9557_state_t newState) { setReg(PCA9557_REG_OUTPUT, newState ? PCA9557_ALL_HIGH : PCA9557_ALL_LOW); }

    void toggleState(pca9557_pin_t pin) { setReg(PCA9557_REG_OUTPUT, getReg(PCA9557_REG_OUTPUT) ^ (1 << pin)); }
    void toggleState() { setReg(PCA9557_REG_OUTPUT, ~getReg(PCA9557_REG_OUTPUT)); }

    void setPolarity(pca9557_pin_t pin, pca9557_polarity_t newPolarity) { setPin(pin, PCA9557_REG_POLARITY, newPolarity); }
    void setPolarity(pca9557_polarity_t newPolarity) {
        byte polarityVals = getReg(PCA9557_REG_POLARITY);
        byte polarityMask = getReg(PCA9557_REG_CONFIG);
        byte polarityNew  = newPolarity ? PCA9557_ALL_INVERTED : PCA9557_ALL_NON_INVERTED;
        setReg(PCA9557_REG_POLARITY, (polarityVals & ~polarityMask) | (polarityNew & polarityMask));
    }

    void reset() {
        setMode(PCA9557_IO_INPUT);
        setState(PCA9557_IO_HIGH);
        setPolarity(PCA9557_IO_NON_INVERTED);
        initCall(PCA9557_REG_CONFIG);
        endCall();
    }

    byte getComResult() { return _comBuffer; }

private:
    byte _comBuffer = 0;

    byte getReg(pca9557_reg_ptr_t regPtr) {
        byte regData = 0;
        initCall(regPtr);
        endCall();
        if (_comBuffer == PCA9557_COM_SUCCESS) {
            Wire.requestFrom(PCA9557_DEV_ADDR, PCA9557_NUM_BYTES);
            if (Wire.available() == PCA9557_NUM_BYTES) regData = Wire.read();
            else {
                while (Wire.available()) Wire.read();
                _comBuffer = ping();
            }
        }
        return regData;
    }

    byte getPin(pca9557_pin_t pin, pca9557_reg_ptr_t regPtr) { return bitRead(getReg(regPtr), pin); }

    void setReg(pca9557_reg_ptr_t regPtr, byte newSetting) {
        if (regPtr > 0) {
            initCall(regPtr);
            Wire.write(newSetting);
            endCall();
        }
    }

    void setPin(pca9557_pin_t pin, pca9557_reg_ptr_t regPtr, byte newSetting) {
        byte newReg = getReg(regPtr);
        bitWrite(newReg, pin, newSetting);
        setReg(regPtr, newReg);
    }

    void initCall(pca9557_reg_ptr_t regPtr) {
        Wire.beginTransmission(PCA9557_DEV_ADDR);
        Wire.write(regPtr);
    }

    void endCall() { _comBuffer = Wire.endTransmission(); }
};
