// Plant Pet - a pocket plant that clicks like a real stressed plant.
// Board: M5Stack StickS3. Sounds: real plant ultrasonic clicks (Khait et al., Cell 2023),
// time-expanded so you can hear them.
//
// Minimal demo controls
//   Front button (BtnA)       click : water the plant
//   Side button  (BtnB)       click : fast-forward half a day
//   Shake / knock the stick         : the plant gets hurt (real "cut" clicks)
//   Side button  hold 1.5 s         : switch tomato / tobacco recordings
//
// Every click first shows a short build-up animation (trembling leaves and
// ripples spreading from the stems), then the real click sound plays.
// When the plant gets thirsty it chimes a "time to water" reminder.
//   Front button hold 3 s           : start over

#include <M5Unified.h>
#include <Preferences.h>
#include "scene.hpp"
#include "assets/plant_sounds.h"

// ---------- tuning ----------
static constexpr uint32_t DAY_MS          = 60000;   // demo: one plant day per real minute
static constexpr float    WATER_PER_DAY   = 20.0f;   // % lost per day
static constexpr float    GROWTH_PER_DAY  = 18.0f;   // growth while watered (stage every 20)
static constexpr float    PEAK_CLICKS_SEC = 0.30f;   // click rate at the worst of thirst (demo speed)
static constexpr float    HIT_G           = 2.3f;    // acceleration that counts as a knock
static constexpr uint32_t PLAYBACK_RATE   = 22050;   // 500 kHz recordings played ~23x slower
static constexpr uint8_t  VOLUME          = 170;     // keep below ~75 % on battery
static constexpr uint32_t PRIME_MS        = 450;     // build-up animation before a thirst click
static constexpr uint32_t PRIME_CUT_MS    = 200;     // shorter build-up after a knock
static constexpr uint32_t REMIND_MS       = 30000;   // repeat the water reminder while thirsty
static constexpr uint32_t REMIND_URGENT_MS= 15000;   // ... and more often while wilting

static M5Canvas canvas(&M5.Display);
static Preferences prefs;
static scene::State st;
static scene::Fx fx;

// pending clicks: each one first runs its build-up animation, then plays
struct Pending { uint32_t at; bool cut; bool primed; };
static Pending pending[16];
static int pendingCount = 0;

// short melodies (water reminder, watering done)
struct Note { uint32_t at; uint16_t freq; uint16_t ms; };
static Note notes[12];
static int noteCount = 0;

static uint32_t lastTick = 0, lastSave = 0, lastClickAt = 0, hitCooldownUntil = 0, bootAt = 0;
static uint32_t nextRemindAt = 0;
static bool wasThirsty = false;
static bool resetArmed = true, switchArmed = true;

static void resetState() {
  st.water = 100; st.health = 100; st.growth = 60; st.day = 0; st.clicks = 0;
}

static void save() {
  prefs.putFloat("water", st.water);
  prefs.putFloat("health", st.health);
  prefs.putFloat("growth", st.growth);
  prefs.putFloat("day", st.day);
  prefs.putUInt("clicks", st.clicks);
  prefs.putUChar("sound", st.soundSet);
}

static void load() {
  resetState();
  st.soundSet = 0;
  if (prefs.isKey("water")) {
    st.water = prefs.getFloat("water", 100);
    st.health = prefs.getFloat("health", 100);
    st.growth = prefs.getFloat("growth", 60);
    st.day = prefs.getFloat("day", 0);
    st.clicks = prefs.getUInt("clicks", 0);
    st.soundSet = prefs.getUChar("sound", 0);
  }
}

// how many clicks the thirst level produces (rises, then falls when the plant dries out,
// following the hump-shaped pattern in the paper)
static float thirstHump(float water) {
  if (water >= 30) return 0;
  if (water >= 12) return 0.2f + 0.8f * (30 - water) / 18.0f;
  return fmaxf(0.15f, water / 12.0f);
}

static void playClick(bool cut) {
  const PlantClip* set;
  int n;
  if (st.soundSet == 0) { set = cut ? tomato_cut : tomato_dry;  n = cut ? tomato_cut_count : tomato_dry_count; }
  else                  { set = cut ? tobacco_cut : tobacco_dry; n = cut ? tobacco_cut_count : tobacco_dry_count; }
  const PlantClip& c = set[random(n)];
  M5.Speaker.playRaw(c.data, c.len, PLAYBACK_RATE, false, 1, -1, false);

  uint32_t now = millis();
  st.clicks++;
  fx.lastClip = c.data;
  fx.lastLen = c.len;
  fx.bubble = cut ? scene::BUBBLE_CUT : scene::BUBBLE_DRY;
  fx.bubbleUntil = now + 700;
  fx.jitterUntil = now + 200;
  lastClickAt = now;
  Serial.printf("[click] %s %s\n", cut ? "cut" : "dry", st.soundSet ? "tobacco" : "tomato");
}

static void queueClick(uint32_t delayMs, bool cut) {
  if (pendingCount < (int)(sizeof(pending) / sizeof(pending[0]))) {
    pending[pendingCount++] = {millis() + delayMs, cut, false};
  }
}

static void playMelody(const uint16_t (*m)[2], int n, uint32_t startDelay = 0) {
  uint32_t t = millis() + startDelay;
  for (int i = 0; i < n && noteCount < (int)(sizeof(notes) / sizeof(notes[0])); ++i) {
    notes[noteCount++] = {t, m[i][0], m[i][1]};
    t += m[i][1] + 25;
  }
}

// "time to water": a soft ding-dong; when wilting, a sadder falling three notes
static const uint16_t kRemind[][2]       = {{1318, 130}, {1046, 200}};
static const uint16_t kRemindUrgent[][2] = {{988, 140}, {784, 140}, {659, 240}};
static const uint16_t kWatered[][2]      = {{1046, 70}, {1318, 70}, {1568, 120}};

