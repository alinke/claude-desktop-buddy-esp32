#pragma once
// ============================================================
// serial_ota.h — firmware update over the USB serial link.
//
// For boards whose USB port esptool can't reset into the bootloader
// (native USB-OTG ports such as the Waveshare ESP32-P4's). Same wire format
// as the Pixelcade Sidekick firmware's serial OTA, so its uploader works
// unchanged (tools/serial_ota_upload.py there):
//
//   host -> "FWB1" + u32 size (LE) + 32 hex chars MD5     ack: "OKA1" + 1/0
//   host -> "DAT1" + u32 len  (LE) + len bytes (<= 8 KB)  ack: "OKA1" + 1/0
//   host -> "END1"   verify MD5, switch boot partition, reboot (no ack)
//   host -> "ABT1"   abandon the transfer (no ack)
//
// Needs a partition table with two OTA app slots (e.g. default_16MB.csv);
// on the factory-only 4 MB layout the FWB1 ack is a failure.
//
// data.h hands control over when a line on USB starts with "FWB1"; the
// transfer then runs to completion here, blocking the UI.
// ============================================================
#include <Arduino.h>
#include <Update.h>

namespace serial_ota {

static bool readExact(Stream& s, uint8_t* dst, size_t n, uint32_t timeoutMs) {
  uint32_t last = millis();
  size_t got = 0;
  while (got < n) {
    int avail = s.available();
    if (avail > 0) {
      size_t take = min((size_t)avail, n - got);
      got += s.readBytes(dst + got, take);
      last = millis();
    } else if (millis() - last > timeoutMs) {
      return false;
    } else {
      delay(1);
    }
  }
  return true;
}

static void ack(Stream& s, bool ok) {
  s.write((const uint8_t*)"OKA1", 4);
  s.write((uint8_t)(ok ? 1 : 0));
  s.flush();
}

static uint32_t readU32(const uint8_t* p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// Called after "FWB1" has been read from s.
inline void run(Stream& s) {
  uint8_t hdr[36];
  if (!readExact(s, hdr, sizeof(hdr), 5000)) { Serial.println("[fwu] header timeout"); return; }
  uint32_t total = readU32(hdr);
  char md5[33];
  memcpy(md5, hdr + 4, 32);
  md5[32] = 0;
  bool ok = Update.begin(total);
  if (ok) ok = Update.setMD5(md5);
  Serial.printf("[fwu] begin %u bytes: %s\n", (unsigned)total, ok ? "ok" : Update.errorString());
  ack(s, ok);
  if (!ok) return;

  const size_t CHUNK_MAX = 8192;
  uint8_t* chunk = (uint8_t*)malloc(CHUNK_MAX);
  if (!chunk) { Update.abort(); return; }
  uint8_t magic[4];
  while (true) {
    if (!readExact(s, magic, 4, 15000)) { Serial.println("[fwu] timeout, aborting"); Update.abort(); break; }
    if (!memcmp(magic, "DAT1", 4)) {
      uint8_t lb[4];
      if (!readExact(s, lb, 4, 5000)) { Update.abort(); break; }
      uint32_t len = readU32(lb);
      bool good = len <= CHUNK_MAX && readExact(s, chunk, len, 5000)
                  && Update.write(chunk, len) == len;
      ack(s, good);
      if (!good) { Serial.printf("[fwu] write failed: %s\n", Update.errorString()); Update.abort(); break; }
    } else if (!memcmp(magic, "END1", 4)) {
      if (Update.end()) {
        Serial.println("[fwu] end: success, rebooting into new firmware");
        Serial.flush();
        delay(300);
        ESP.restart();
      }
      Serial.printf("[fwu] end failed: %s\n", Update.errorString());
      break;
    } else if (!memcmp(magic, "ABT1", 4)) {
      Serial.println("[fwu] aborted by host");
      Update.abort();
      break;
    } else {
      Serial.println("[fwu] unexpected frame, aborting");
      Update.abort();
      break;
    }
  }
  free(chunk);
}

}  // namespace serial_ota
