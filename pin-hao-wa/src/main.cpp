// 拼好蛙 - a tiny drop-and-merge game for the M5Stack StickS3.
//
//   Side button  (BtnB) click : move to the next column
//   Front button (BtnA) click : drop the baby
//   Front button hold 3 s     : start over
//
// Shake the stick (three quick jolts) to clear the board straight away: "通关!".
//
// Small helps: the lane turns green and the landing spot gets a green frame when the
// next baby would merge there; a full column shakes instead of ending the game (only a
// completely full board ends it); chains show "连击"; a new best shows "新纪录!";
// the screen dims after 30 s and the first press only wakes it.
//
// Drop a baby onto the same baby and they merge into the next level.
// Drop it onto a different one and 熊大 pops up: "轻松绷住".
// Reaching level 3 fills the screen with the big laugh and loops the laugh sound
// until you press a button.

#include <M5Unified.h>
#include <Preferences.h>
#include "game.hpp"
#include "scene.hpp"

#include "assets/laugh.h"            // the laugh, cut from seconds 1-4 of the video
#include "assets/oops_sound.h"       // "轻松绷住", cut from seconds 3-4.5 of the second video

static constexpr uint8_t  VOLUME        = 180;   // keep below ~75 % on battery
static constexpr uint32_t DROP_MS_ROW   = 75;    // fall time per row
static constexpr uint32_t POP_MS        = 320;
static constexpr uint32_t TOAST_MS      = 1900;  // a little longer than the 1.5 s sound
static constexpr uint32_t CELEBRATE_MAX = 20000; // stop the laugh loop after 20 s if nobody presses
static constexpr int      LAUGH_CH      = 1;
static constexpr int      OOPS_CH       = 2;
static constexpr uint32_t LAUGH_REPEATS = 12;    // loops queued at once (about 28 s of a 2.3 s laugh)
static constexpr int      LEVEL2_CHANCE = 15;    // % chance the next baby is already level 2
static constexpr uint32_t DIM_AFTER_MS  = 30000; // dim the screen when nobody plays
static constexpr uint8_t  BRIGHT        = 150, DIM = 20;
static constexpr float    SHAKE_G       = 1.9f;  // one jolt of a shake
static constexpr int      SHAKE_COUNT   = 3;     // jolts needed ...
static constexpr uint32_t SHAKE_WINDOW  = 900;   // ... within this many ms

static M5Canvas canvas(&M5.Display);
static Preferences prefs;
static game::State g;
static int celebrateCol = 0, celebrateRow = 0;

// ---- little sounds (synth tones) ----
struct Note { uint32_t at; uint16_t freq; uint16_t ms; };
static Note notes[16];
static int noteCount = 0;
static void melody(std::initializer_list<std::pair<uint16_t, uint16_t>> m, uint32_t delay = 0) {
  uint32_t t = millis() + delay;
  for (auto& n : m) {
    if (noteCount < 16) notes[noteCount++] = {t, n.first, n.second};
    t += n.second + 15;
  }
}
static void sndMove()  { melody({{1568, 25}}); }
static void sndDrop()  { melody({{880, 30}}); }
static void sndPop(int tier) { if (tier >= 3) melody({{1046, 60}, {1318, 60}, {1568, 60}, {2093, 120}}); else melody({{1318, 60}, {1760, 90}}); }
static void sndOops() {   // 轻松绷住
  M5.Speaker.playRaw(oops_pcm, sizeof(oops_pcm) / sizeof(oops_pcm[0]), OOPS_RATE, false, 1, OOPS_CH, true);
}
static void sndOver()  { melody({{392, 150}, {330, 150}, {262, 300}}); }
static void sndFull()  { melody({{220, 70}, {220, 70}}); }   // column is full, try another
static void sndRecord(){ melody({{1568, 70}, {2093, 120}}, 250); }

static void startPhase(game::Phase p, uint32_t len) {
  g.phase = p;
  g.phaseStart = millis();
  g.phaseLen = len;
}

static void newNext() { g.next = (random(100) < LEVEL2_CHANCE) ? 2 : 1; }

