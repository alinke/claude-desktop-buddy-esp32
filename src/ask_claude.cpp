// ============================================================
// ask_claude.cpp — see ask_claude.h.
// ============================================================
#include "ask_claude.h"
#include "wifi_creds.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Model + endpoint. The latest small/fast model is appropriate for an
// always-on desk device — fast first-token, cheap tokens, plenty of
// quality for short prompts.
static const char* ANTHROPIC_HOST    = "api.anthropic.com";
static const int   ANTHROPIC_PORT    = 443;
static const char* ANTHROPIC_PATH    = "/v1/messages";
static const char* ANTHROPIC_VERSION = "2023-06-01";
static const char* ANTHROPIC_MODEL   = "claude-haiku-4-5";
static const int   ANTHROPIC_MAX_TOK = 512;

// Bounded buffers so the response can't run away with the heap. 1 KB of
// text wraps to ~25 lines on the 240×320 UI — beyond that the user gets
// "...". For longer replies they can use the desktop.
static const size_t RESP_CAP = 1024;
static const size_t LINE_CAP = 1024;   // SSE line buffer; a single delta JSON

static AskStateE        s_state    = ASK_IDLE;
static char             s_err[80]  = {0};
static char             s_resp[RESP_CAP] = {0};
static size_t           s_respLen  = 0;
static uint16_t         s_respGen  = 0;
static uint32_t         s_startMs  = 0;

// Held across tick() invocations while in WIFI_CONNECTING / STREAMING.
static WiFiClientSecure s_client;
static HTTPClient       s_https;
static bool             s_httpOpen = false;
static char             s_line[LINE_CAP];
static size_t           s_lineLen  = 0;
static uint32_t         s_wifiStartMs = 0;
static String           s_pendingBody;   // request body queued while wifi connects

static void _fail(const char* msg) {
  strncpy(s_err, msg, sizeof(s_err) - 1);
  s_err[sizeof(s_err) - 1] = 0;
  s_state = ASK_ERROR;
  if (s_httpOpen) { s_https.end(); s_httpOpen = false; }
  s_client.stop();
}

static void _appendText(const char* t, size_t n) {
  if (n == 0) return;
  size_t room = (RESP_CAP - 1) - s_respLen;
  if (room == 0) return;
  if (n > room) n = room;
  memcpy(s_resp + s_respLen, t, n);
  s_respLen += n;
  s_resp[s_respLen] = 0;
  s_respGen++;
}

// Parse one SSE `data: {...}` JSON payload. We only care about
// content_block_delta with a text_delta inside; everything else is
// noise from the server's content_block_start/stop and ping events.
static void _parseSseDataLine(const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return;
  const char* type = doc["type"];
  if (!type) return;
  if (strcmp(type, "content_block_delta") == 0) {
    JsonObject d = doc["delta"];
    if (d.isNull()) return;
    const char* dtype = d["type"];
    if (dtype && strcmp(dtype, "text_delta") == 0) {
      const char* t = d["text"];
      if (t) _appendText(t, strlen(t));
    }
  } else if (strcmp(type, "message_stop") == 0) {
    s_state = ASK_DONE;
  } else if (strcmp(type, "error") == 0) {
    const char* m = doc["error"]["message"];
    _fail(m ? m : "stream error");
  }
}

bool askStart(const char* prompt) {
  if (!prompt || !*prompt) return false;
  if (!wifiCredsPresent()) { _fail("no wifi credentials"); return false; }
  if (!apiKeyPresent())     { _fail("no api key");         return false; }

  // Build request body up front so we don't need to keep the prompt string
  // alive across tick() invocations. ArduinoJson v7 idiom — no Static prefix
  // and use to<T>() / add<T>() for nested types.
  JsonDocument req;
  req["model"]      = ANTHROPIC_MODEL;
  req["max_tokens"] = ANTHROPIC_MAX_TOK;
  req["stream"]     = true;
  JsonArray  msgs   = req["messages"].to<JsonArray>();
  JsonObject m      = msgs.add<JsonObject>();
  m["role"]         = "user";
  m["content"]      = prompt;
  s_pendingBody     = "";
  serializeJson(req, s_pendingBody);

  // Reset per-request state
  s_resp[0] = 0; s_respLen = 0; s_respGen = 0;
  s_err[0]  = 0;
  s_lineLen = 0;
  s_startMs = millis();

  // Bring WiFi up if it isn't already
  if (WiFi.status() != WL_CONNECTED) {
    char ssid[WIFI_SSID_LEN] = {0};
    char pass[WIFI_PASS_LEN] = {0};
    wifiCredsLoad(ssid, pass);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    s_wifiStartMs = millis();
    s_state = ASK_WIFI_CONNECTING;
  } else {
    s_state = ASK_POSTING;
  }
  return true;
}

