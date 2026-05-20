#pragma once
// ============================================================
// touch_keyboard.h — on-device touch keyboard for entering WiFi creds and
// the Anthropic API key.
//
// Resistive single-touch keyboard with three layouts (lowercase, shift,
// numbers/symbols), wide special keys, optional password mask, and a
// "show password" toggle so users can sanity-check what they typed.
//
// `kbdShow()` is modal/blocking — it runs its own inner loop (calling
// M5.update() each iteration) and only returns when the user taps OK or
// CANCEL. BLE callbacks keep firing in the background, so the buddy stays
// reachable from the desktop while typing.
// ============================================================

#include <stddef.h>

// Returns true on OK with the typed text written to `out`, false on
// CANCEL (out is left untouched). Pass `masked=true` for password fields;
// the actual characters are stored verbatim but rendered as `*`. A small
// 👁 toggle in the title bar reveals them while held.
bool kbdShow(const char* title, char* out, size_t outSize, bool masked);