static void remindWater() {
  bool urgent = st.water < 8;
  if (urgent) playMelody(kRemindUrgent, 3); else playMelody(kRemind, 2);
  uint32_t now = millis();
  fx.remindUrgent = urgent;
  fx.remindUntil = now + 1600;
  fx.bubble = scene::BUBBLE_REMIND;
  fx.bubbleUntil = now + 1600;
  nextRemindAt = now + (urgent ? REMIND_URGENT_MS : REMIND_MS);
  Serial.println(urgent ? "[remind] urgent" : "[remind]");
}

static void info(const char* text) {
  fx.bubble = scene::BUBBLE_INFO;
  fx.infoText = text;
  fx.bubbleUntil = millis() + 1400;
}

static void advance(float days) {
  st.day += days;
  st.water = fmaxf(0, st.water - WATER_PER_DAY * days);
  if (st.water > 40) {
    st.growth = fminf(100, st.growth + GROWTH_PER_DAY * days);
    st.health = fminf(100, st.health + 6 * days);
  }
  if (st.water < 8) st.health = fmaxf(0, st.health - 12 * days);
}

static void waterPlant() {
  st.water = 100;
  st.clicks = 0;
  pendingCount = 0;
  uint32_t now = millis();
  fx.bubble = scene::BUBBLE_WATER;
  fx.bubbleUntil = now + 900;
  fx.waterUntil = now + 900;
  fx.lastClip = nullptr;
  fx.remindUntil = 0;
  fx.primeUntil = 0;
  noteCount = 0;
  playMelody(kWatered, 3);
  wasThirsty = false;
  save();
  Serial.println("[water]");
}

static void knock() {
  uint32_t now = millis();
  st.health = fmaxf(0, st.health - 15);
  fx.hitUntil = now + 600;      // the knock itself is the first animation
  queueClick(0, true);
  queueClick(320, true);
  hitCooldownUntil = now + 1500;
  save();
  Serial.println("[knock]");
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_imu = true;
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
  scene::initLeaves(&canvas);

  prefs.begin("plantpet", false);
  load();
  randomSeed(esp_random());

  bootAt = lastTick = lastSave = millis();
  fx = {};
  info("你好呀");
  Serial.println("Plant Pet ready. Board detected: " + String((int)M5.getBoard()));
}

void loop() {
  M5.update();
  const uint32_t now = millis();

  // ---- buttons ----
  if (M5.BtnA.wasClicked()) waterPlant();
  if (M5.BtnA.pressedFor(3000) && resetArmed) {
    resetArmed = false;
    resetState(); pendingCount = 0; noteCount = 0; wasThirsty = false; save(); info("重新开始");
  }
  if (M5.BtnA.wasReleased()) resetArmed = true;

  if (M5.BtnB.wasClicked()) {
    advance(0.5f);
    int n = (int)roundf(thirstHump(st.water) * 4.0f + random(0, 100) / 100.0f * 0.6f);
    for (int i = 0; i < n; ++i) queueClick(700 + i * 650, false);
    if (n == 0) info("快进半天");
    save();
  }
  if (M5.BtnB.pressedFor(1500) && switchArmed) {
    switchArmed = false;
    st.soundSet ^= 1;
    info(st.soundSet ? "改用烟草声" : "改用番茄声");
    save();
  }
  if (M5.BtnB.wasReleased()) switchArmed = true;

  // ---- knock detection ----
  if (M5.Imu.update()) {
    auto d = M5.Imu.getImuData();
    float g = sqrtf(d.accel.x * d.accel.x + d.accel.y * d.accel.y + d.accel.z * d.accel.z);
    if (g > HIT_G && now > hitCooldownUntil && now - bootAt > 2000) knock();
  }

  // ---- time passes ----
  uint32_t dt = now - lastTick;
  lastTick = now;
  advance((float)dt / DAY_MS);

  // spontaneous thirst clicks
  float rate = PEAK_CLICKS_SEC * thirstHump(st.water);
  if (rate > 0 && pendingCount == 0 && now - lastClickAt > 1200 && random(0, 1000000) < rate * dt * 1000.0f) queueClick(0, false);

  // queued clicks: start the build-up animation, then play the sound when it ends
  for (int i = 0; i < pendingCount;) {
    Pending& p = pending[i];
    if ((int32_t)(now - p.at) < 0 || (!p.primed && now < fx.primeUntil)) { ++i; continue; }
    if (!p.primed) {
      uint32_t len = p.cut ? PRIME_CUT_MS : PRIME_MS;
      fx.primeStart = now;
      fx.primeUntil = now + len;
      fx.primeCut = p.cut;
      p.primed = true;
      p.at = now + len;
      ++i;
    } else {
      playClick(p.cut);
      pending[i] = pending[--pendingCount];
    }
  }

  // water reminder: once when the plant becomes thirsty, then repeated
  bool thirsty = st.water < 30;
  if (thirsty && (!wasThirsty || now >= nextRemindAt)) remindWater();
  wasThirsty = thirsty;

  // melody notes
  for (int i = 0; i < noteCount;) {
    if ((int32_t)(now - notes[i].at) >= 0) {
      M5.Speaker.tone(notes[i].freq, notes[i].ms, -1, false);
      notes[i] = notes[--noteCount];
    } else {
      ++i;
    }
  }

  if (now - lastSave > 15000) { save(); lastSave = now; }

  // ---- draw (~30 fps) ----
  fx.now = now;
  scene::draw(&canvas, st, fx);
  canvas.pushSprite(&M5.Display, 0, 0);
  delay(30);
}