static void addScore(int n, int col, int row) {
  g.score += n;
  if (g.score > g.best) {
    if (!g.recordShown && g.best > 0) { g.recordShown = true; g.recordUntil = millis() + 1500; sndRecord(); }
    g.best = g.score;
    prefs.putUInt("best", g.best);
  }
  g.gain = n; g.gainCol = col; g.gainRow = row; g.gainUntil = millis() + 700;
}

static void startLaugh() {
  // repeat inside the speaker driver so the loop has no gap; stopped when the player continues
  M5.Speaker.playRaw(laugh_pcm, sizeof(laugh_pcm) / sizeof(laugh_pcm[0]), LAUGH_RATE, false, LAUGH_REPEATS, LAUGH_CH, true);
}

// shake to win: celebrate now, the whole board clears afterwards
static void shakeClear() {
  g.cleared = true;
  g.toastUntil = 0;
  startPhase(game::CELEBRATE, CELEBRATE_MAX);
  sndPop(3);
  startLaugh();
  Serial.println("[shake] 通关");
}

// count quick jolts; returns true when the stick was shaken
static bool shaken(uint32_t now) {
  static uint32_t jolts[SHAKE_COUNT] = {0};
  static int idx = 0;
  static bool above = false;
  if (!M5.Imu.update()) return false;
  auto d = M5.Imu.getImuData();
  float a = sqrtf(d.accel.x * d.accel.x + d.accel.y * d.accel.y + d.accel.z * d.accel.z);
  bool hit = a > SHAKE_G;
  if (hit && !above) {                       // rising edge = one jolt
    jolts[idx] = now;
    idx = (idx + 1) % SHAKE_COUNT;
    uint32_t oldest = jolts[idx];            // the jolt SHAKE_COUNT-1 jolts ago
    if (oldest && now - oldest < SHAKE_WINDOW) {
      for (auto& j : jolts) j = 0;
      above = hit;
      return true;
    }
  }
  above = hit;
  return false;
}

static void restart() {
  game::reset(g, prefs.getUInt("best", 0));
  newNext();
  noteCount = 0;
  M5.Speaker.stop(LAUGH_CH);
  Serial.println("[restart]");
}

static void drop() {
  int c = g.cursor;
  int h = game::height(g, c);
  if (h >= game::ROWS) {              // column already full: refuse, the player picks another
    if (game::boardFull(g)) {
      startPhase(game::OVER, 0);
      sndOver();
      Serial.println("[game over]");
    } else {
      g.fullCol = c;
      g.fullUntil = millis() + 400;
      sndFull();
    }
    return;
  }
  g.combo = 0;
  g.dropCol = c; g.dropRow = h; g.dropTier = g.next;
  startPhase(game::DROP, DROP_MS_ROW * (game::ROWS - h) + 60);
  sndDrop();
  newNext();
}

// after a merge, see whether the new baby matches the one below it (chain merge)
static void checkChain() {
  int c = g.popCol, r = g.popRow, t = g.grid[c][r];
  if (t >= game::MAX_TIER) {
    celebrateCol = c; celebrateRow = r;
    addScore(10, c, r);
    startPhase(game::CELEBRATE, CELEBRATE_MAX);
    g.cleared = false;
    startLaugh();
    Serial.println("[level 3] laugh");
    return;
  }
  if (r > 0 && g.grid[c][r - 1] == t) {
    g.grid[c][r] = 0;
    g.grid[c][r - 1] = t + 1;
    g.popRow = r - 1;
    g.combo++;
    g.comboUntil = millis() + 900;
    addScore(t + 1 == 3 ? 3 : 1, c, r - 1);
    sndPop(t + 1);
    startPhase(game::POP, POP_MS);
    return;
  }
  startPhase(game::IDLE, 0);
  if (game::boardFull(g)) { startPhase(game::OVER, 0); sndOver(); }
}

