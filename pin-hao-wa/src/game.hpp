// Game state shared by the logic (main.cpp) and the drawing (scene.hpp).
#pragma once
#include <stdint.h>

namespace game {

static constexpr int COLS = 3;
static constexpr int ROWS = 4;
static constexpr int MAX_TIER = 3;
static constexpr int FIRST_BLOCK = 4;   // 4, 5, 6 are troublemakers: they never merge
static constexpr int KINDS = 6;
inline bool isBlock(int t) { return t >= FIRST_BLOCK; }
inline bool canMerge(int a, int b) { return a == b && a >= 1 && a < MAX_TIER; }

enum Phase : uint8_t { IDLE, DROP, POP, CELEBRATE, OVER };

struct State {
  uint8_t grid[COLS][ROWS];   // 0 = empty, 1..3 = level, 4..6 = troublemaker; row 0 is the bottom
  int cursor;                 // column the next baby will drop into
  uint8_t next;               // level of the next baby
  uint32_t score, best;

  Phase phase;
  uint32_t phaseStart;        // ms
  uint32_t phaseLen;          // ms

  // DROP: a baby falling into (col, row)
  int dropCol, dropRow;
  uint8_t dropTier;

  // POP: the merged baby growing at (col, row)
  int popCol, popRow;

  // "轻松绷住" toast after a mismatch
  uint32_t toastStart, toastUntil;

  // floating "+N"
  int gain;
  int gainCol, gainRow;
  uint32_t gainUntil;

  int fullCol;                // column that refused a drop (it shakes)
  uint32_t fullUntil;
  int combo;                  // merges in a row from one drop
  uint32_t comboUntil;
  bool recordShown;           // "新纪录!" shown once per game
  uint32_t recordUntil;
  bool cleared;               // celebration came from a shake: "通关!", the whole board clears after it
};

inline bool boardFull(const State& s) {
  for (int c = 0; c < COLS; ++c) if (s.grid[c][ROWS - 1] == 0) return false;
  return true;
}

inline bool boardFull(const State& s);
inline int height(const State& s, int c) {
  int h = 0;
  while (h < ROWS && s.grid[c][h]) ++h;
  return h;
}

inline void reset(State& s, uint32_t best) {
  for (auto& col : s.grid) for (auto& v : col) v = 0;
  s.cursor = 1;
  s.next = 1;
  s.score = 0;
  s.best = best;
  s.phase = IDLE;
  s.phaseStart = s.phaseLen = 0;
  s.toastStart = s.toastUntil = 0;
  s.gain = 0;
  s.gainUntil = 0;
  s.fullCol = -1;
  s.fullUntil = 0;
  s.combo = 0;
  s.comboUntil = 0;
  s.recordShown = false;
  s.recordUntil = 0;
  s.cleared = false;
}

}  // namespace game
