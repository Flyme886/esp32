// 拼好蛙 - a tiny drop-and-merge game for the M5Stack StickS3.
//
//   Side button  (BtnB) click : move to the next column
//   Front button (BtnA) click : drop the baby
//   Front button hold 3 s     : start over
//
// Troublemakers (三张捣乱图) sometimes come up as the next piece. They never merge,
// and whenever one lands, or a baby lands on one, 熊大 says 轻松绷住.
//
// Blow into the mic: the highest troublemaker on the board is blown away ("咻~").
// The mic only listens while a troublemaker is on the board and no sound is playing
// (the StickS3 cannot use its mic and speaker at the same time).
//
// Lay the stick screen-down: the babies fall asleep ("Zzz", a snore, screen off).
// Turn it back over: they stretch and say "醒啦~".
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
static constexpr int      BLOCK_CHANCE  = 12;    // % chance the next piece is a troublemaker
static constexpr uint32_t DIM_AFTER_MS  = 30000; // dim the screen when nobody plays
static constexpr uint8_t  BRIGHT        = 150, DIM = 20;
static constexpr float    SHAKE_G       = 1.9f;  // one jolt of a shake
static constexpr int      SHAKE_COUNT   = 3;     // jolts needed ...
static constexpr uint32_t SHAKE_WINDOW  = 900;   // ... within this many ms
static constexpr int      MIC_RATE      = 16000;
static constexpr int      MIC_FRAME     = 320;   // 20 ms per frame
static constexpr float    BLOW_RMS      = 2500;  // frame loudness that counts as blowing (see serial log)
static constexpr int      BLOW_FRAMES   = 12;    // ~0.25 s of blowing ...
static constexpr int      BLOW_WINDOW   = 16;    // ... within the last ~0.3 s
static constexpr uint32_t BLOW_MS       = 650;
static constexpr float    FACE_DOWN_G   = 0.80f; // gravity along the screen axis that counts as face down
static constexpr uint32_t SLEEP_AFTER   = 1200;  // face down this long -> sleep
static constexpr uint32_t ZZZ_MS        = 1400;  // "Zzz" before the screen goes dark
static constexpr uint32_t WAKE_MS       = 900;

static M5Canvas canvas(&M5.Display);
static Preferences prefs;
static game::State g;
static int celebrateCol = 0, celebrateRow = 0;

// ---- speaker / mic: the StickS3 can only use one of them at a time ----
static bool micOn = false;
static uint32_t lastSoundAt = 0;
static void useSpeaker() {
  if (micOn) {
    M5.Mic.end();
    micOn = false;
    g.listening = false;
    M5.Speaker.begin();
    M5.Speaker.setVolume(VOLUME);
  }
  lastSoundAt = millis();
}
static void useMic() {
  if (!micOn) {
    M5.Speaker.end();
    M5.Mic.begin();
    micOn = true;
  }
}

// ---- little sounds (synth tones) ----
struct Note { uint32_t at; uint16_t freq; uint16_t ms; };
static Note notes[16];
static int noteCount = 0;
static void melody(std::initializer_list<std::pair<uint16_t, uint16_t>> m, uint32_t delay = 0) {
  useSpeaker();
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
  useSpeaker();
  M5.Speaker.playRaw(oops_pcm, sizeof(oops_pcm) / sizeof(oops_pcm[0]), OOPS_RATE, false, 1, OOPS_CH, true);
}
static void sndOver()  { melody({{392, 150}, {330, 150}, {262, 300}}); }
static void sndFull()  { melody({{220, 70}, {220, 70}}); }   // column is full, try another
static void sndRecord(){ melody({{1568, 70}, {2093, 120}}, 250); }
static void sndWhoosh() { melody({{900, 40}, {1300, 40}, {1800, 40}, {2400, 80}}); }
static void sndSnore()  { melody({{110, 380}, {0, 200}, {98, 520}}); }
static void sndWake()   { melody({{784, 70}, {988, 70}, {1318, 140}}); }

static void startPhase(game::Phase p, uint32_t len) {
  g.phase = p;
  g.phaseStart = millis();
  g.phaseLen = len;
}

static void newNext() {
  int r = random(100);
  if (r < BLOCK_CHANCE) g.next = game::FIRST_BLOCK + random(3);
  else g.next = (r < BLOCK_CHANCE + LEVEL2_CHANCE) ? 2 : 1;
}

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
  useSpeaker();
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

// ---- blow detection ----
static int16_t micBuf[MIC_FRAME];
static bool blownFrames[BLOW_WINDOW];
static int blowIdx = 0;
static uint32_t lastMicLog = 0;

static bool listenForBlow(uint32_t now) {
  if (!M5.Mic.record(micBuf, MIC_FRAME, MIC_RATE)) return false;
  while (M5.Mic.isRecording()) delay(1);
  double mean = 0, sq = 0;
  for (int i = 0; i < MIC_FRAME; ++i) mean += micBuf[i];
  mean /= MIC_FRAME;
  for (int i = 0; i < MIC_FRAME; ++i) { double v = micBuf[i] - mean; sq += v * v; }
  float rms = sqrtf(sq / MIC_FRAME);
  g.micLevel = fminf(1.0f, rms / (BLOW_RMS * 2));
  if (rms > BLOW_RMS * 0.5f && now - lastMicLog > 300) { Serial.printf("[mic] rms %d\n", (int)rms); lastMicLog = now; }
  blownFrames[blowIdx] = rms > BLOW_RMS;
  blowIdx = (blowIdx + 1) % BLOW_WINDOW;
  int n = 0;
  for (bool b : blownFrames) n += b;
  if (n >= BLOW_FRAMES) { for (bool& b : blownFrames) b = false; return true; }
  return false;
}

