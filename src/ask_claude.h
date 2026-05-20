#pragma once
// ============================================================
// ask_claude.h — standalone Claude API client running over WiFi.
//
// A small state machine + streaming SSE parser. The main loop calls
// askTick() every iteration once a request is in flight; the UI reads
// askState() / askResponse() to render progress and the growing reply.
//
// Coexists with BLE: ESP32 time-shares the radio between Bluedroid and
// WiFi, so the desktop bridge keeps working while a request is in
// flight (a tiny stutter on the BLE link during HTTPS handshake is
// normal).
// ============================================================

#include <stdint.h>
#include <stddef.h>

enum AskStateE {
  ASK_IDLE,
  ASK_WIFI_CONNECTING,
  ASK_POSTING,          // TCP/TLS handshake + POST headers
  ASK_STREAMING,        // receiving SSE chunks
  ASK_DONE,
  ASK_ERROR,
};

// Kicks off the request; returns false if creds / API key are missing
// (the UI should route the user to the keyboard in that case).
bool askStart(const char* prompt);

// Reset to IDLE so a fresh prompt can be sent. Also tears down any
// in-flight HTTPS connection. Safe to call repeatedly.
void askReset();

// Drives the network state machine — call every loop while not IDLE.
void askTick();

AskStateE   askState();
const char* askError();        // human-readable error after ASK_ERROR
const char* askResponse();     // accumulated assistant text so far
uint16_t    askResponseGen();  // bumps each time response grows — UI scroll reset

// In milliseconds since the request started — handy for the UI timer.
uint32_t askElapsedMs();