void askReset() {
  if (s_httpOpen) { s_https.end(); s_httpOpen = false; }
  s_client.stop();
  s_state    = ASK_IDLE;
  s_err[0]   = 0;
  s_resp[0]  = 0; s_respLen = 0; s_respGen = 0;
  s_lineLen  = 0;
}

static void _openHttp() {
  char key[ANTHROPIC_KEY_LEN] = {0};
  apiKeyLoad(key);
  // Skip cert validation. Anthropic's cert is fine but the bundle adds
  // ~80 KB to flash; we accept "anyone on the path" for a maker device.
  s_client.setInsecure();
  s_client.setTimeout(15000);
  if (!s_https.begin(s_client, ANTHROPIC_HOST, ANTHROPIC_PORT, ANTHROPIC_PATH, true)) {
    _fail("https begin failed");
    return;
  }
  s_httpOpen = true;
  s_https.addHeader("x-api-key",         key);
  s_https.addHeader("anthropic-version", ANTHROPIC_VERSION);
  s_https.addHeader("Content-Type",      "application/json");
  s_https.addHeader("Accept",            "text/event-stream");
  int code = s_https.POST(s_pendingBody);
  if (code != HTTP_CODE_OK) {
    char buf[64];
    snprintf(buf, sizeof(buf), "HTTP %d", code);
    _fail(buf);
    return;
  }
  s_state = ASK_STREAMING;
}

void askTick() {
  switch (s_state) {
    case ASK_IDLE:
    case ASK_DONE:
    case ASK_ERROR:
      return;

    case ASK_WIFI_CONNECTING: {
      if (WiFi.status() == WL_CONNECTED) { s_state = ASK_POSTING; return; }
      if (millis() - s_wifiStartMs > 15000) _fail("wifi timeout");
      return;
    }

    case ASK_POSTING:
      _openHttp();
      return;

    case ASK_STREAMING: {
      WiFiClient* s = s_https.getStreamPtr();
      if (!s) { _fail("stream lost"); return; }

      // Drain whatever the TLS layer has handed up since last tick, one
      // line at a time. Lines are SSE events terminated with \n. Empty
      // lines separate events; we don't need to track that boundary —
      // each "data:" line is a complete JSON delta on its own.
      while (s->available() > 0) {
        int b = s->read();
        if (b < 0) break;
        if (b == '\r') continue;
        if (b == '\n') {
          s_line[s_lineLen] = 0;
          if (strncmp(s_line, "data: ", 6) == 0) {
            _parseSseDataLine(s_line + 6);
          }
          s_lineLen = 0;
        } else if (s_lineLen + 1 < LINE_CAP) {
          s_line[s_lineLen++] = (char)b;
        } else {
          // overflow on a single line — almost certainly the server is
          // streaming a giant delta. Reset and keep going.
          s_lineLen = 0;
        }
      }

      // Server closed the stream
      if (!s_https.connected() && s->available() == 0 && s_state == ASK_STREAMING) {
        s_state = ASK_DONE;
      }
      // Hard ceiling — don't sit on a half-open connection forever
      if (millis() - s_startMs > 60000) _fail("stream timeout");

      if (s_state == ASK_DONE || s_state == ASK_ERROR) {
        s_https.end();
        s_httpOpen = false;
      }
      return;
    }
  }
}

AskStateE   askState()       { return s_state; }
const char* askError()       { return s_err; }
const char* askResponse()    { return s_resp; }
uint16_t    askResponseGen() { return s_respGen; }
uint32_t    askElapsedMs()   { return millis() - s_startMs; }