static void blowAway() {
  int c, r;
  if (!game::findBlock(g, c, r)) return;
  g.blowCol = c; g.blowRow = r; g.blowTier = g.grid[c][r];
  game::removeAt(g, c, r);
  g.blowStart = millis();
  g.blowUntil = g.blowStart + BLOW_MS;
  g.micLevel = 0;
  sndWhoosh();
  Serial.println("[blow] troublemaker blown away");
}

// ---- face-down sleep ----
enum Sleep : uint8_t { AWAKE, DROWSY, ASLEEP };
static Sleep sleepState = AWAKE;
static uint32_t faceDownSince = 0, faceUpSince = 0;
static float faceUpSign = 1.0f;     // learnt from how the stick lies when a button is pressed
static bool faceUpKnown = false;    // no sleeping until we know which side the screen is on

static float screenAxisG() {         // + = screen up, - = screen down
  auto d = M5.Imu.getImuData();
  return d.accel.z * faceUpSign;
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
  if (r > 0 && game::canMerge(g.grid[c][r - 1], t)) {
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
  if (r > 0 && game::canMerge(g.grid[c][r - 1], t)) {   // same baby below: merge
    g.grid[c][r - 1] = t + 1;
    g.popCol = c; g.popRow = r - 1;
    g.combo = 1;
    addScore(t + 1 == 3 ? 3 : 1, c, r - 1);
    sndPop(t + 1);
    startPhase(game::POP, POP_MS);
    Serial.printf("[merge] level %d\n", t + 1);
  } else {
    g.grid[c][r] = t;
    if (r > 0 || game::isBlock(t)) {                 // different one below, or a troublemaker
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
  cfg.internal_mic = true;   // configured here, switched on only while listening
  M5.begin(cfg);
  if (M5.Display.width() > M5.Display.height()) M5.Display.setRotation(M5.Display.getRotation() ^ 1);
  M5.Display.setBrightness(BRIGHT);
  M5.Speaker.begin();
  M5.Speaker.setVolume(VOLUME);
  // AW8737A amp to Mode 3 (medium gain, 2 pulses) — louder than default without brownout reboots.
  // Amp MODE pin is wired to PM1 coprocessor GPIO3 (0x6E @ I2C);
  // register 0x53: [7]=refresh, [6:5]=pulse count, [4:0]=PM1 gpio number.
  M5.In_I2C.writeRegister8(0x6E, 0x53, 0x80 | (2 << 5) | 0x03, 100000);

  canvas.setColorDepth(16);
  canvas.setPsram(true);
  canvas.createSprite(scene::W, scene::H);
  scene::init(&canvas);

  for (int i = 0; i < 20; ++i) { M5.Imu.update(); delay(5); }
  {
    auto d = M5.Imu.getImuData();
    if (fabsf(d.accel.z) > 0.7f) { faceUpSign = d.accel.z > 0 ? 1.0f : -1.0f; faceUpKnown = true; }
  }

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

  M5.Imu.update();

  // ---- face-down sleep ----
  {
    float z = screenAxisG();
    if (z < -FACE_DOWN_G) { if (!faceDownSince) faceDownSince = now; faceUpSince = 0; }
    else { faceDownSince = 0; if (!faceUpSince) faceUpSince = now; }

    if (sleepState == AWAKE && faceUpKnown && faceDownSince && now - faceDownSince > SLEEP_AFTER) {
      sleepState = DROWSY;
      g.sleepyStart = now;
      M5.Speaker.stop();
      noteCount = 0;
      if (g.phase == game::CELEBRATE) startPhase(game::IDLE, 0);
      sndSnore();
      Serial.println("[sleep] face down");
    }
    if (sleepState == DROWSY && now - g.sleepyStart > ZZZ_MS) {
      sleepState = ASLEEP;
      g.sleepyStart = 0;
      M5.Display.setBrightness(0);
    }
    if (sleepState != AWAKE && faceUpSince && now - faceUpSince > 300) {
      sleepState = AWAKE;
      g.sleepyStart = 0;
      M5.Display.setBrightness(BRIGHT);
      g.wakeStart = now;
      g.wakeUntil = now + WAKE_MS;
      lastInput = now;
      dimmed = false;
      sndWake();
      Serial.println("[sleep] woke up");
    }
    if (sleepState == ASLEEP) { M5.update(); delay(50); return; }
  }

  // learn which way is "screen up": when someone presses a button the screen faces them
  if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) {
    auto d = M5.Imu.getImuData();
    if (fabsf(d.accel.z) > 0.7f) { faceUpSign = d.accel.z > 0 ? 1.0f : -1.0f; faceUpKnown = true; }
  }

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

  // listen for blowing only when it can do something and nothing is playing
  int bc, br;
  bool wantMic = g.phase == game::IDLE && sleepState == AWAKE && now >= g.blowUntil &&
                 noteCount == 0 && now - lastSoundAt > 400 && game::findBlock(g, bc, br);
  if (wantMic && !micOn && !M5.Speaker.isPlaying()) useMic();
  if (!wantMic && micOn) useSpeaker();
  g.listening = micOn;
  bool blown = micOn && listenForBlow(now);

  switch (g.phase) {
    case game::IDLE:
      if (blown) { lastInput = now; blowAway(); break; }
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
      useSpeaker();
      if (notes[i].freq) M5.Speaker.tone(notes[i].freq, notes[i].ms, 0, false);
      notes[i] = notes[--noteCount];
    } else {
      ++i;
    }
  }

  scene::draw(&canvas, g, now);
  canvas.pushSprite(&M5.Display, 0, 0);
  if (!micOn) delay(16);   // while listening, the 20 ms mic frame already paces the loop
}
