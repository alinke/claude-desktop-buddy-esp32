// ============================================================
// board.cpp — board bring-up around lcd.init(). See board.h.
//
// The reset / backlight sequences here come from the Pixelcade Sidekick
// firmware (github.com/alinke/esp32lcd), where each was confirmed on real
// hardware; the comments note why each one is needed.
// ============================================================
#include "board.h"
#include <Arduino.h>

#if defined(ELECROW_CROWPANEL_7_0_HMI)
// CrowPanel 5.0/7.0 (V3.0 PCB): the RGB panel's reset and the GT911's reset
// sit behind a PCA9557 I2C expander. Touch never reports without this
// sequence, run once before lcd.init().
#include <Wire.h>
#include "PCA9557.h"
static PCA9557 pca9557;
static void resetPanelAndTouch() {
  Wire.begin(19, 20);
  pca9557.reset();
  pca9557.setMode(PCA9557_IO_OUTPUT);
  pca9557.setState(PCA9557_IO0, PCA9557_IO_LOW);
  pca9557.setState(PCA9557_IO1, PCA9557_IO_LOW);
  delay(20);
  pca9557.setState(PCA9557_IO0, PCA9557_IO_HIGH);   // release panel reset
  delay(100);
  pca9557.setMode(PCA9557_IO1, PCA9557_IO_INPUT);   // release touch reset
  delay(50);
  // LovyanGFX's GT911 driver installs its own I2C driver on these pins.
  Wire.end();
}
#endif

#if defined(PIXELCADE_BOARD_ELECROW_ROUND_2_1)
// CrowPanel 2.1" round: LCD power/reset and touch reset go through a
// PCF8574 (0x21, SDA 38 / SCL 39). P0 touch RST, P2 touch INT, P3 LCD
// power, P4 LCD RST. Same sequence as Elecrow's factory firmware.
#include <Wire.h>
#include <USB.h>
static uint8_t pcfState = 0xFF;
static void pcfWrite(uint8_t pin, bool high) {
  if (high) pcfState |= (1 << pin); else pcfState &= ~(1 << pin);
  Wire.beginTransmission(0x21);
  Wire.write(pcfState);
  Wire.endTransmission();
}
static void resetPanelAndTouch() {
  Wire.begin(38, 39);
  pcfWrite(3, true);  delay(100);                    // LCD power
  pcfWrite(4, true);  delay(100);                    // LCD reset pulse
  pcfWrite(4, false); delay(120);
  pcfWrite(4, true);  delay(120);
  pcfWrite(0, true);  delay(100);                    // touch reset pulse
  pcfWrite(0, false); delay(120);
  pcfWrite(0, true);  delay(120);
  pcfWrite(2, true);  delay(120);                    // touch INT
  Wire.end();
}
#endif

#if defined(BUDDY_HOSTED_SDIO_PINS)
#include <USB.h>
#include "esp32-hal-hosted.h"
#endif

#if defined(ELECROW_CROWPANEL_5_0_ADVANCE_HMI)
// CrowPanel Advance: the backlight is driven by an STC8H1K28 co-processor
// on the touch I2C bus, not a PWM pin. v1.1 boards speak a different
// protocol from v1.2/v1.3.
#if defined(PIXELCADE_STC8H1K28_V1_1)
#include "Backlight_STC8H1K28_V1_1.hpp"
#else
#include "Backlight_STC8H1K28.hpp"
#endif
static bool s_stcOk = false;
#endif

void boardPreInit() {
#if defined(BUDDY_HOSTED_SDIO_PINS)
  // USB-OTG CDC: CDC-on-boot alone didn't enumerate reliably on this port
  // in the Sidekick firmware; an explicit begin() did.
  USB.begin();
  // The core's board variant carries another board's SDIO pins; point
  // ESP-Hosted at this board's C6 before anything starts the link.
  hostedSetPins(BUDDY_HOSTED_SDIO_PINS);
#endif
#if defined(PIXELCADE_BOARD_ELECROW_ROUND_2_1)
  // The round board's only USB data port is the S3's native USB; CDC-on-boot
  // alone didn't bring it up reliably in the Sidekick firmware.
  USB.begin();
#endif
#if defined(ELECROW_CROWPANEL_7_0_HMI) || defined(PIXELCADE_BOARD_ELECROW_ROUND_2_1)
  resetPanelAndTouch();
#endif
#if BUDDY_AMP_EN_PIN >= 0
  pinMode(BUDDY_AMP_EN_PIN, OUTPUT);
  digitalWrite(BUDDY_AMP_EN_PIN, HIGH);   // amp off until a note plays
#endif
#if BUDDY_LED_PIN >= 0
  pinMode(BUDDY_LED_PIN, OUTPUT);
  digitalWrite(BUDDY_LED_PIN, HIGH);      // off (active low)
#endif
}

void boardPostInit(LGFX& lcd) {
  (void)lcd;
#if defined(ELECROW_CROWPANEL_5_0_ADVANCE_HMI)
  // Must run after lcd.init(): reuses the I2C port the GT911 driver set up.
  s_stcOk = stc8h1k28::begin();
  if (!s_stcOk) Serial.println("[board] STC8H1K28 backlight chip not detected");
#endif
}

void boardSetBrightness(LGFX& lcd, uint8_t percent) {
  if (percent > 100) percent = 100;
#if defined(ELECROW_CROWPANEL_5_0_ADVANCE_HMI)
  (void)lcd;
  if (!s_stcOk) return;
#if defined(PIXELCADE_STC8H1K28_V1_1)
  stc8h1k28::setBrightnessPercent(percent);
#else
  // v1.2/v1.3: 0 = brightest .. 244 = dimmest, 245 = off.
  stc8h1k28::setBrightness(percent == 0 ? stc8h1k28::kOff
                                        : (uint8_t)(244 - (percent * 244) / 100));
#endif
#else
  lcd.setBrightness((uint16_t)percent * 255 / 100);
#endif
}
