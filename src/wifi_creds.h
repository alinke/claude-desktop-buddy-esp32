#pragma once
// ============================================================
// wifi_creds.h — NVS-backed storage for WiFi SSID / password and the
// Anthropic API key used by the standalone "Ask Claude" feature.
//
// All three fields are entered on-device via the touch keyboard
// (touch_keyboard.h) — there is no over-the-wire provisioning, and the
// firmware image itself contains no secrets. NVS is unencrypted on a
// stock ESP32; treat the API key on this device the same way you'd
// treat one stored in plain `.env` on a dev box.
// ============================================================

#include <Arduino.h>
#include <Preferences.h>

// Buffer sizes; arrays in main.cpp / settings UI dimension to these.
#define WIFI_SSID_LEN      33    // max SSID length (32 + NUL)
#define WIFI_PASS_LEN      65    // max WPA2 PSK length (64 + NUL)
#define ANTHROPIC_KEY_LEN  128   // sk-ant-xxxxx... up to ~108 chars in practice

inline void wifiCredsLoad(char* ssid, char* pass) {
  Preferences p;
  p.begin("net", true);
  p.getString("ssid", ssid, WIFI_SSID_LEN);
  p.getString("pass", pass, WIFI_PASS_LEN);
  p.end();
}
inline void wifiCredsSave(const char* ssid, const char* pass) {
  Preferences p;
  p.begin("net", false);
  p.putString("ssid", ssid);
  p.putString("pass", pass);
  p.end();
}
inline bool wifiCredsPresent() {
  Preferences p;
  p.begin("net", true);
  bool has = p.isKey("ssid") && (p.getString("ssid", "") != "");
  p.end();
  return has;
}

inline void apiKeyLoad(char* key) {
  Preferences p;
  p.begin("net", true);
  p.getString("apikey", key, ANTHROPIC_KEY_LEN);
  p.end();
}
inline void apiKeySave(const char* key) {
  Preferences p;
  p.begin("net", false);
  p.putString("apikey", key);
  p.end();
}
inline bool apiKeyPresent() {
  Preferences p;
  p.begin("net", true);
  bool has = p.isKey("apikey") && (p.getString("apikey", "") != "");
  p.end();
  return has;
}
