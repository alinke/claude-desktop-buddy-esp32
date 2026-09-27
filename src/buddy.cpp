#include "buddy.h"
#include "buddy_common.h"
#include "hal_m5.h"
#include "canvas.h"
#include <string.h>

// Mirrors PersonaState in main.cpp
enum { B_SLEEP, B_IDLE, B_BUSY, B_ATTENTION, B_CELEBRATE, B_DIZZY, B_HEART };

// ──────────────── shared geometry ────────────────
// Species art is authored against a fixed 1x frame (Y_BASE / Y_OVERLAY) and
// centred on BUDDY_X_CENTER. The layout in main.cpp moves the centre, the
// clear strip and a vertical offset per screen shape via buddySetGeometry().
int BUDDY_X_CENTER = 120;
int BUDDY_CANVAS_W = 240;
const int BUDDY_Y_BASE   = 30;
const int BUDDY_Y_OVERLAY = 6;
const int BUDDY_CHAR_W   = 6;
const int BUDDY_CHAR_H   = 8;

// ──────────────── shared colors ────────────────
const uint16_t BUDDY_BG     = 0x0000;
const uint16_t BUDDY_HEART  = 0xF810;
const uint16_t BUDDY_DIM    = 0x8410;
const uint16_t BUDDY_YEL    = 0xFFE0;
const uint16_t BUDDY_WHITE  = 0xFFFF;
const uint16_t BUDDY_CYAN   = 0x07FF;
const uint16_t BUDDY_GREEN  = 0x07E0;
const uint16_t BUDDY_PURPLE = 0xA01F;
const uint16_t BUDDY_RED    = 0xF800;
const uint16_t BUDDY_BLUE   = 0x041F;

// ──────────────── shared rendering helpers ────────────────
static Canvas* _tgt = &spr;
// Home scale (2x, or 3x on tall portrait screens) and 1x in peek (PET/INFO).
// Species art is space-padded to a fixed width for alignment at 1x; above
// that we trim and re-center per line so the padding doesn't push ink
// off-screen.
static uint8_t _scale = 1;
static uint8_t _homeScale = 2;
static int     _canvasX0 = 0;
static int     _yOff = 0;
static bool    _peek = false;

void buddyPrintLine(const char* line, int yPx, uint16_t color, int xOff) {
  int len = strlen(line);
  if (_scale > 1) {
    while (len && line[len-1] == ' ') len--;
    while (len && *line == ' ')       { line++; len--; }
  }
  int w = len * BUDDY_CHAR_W * _scale;
  int x = BUDDY_X_CENTER - w / 2 + xOff * _scale;
  _tgt->setTextColor(color, BUDDY_BG);
  _tgt->setCursor(x, yPx);
  for (int i = 0; i < len; i++) _tgt->print(line[i]);
}

void buddyPrintSprite(const char* const* lines, uint8_t nLines, int yOffset, uint16_t color, int xOff) {
  _tgt->setTextSize(_scale);
  int yBase = BUDDY_Y_BASE * _scale - (_scale - 1) * 14 + _yOff;
  for (uint8_t i = 0; i < nLines; i++) {
    buddyPrintLine(lines[i], yBase + (yOffset + i * BUDDY_CHAR_H) * _scale, color, xOff);
  }
}

// Species pass 1× coords (relative to BUDDY_X_CENTER / BUDDY_Y_OVERLAY);
// transform here so all 18 species files stay scale-agnostic.
void buddySetCursor(int x, int y) {
  _tgt->setCursor(BUDDY_X_CENTER + (x - BUDDY_X_CENTER) * _scale, y * _scale + _yOff);
}
void buddySetColor(uint16_t fg)   { _tgt->setTextColor(fg, BUDDY_BG); }
void buddyPrint(const char* s)    { _tgt->setTextSize(_scale); _tgt->print(s); }

