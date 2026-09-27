#pragma once
#include <stdint.h>

// Multi-species ASCII buddy renderer. Each species lives in its own
// src/buddies/<name>.cpp file and exposes 7 state functions matching
// the PersonaState enum order: sleep, idle, busy, attention, celebrate,
// dizzy, heart.
void buddyInit();
void buddyAdvance();                   // advance the animation clock (call once per frame)
void buddyTick(uint8_t personaState);  // draw the current pose into the canvas
void buddyInvalidate();
// Where the pet lives on the canvas (logical px): horizontal centre, the
// left edge and width of the strip it clears each tick, a vertical offset
// added to every row, and the scale used on the home screen (peek is 1x).
void buddySetGeometry(int xCenter, int x0, int w, int yOff, uint8_t homeScale);
// Height (logical px) of the strip the pet occupies at a given scale.
int  buddyHeight(uint8_t scale);
void buddySetSpecies(const char* name);
void buddySetSpeciesIdx(uint8_t idx);
void buddyNextSpecies();
void buddySetPeek(bool peek);
uint8_t buddySpeciesIdx();
uint8_t buddySpeciesCount();
const char* buddySpeciesName();

// Per-species state function: takes the global tickCount and renders
// the buddy + any overlays for the current state into the shared sprite.
typedef void (*StateFn)(uint32_t t);

struct Species {
  const char* name;
  uint16_t bodyColor;
  StateFn states[7];   // index by PersonaState (0=sleep .. 6=heart)
};
