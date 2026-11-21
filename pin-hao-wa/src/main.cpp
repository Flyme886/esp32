// 拼好蛙 - a tiny drop-and-merge game for the M5Stack StickS3.
//
//   Side button  (BtnB) click : move to the next column
//   Front button (BtnA) click : drop the baby
//   Front button hold 3 s     : start over
//
// Drop a baby onto the same baby and they merge into the next level.
// Drop it onto a different one and you get "轻松绷住".
// Reaching level 3 fills the screen with the big laugh and loops the laugh sound
// until you press a button.

#include <M5Unified.h>
#include <Preferences.h>
#include "game.hpp"
#include "scene.hpp"

#if __has_include("assets/laugh_user.h")
#include "assets/laugh_user.h"       // your laugh, cut from the video (kept out of git)
#else
#include "assets/laugh_default.h"    // synthesised fallback
#endif

static constexpr uint8_t  VOLUME        = 180;   // keep below ~75 % on battery
static constexpr uint32_t DROP_MS_ROW   = 75;    // fall time per row
static constexpr uint32_t POP_MS        = 320;
static constexpr uint32_t TOAST_MS      = 1300;
static constexpr uint32_t CELEBRATE_MAX = 20000; // stop the laugh loop after 20 s if nobody presses
static constexpr int      LAUGH_CH      = 1;
static constexpr uint32_t LAUGH_REPEATS = 12;    // loops queued at once (about 28 s of a 2.3 s laugh)
static constexpr int      LEVEL2_CHANCE = 15;    // % chance the next baby is already level 2

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
static void sndOops()  { melody({{523, 90}, {392, 90}, {330, 160}}); }   // 轻松绷住
static void sndOver()  { melody({{392, 150}, {330, 150}, {262, 300}}); }

static void startPhase(game::Phase p, uint32_t len) {
  g.phase = p;
  g.phaseStart = millis();
  g.phaseLen = len;
}

static void newNext() { g.next = (random(100) < LEVEL2_CHANCE) ? 2 : 1; }

static void addScore(int n, int col, int row) {
  g.score += n;
  if (g.score > g.best) { g.best = g.score; prefs.putUInt("best", g.best); }
  g.gain = n; g.gainCol = col; g.gainRow = row; g.gainUntil = millis() + 700;
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
  if (h >= game::ROWS) {              // column already full
    startPhase(game::OVER, 0);
    sndOver();
    Serial.println("[game over]");
    return;
  }
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
    // repeat inside the speaker driver so the loop has no gap; stopped when the player continues
    M5.Speaker.playRaw(laugh_pcm, sizeof(laugh_pcm) / sizeof(laugh_pcm[0]), LAUGH_RATE, false, LAUGH_REPEATS, LAUGH_CH, true);
    Serial.println("[level 3] laugh");
    return;
  }
  if (r > 0 && g.grid[c][r - 1] == t) {
    g.grid[c][r] = 0;
    g.grid[c][r - 1] = t + 1;
    g.popRow = r - 1;
    addScore(t + 1 == 3 ? 3 : 1, c, r - 1);
    sndPop(t + 1);
    startPhase(game::POP, POP_MS);
    return;
  }
  startPhase(game::IDLE, 0);
}

static void land() {
  int c = g.dropCol, r = g.dropRow, t = g.dropTier;
  if (r > 0 && g.grid[c][r - 1] == t) {           // same baby below: merge
    g.grid[c][r - 1] = t + 1;
    g.popCol = c; g.popRow = r - 1;
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
    startPhase(game::IDLE, 0);
  }
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_imu = false;
  cfg.internal_spk = true;
  cfg.internal_mic = false;
  M5.begin(cfg);
  if (M5.Display.width() > M5.Display.height()) M5.Display.setRotation(M5.Display.getRotation() ^ 1);
  M5.Display.setBrightness(150);
  M5.Speaker.begin();
  M5.Speaker.setVolume(VOLUME);

  canvas.setColorDepth(16);
  canvas.setPsram(true);
  canvas.createSprite(scene::W, scene::H);
  scene::init(&canvas);

  prefs.begin("pinhaowa", false);
  randomSeed(esp_random());
  restart();
  Serial.printf("拼好蛙 ready. pictures: %s\n", HAS_USER_SPRITES ? "yours" : "placeholders");
}

void loop() {
  M5.update();
  const uint32_t now = millis();
  static bool holdArmed = true;

  if (M5.BtnA.pressedFor(3000) && holdArmed) { holdArmed = false; restart(); }
  if (M5.BtnA.wasReleased()) holdArmed = true;

  switch (g.phase) {
    case game::IDLE:
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
        g.grid[celebrateCol][celebrateRow] = 0;      // the level-3 baby leaves the board
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