// ──────────────── species registry ────────────────
extern const Species CAPYBARA_SPECIES;
extern const Species DUCK_SPECIES;
extern const Species GOOSE_SPECIES;
extern const Species BLOB_SPECIES;
extern const Species CAT_SPECIES;
extern const Species DRAGON_SPECIES;
extern const Species OCTOPUS_SPECIES;
extern const Species OWL_SPECIES;
extern const Species PENGUIN_SPECIES;
extern const Species TURTLE_SPECIES;
extern const Species SNAIL_SPECIES;
extern const Species GHOST_SPECIES;
extern const Species AXOLOTL_SPECIES;
extern const Species CACTUS_SPECIES;
extern const Species ROBOT_SPECIES;
extern const Species RABBIT_SPECIES;
extern const Species MUSHROOM_SPECIES;
extern const Species CHONK_SPECIES;

static const Species* SPECIES_TABLE[] = {
  &CAPYBARA_SPECIES, &DUCK_SPECIES, &GOOSE_SPECIES, &BLOB_SPECIES,
  &CAT_SPECIES, &DRAGON_SPECIES, &OCTOPUS_SPECIES, &OWL_SPECIES,
  &PENGUIN_SPECIES, &TURTLE_SPECIES, &SNAIL_SPECIES, &GHOST_SPECIES,
  &AXOLOTL_SPECIES, &CACTUS_SPECIES, &ROBOT_SPECIES, &RABBIT_SPECIES,
  &MUSHROOM_SPECIES, &CHONK_SPECIES,
};
static const uint8_t N_SPECIES = sizeof(SPECIES_TABLE) / sizeof(SPECIES_TABLE[0]);
static uint8_t currentSpeciesIdx = 0;

// ──────────────── tick state ────────────────
static uint32_t tickCount  = 0;
static uint32_t nextTickAt = 0;
static const uint32_t TICK_MS = 200;

#include "stats.h"

void buddyInit() {
  tickCount = 0;
  nextTickAt = 0;
  uint8_t saved = speciesIdxLoad();
  if (saved < N_SPECIES) currentSpeciesIdx = saved;
}

void buddySetSpeciesIdx(uint8_t idx) {
  if (idx < N_SPECIES) currentSpeciesIdx = idx;
}

void buddySetSpecies(const char* name) {
  for (uint8_t i = 0; i < N_SPECIES; i++) {
    if (strcmp(SPECIES_TABLE[i]->name, name) == 0) {
      currentSpeciesIdx = i;
      return;
    }
  }
}

const char* buddySpeciesName() {
  return SPECIES_TABLE[currentSpeciesIdx]->name;
}

uint8_t buddySpeciesCount() { return N_SPECIES; }

uint8_t buddySpeciesIdx() { return currentSpeciesIdx; }

void buddyNextSpecies() {
  currentSpeciesIdx = (currentSpeciesIdx + 1) % N_SPECIES;
  speciesIdxSave(currentSpeciesIdx);
}

// The UI is drawn immediate-mode (see canvas.h), so the pet is drawn every
// frame; buddyAdvance() moves the animation on at TICK_MS (5 fps) and
// buddyTick() just draws the current pose.
void buddyInvalidate() {}

void buddyAdvance() {
  uint32_t now = millis();
  if ((int32_t)(now - nextTickAt) >= 0) {
    nextTickAt = now + TICK_MS;
    tickCount++;
  }
}

void buddySetPeek(bool peek) {
  _peek = peek;
  uint8_t s = peek ? 1 : _homeScale;
  if (s == _scale) return;
  _scale = s;
  buddyInvalidate();
}

void buddySetGeometry(int xCenter, int x0, int w, int yOff, uint8_t homeScale) {
  BUDDY_X_CENTER = xCenter;
  _canvasX0      = x0;
  BUDDY_CANVAS_W = w;
  _yOff          = yOff;
  _homeScale     = homeScale ? homeScale : 2;
  _scale         = _peek ? 1 : _homeScale;
  buddyInvalidate();
}

int buddyHeight(uint8_t scale) {
  return (BUDDY_Y_BASE + 5 * BUDDY_CHAR_H + 12) * scale;
}

void buddyTick(uint8_t personaState) {
  if (personaState >= 7) personaState = B_IDLE;
  // Clear the whole render strip — at 2× the body reaches y≈126, at 1× ≈82.
  spr.fillRect(_canvasX0, _yOff, BUDDY_CANVAS_W, buddyHeight(_scale), BUDDY_BG);
  const Species* sp = SPECIES_TABLE[currentSpeciesIdx];
  if (sp->states[personaState]) sp->states[personaState](tickCount);
}
