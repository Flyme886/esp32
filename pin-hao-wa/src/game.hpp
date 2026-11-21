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

  // blow on the mic: a troublemaker flies away
  bool listening;             // mic is on (a troublemaker is on the board and nothing is playing)
  float micLevel;             // 0..1, how hard someone is blowing right now
  uint32_t blowStart, blowUntil;
  int blowCol, blowRow;
  uint8_t blowTier;

  // face-down sleep
  uint32_t sleepyStart;       // "Zzz" before the screen goes dark (0 = not falling asleep)
  uint32_t wakeStart, wakeUntil;
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

// the troublemaker to blow away: the highest one on the board (fewest babies above it)
inline bool findBlock(const State& s, int& col, int& row) {
  int best = -1;
  for (int c = 0; c < COLS; ++c) {
    for (int r = ROWS - 1; r >= 0; --r) {
      if (isBlock(s.grid[c][r])) {
        int above = 0;
        while (r + 1 + above < ROWS && s.grid[c][r + 1 + above]) ++above;
        int score = 100 - above * 10 + r;
        if (score > best) { best = score; col = c; row = r; }
        break;
      }
    }
  }
  return best >= 0;
}

// remove (c, r) and let everything above it slide down
inline void removeAt(State& s, int c, int r) {
  for (int i = r; i < ROWS - 1; ++i) s.grid[c][i] = s.grid[c][i + 1];
  s.grid[c][ROWS - 1] = 0;
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
  s.listening = false;
  s.micLevel = 0;
  s.blowStart = s.blowUntil = 0;
  s.blowCol = s.blowRow = 0;
  s.blowTier = 0;
  s.sleepyStart = 0;
  s.wakeStart = s.wakeUntil = 0;
}

}  // namespace game
