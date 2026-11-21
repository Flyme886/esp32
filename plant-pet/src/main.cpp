// Plant Pet - a pocket plant that clicks like a real stressed plant.
// Board: M5Stack StickS3. Sounds: real plant ultrasonic clicks (Khait et al., Cell 2023),
// time-expanded so you can hear them.
//
// Minimal demo controls
//   Front button (BtnA)       click : water the plant
//   Side button  (BtnB)       click : fast-forward half a day
//   Shake / knock the stick         : the plant gets hurt (real "cut" clicks)
//   Side button  hold 1.5 s         : switch tomato / tobacco recordings
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

static M5Canvas canvas(&M5.Display);
static Preferences prefs;
static scene::State st;
static scene::Fx fx;

// pending sounds (bursts after fast-forward or a knock)
struct Pending { uint32_t at; bool cut; };
static Pending pending[16];
static int pendingCount = 0;

static uint32_t lastTick = 0, lastSave = 0, lastClickAt = 0, hitCooldownUntil = 0, bootAt = 0;
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
  fx.jitterUntil = now + 260;
  lastClickAt = now;
  Serial.printf("[click] %s %s\n", cut ? "cut" : "dry", st.soundSet ? "tobacco" : "tomato");
}

static void queueClick(uint32_t delayMs, bool cut) {
  if (pendingCount < (int)(sizeof(pending) / sizeof(pending[0]))) {
    pending[pendingCount++] = {millis() + delayMs, cut};
  }
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
  M5.Speaker.tone(1318, 90);
  save();
  Serial.println("[water]");
}

static void knock() {
  uint32_t now = millis();
  st.health = fmaxf(0, st.health - 15);
  fx.hitUntil = now + 600;
  playClick(true);
  queueClick(280, true);
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
    resetState(); pendingCount = 0; save(); info("重新开始");
  }
  if (M5.BtnA.wasReleased()) resetArmed = true;

  if (M5.BtnB.wasClicked()) {
    advance(0.5f);
    int n = (int)roundf(thirstHump(st.water) * 4.0f + random(0, 100) / 100.0f * 0.6f);
    for (int i = 0; i < n; ++i) queueClick(150 + i * 320, false);
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
  if (rate > 0 && now - lastClickAt > 600 && random(0, 1000000) < rate * dt * 1000.0f) playClick(false);

  // queued clicks
  for (int i = 0; i < pendingCount;) {
    if ((int32_t)(now - pending[i].at) >= 0) {
      playClick(pending[i].cut);
      pending[i] = pending[--pendingCount];
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