static void land() {
  int c = g.dropCol, r = g.dropRow, t = g.dropTier;
  if (r > 0 && g.grid[c][r - 1] == t) {           // same baby below: merge
    g.grid[c][r - 1] = t + 1;
    g.popCol = c; g.popRow = r - 1;
    g.combo = 1;
    addScore(t + 1 == 3 ? 3 : 1, c, r - 1);
    sndPop(t + 1);
    startPhase(game::POP, POP_MS);
    Serial.printf("[merge] level %d\n", t + 1);
  } else {
    g.grid[c][r] = t;
    if (r > 0) {                                     // different baby below
      g.toastStart = millis();
      g.toastUntil = g.toastStart + TOAST_MS;
      sndOops();
      Serial.println("[mismatch] 轻松绷住");
    }
    if (game::boardFull(g)) { startPhase(game::OVER, 0); sndOver(); Serial.println("[game over]"); return; }
    startPhase(game::IDLE, 0);
  }
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_imu = true;
  cfg.internal_spk = true;
  cfg.internal_mic = false;
  M5.begin(cfg);
  if (M5.Display.width() > M5.Display.height()) M5.Display.setRotation(M5.Display.getRotation() ^ 1);
  M5.Display.setBrightness(BRIGHT);
  M5.Speaker.begin();
  M5.Speaker.setVolume(VOLUME);

  canvas.setColorDepth(16);
  canvas.setPsram(true);
  canvas.createSprite(scene::W, scene::H);
  scene::init(&canvas);

  prefs.begin("pinhaowa", false);
  randomSeed(esp_random());
  restart();
  Serial.println("拼好蛙 ready");
}

void loop() {
  M5.update();
  const uint32_t now = millis();
  static bool holdArmed = true;
  static uint32_t lastInput = 0;
  static bool dimmed = false;

  // dim after a while; the first press only wakes the screen
  bool anyPress = M5.BtnA.wasPressed() || M5.BtnB.wasPressed();
  if (anyPress) lastInput = now;
  if (dimmed) {
    if (anyPress) {
      dimmed = false;
      M5.Display.setBrightness(BRIGHT);
      while (M5.BtnA.isPressed() || M5.BtnB.isPressed()) { M5.update(); delay(10); }
      M5.update();
    }
    delay(30);
    return;
  }
  if (g.phase != game::CELEBRATE && now - lastInput > DIM_AFTER_MS) {
    dimmed = true;
    M5.Display.setBrightness(DIM);
  }

  if (M5.BtnA.pressedFor(3000) && holdArmed) { holdArmed = false; restart(); }
  if (M5.BtnA.wasReleased()) holdArmed = true;

  bool shake = shaken(now);
  if (shake) lastInput = now;

  switch (g.phase) {
    case game::IDLE:
      if (shake) { shakeClear(); break; }
      if (M5.BtnB.wasClicked()) { g.cursor = (g.cursor + 1) % game::COLS; sndMove(); }
      if (M5.BtnA.wasClicked()) drop();
      break;
    case game::DROP:
      if (now - g.phaseStart >= g.phaseLen) land();
      break;
    case game::POP:
      if (now - g.phaseStart >= g.phaseLen) checkChain();
      break;
    case game::CELEBRATE: {
      bool pressed = M5.BtnA.wasClicked() || M5.BtnB.wasClicked();
      bool done = (pressed && now - g.phaseStart > 800) || now - g.phaseStart > g.phaseLen;
      if (done) {
        M5.Speaker.stop(LAUGH_CH);
        if (g.cleared) {                              // shake win: fresh board, score kept
          for (auto& col : g.grid) for (auto& v : col) v = 0;
          g.cleared = false;
        } else {
          g.grid[celebrateCol][celebrateRow] = 0;     // the level-3 baby leaves the board
        }
        startPhase(game::IDLE, 0);
      } else if (!M5.Speaker.isPlaying(LAUGH_CH)) {  // loop the laugh
        M5.Speaker.playRaw(laugh_pcm, sizeof(laugh_pcm) / sizeof(laugh_pcm[0]), LAUGH_RATE, false, LAUGH_REPEATS, LAUGH_CH, false);
      }
      break;
    }
    case game::OVER:
      if (M5.BtnA.wasClicked()) restart();
      break;
  }

  for (int i = 0; i < noteCount;) {
    if ((int32_t)(now - notes[i].at) >= 0) {
      M5.Speaker.tone(notes[i].freq, notes[i].ms, 0, false);
      notes[i] = notes[--noteCount];
    } else {
      ++i;
    }
  }

  scene::draw(&canvas, g, now);
  canvas.pushSprite(&M5.Display, 0, 0);
  delay(16);
}
