// Drawing of the plant pet screen (135 x 240, portrait).
// Kept free of Arduino calls so the same code can be rendered on a PC for previews.
#pragma once

#include <M5GFX.h>
#include <math.h>
#include "assets/leaves.h"

namespace scene {

static constexpr int W = 135;
static constexpr int H = 240;

// ---- palette (soft, rounded, low saturation) ----
static inline uint16_t C(uint32_t rgb) { return lgfx::color565(rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF); }
static const uint32_t BG       = 0xF3EEE4;
static const uint32_t CARD     = 0xFFFFFF;
static const uint32_t INK      = 0x5A4A3A;
static const uint32_t INK_SOFT = 0x9A8A76;
static const uint32_t POT      = 0xC8DE8C;
static const uint32_t POT_RIM  = 0xD3E59C;
static const uint32_t SOIL     = 0x8A7A62;
static const uint32_t STEM     = 0x3E2E26;
static const uint32_t WATER    = 0x8FD3C1;
static const uint32_t WATER_LO = 0xF6B26B;
static const uint32_t VIGOR    = 0xB9DB7E;
static const uint32_t BAR_BG   = 0xE6DFD2;
static const uint32_t BLUSH    = 0xF4A6B8;
static const uint32_t CORAL    = 0xC0583A;

struct State {
  float water;      // 0..100
  float health;     // 0..100
  float growth;     // 0..100, stage = growth / 20
  float day;        // plant days since start
  uint32_t clicks;  // clicks since last watering
  uint8_t soundSet; // 0 = tomato recordings, 1 = tobacco recordings
};

enum Bubble : uint8_t { BUBBLE_NONE, BUBBLE_DRY, BUBBLE_CUT, BUBBLE_WATER, BUBBLE_INFO };

struct Fx {
  uint32_t now;            // ms
  uint32_t bubbleUntil;
  Bubble bubble;
  const char* infoText;    // for BUBBLE_INFO
  uint32_t jitterUntil;    // leaves shiver after a click
  uint32_t hitUntil;       // after a knock
  uint32_t waterUntil;     // droplets after watering
  const int16_t* lastClip; // waveform shown at the bottom
  uint16_t lastLen;
};

static const char* kStageNames[5] = {"卷芽", "展叶", "两扇", "三扇", "成丛"};

// ---- leaf sprites (3 leaves x 3 colour variants) ----
struct LeafDef { const uint16_t* img[3]; int w, h, px, py; };
static const LeafDef kLeafDefs[3] = {
  {{leaf_left_normal,  leaf_left_dry,  leaf_left_wilt},  LEAF_LEFT_W,  LEAF_LEFT_H,  LEAF_LEFT_PX,  LEAF_LEFT_PY},
  {{leaf_right_normal, leaf_right_dry, leaf_right_wilt}, LEAF_RIGHT_W, LEAF_RIGHT_H, LEAF_RIGHT_PX, LEAF_RIGHT_PY},
  {{leaf_small_normal, leaf_small_dry, leaf_small_wilt}, LEAF_SMALL_W, LEAF_SMALL_H, LEAF_SMALL_PX, LEAF_SMALL_PY},
};
static LGFX_Sprite* gLeaf[3][3];

inline void initLeaves(LovyanGFX* parent) {
  for (int i = 0; i < 3; ++i) {
    for (int v = 0; v < 3; ++v) {
      auto* s = new LGFX_Sprite(parent);
      s->setColorDepth(16);
      s->createSprite(kLeafDefs[i].w, kLeafDefs[i].h);
      s->pushImage(0, 0, kLeafDefs[i].w, kLeafDefs[i].h, (const lgfx::rgb565_t*)kLeafDefs[i].img[v]);
      s->setPivot(kLeafDefs[i].px, kLeafDefs[i].py);
      gLeaf[i][v] = s;
    }
  }
}

inline int stageOf(const State& s) { int st = (int)(s.growth / 20.0f); return st < 0 ? 0 : (st > 4 ? 4 : st); }

// ---- small drawing helpers ----
static void pill(LovyanGFX* d, int x, int y, int w, int h, uint32_t bg, uint32_t fg, const char* text) {
  d->fillSmoothRoundRect(x, y, w, h, h / 2, C(bg));
  d->setTextColor(C(fg));
  d->setTextDatum(textdatum_t::middle_center);
  d->drawString(text, x + w / 2, y + h / 2 + 1);
}

static void stem(LovyanGFX* d, float x0, float y0, float cx, float cy, float x1, float y1) {
  float px = x0, py = y0;
  for (int i = 1; i <= 12; ++i) {
    float t = i / 12.0f, u = 1 - t;
    float x = u * u * x0 + 2 * u * t * cx + t * t * x1;
    float y = u * u * y0 + 2 * u * t * cy + t * t * y1;
    d->drawWideLine((int)px, (int)py, (int)x, (int)y, 0.8f, C(STEM));
    px = x; py = y;
  }
}

static void bar(LovyanGFX* d, int y, const char* label, float v, uint32_t col) {
  d->setTextColor(C(INK));
  d->setTextDatum(textdatum_t::middle_left);
  d->drawString(label, 8, y + 4);
  int bx = 36, bw = 66;
  d->fillSmoothRoundRect(bx, y, bw, 9, 4, C(BAR_BG));
  int fw = (int)(bw * (v < 0 ? 0 : (v > 100 ? 100 : v)) / 100.0f);
  if (fw >= 9) d->fillSmoothRoundRect(bx, y, fw, 9, 4, C(col));
  else if (fw > 0) d->fillSmoothCircle(bx + 4, y + 4, 4, C(col));
  char buf[8]; snprintf(buf, sizeof(buf), "%d%%", (int)(v + 0.5f));
  d->setTextDatum(textdatum_t::middle_right);
  d->drawString(buf, W - 6, y + 4);
}

static void bubbleAt(LovyanGFX* d, int x, int y, const char* text, uint32_t fg) {
  int w = d->textWidth(text) + 16, h = 20;
  d->fillSmoothRoundRect(x - w / 2, y - h / 2, w, h, 10, C(CARD));
  d->fillTriangle(x - 10, y + h / 2 - 2, x - 3, y + h / 2 - 2, x - 12, y + h / 2 + 5, C(CARD));
  d->setTextColor(C(fg));
  d->setTextDatum(textdatum_t::middle_center);
  d->drawString(text, x, y + 1);
}

// ---- the pot (Q style) ----
static void drawPot(LovyanGFX* d, int cx, int top, bool sad, bool closedEyes, bool hurt) {
  // shadow and feet
  d->fillEllipse(cx, top + 53, 34, 4, C(0xE2DACB));
  for (int fx : {cx - 22, cx, cx + 22}) {
    d->fillTriangle(fx - 6, top + 44, fx + 6, top + 44, fx, top + 56, C(0xFFFFFF));
  }
  // round body, rim, soil, pebbles
  d->fillEllipse(cx, top + 24, 40, 29, C(POT));
  d->fillEllipse(cx, top + 1, 30, 8, C(POT_RIM));
  d->fillEllipse(cx, top + 1, 25, 6, C(SOIL));
  static const int8_t peb[][2] = {{-14, 2}, {-8, 4}, {-2, 2}, {5, 4}, {11, 2}, {16, 3}, {-18, 3}, {0, 0}, {9, 0}, {-10, 0}};
  for (auto& p : peb) d->fillSmoothCircle(cx + p[0], top + p[1], 2, C(0xFFFFFF));
  // speckles
  static const int8_t sp[][2] = {{-28, 16}, {-20, 34}, {24, 20}, {30, 34}, {12, 42}, {-31, 28}, {20, 12}, {-8, 44}, {-14, 14}};
  for (auto& p : sp) d->fillCircle(cx + p[0], top + p[1], 1, C(0x8A6A4A));
  // flower stamp
  d->drawCircle(cx, top + 40, 4, C(0xAFC873));
  for (int i = 0; i < 5; ++i) {
    float a = i * 1.2566f;
    d->drawCircle(cx + (int)(cosf(a) * 2.5f), top + 40 + (int)(sinf(a) * 2.5f), 1, C(0xAFC873));
  }
  // face
  int ey = top + 22;
  if (closedEyes) {
    d->drawArc(cx - 11, ey + 2, 4, 3, 200, 340, C(INK));
    d->drawArc(cx + 11, ey + 2, 4, 3, 200, 340, C(INK));
  } else {
    d->fillSmoothCircle(cx - 11, ey, 3, C(INK));
    d->fillSmoothCircle(cx + 11, ey, 3, C(INK));
    d->fillCircle(cx - 10, ey - 1, 1, C(0xFFFFFF));
    d->fillCircle(cx + 12, ey - 1, 1, C(0xFFFFFF));
  }
  if (sad) d->drawArc(cx, ey + 12, 5, 4, 210, 330, C(INK));
  else     d->drawArc(cx, ey + 4, 5, 4, 30, 150, C(INK));
  d->fillEllipse(cx - 19, ey + 6, 4, 2, C(BLUSH));
  d->fillEllipse(cx + 19, ey + 6, 4, 2, C(BLUSH));
  if (hurt) {  // band-aid
    d->fillSmoothRoundRect(cx + 16, top + 10, 16, 7, 3, C(0xF7D9B9));
    d->fillRect(cx + 22, top + 10, 4, 7, C(0xEFC79E));
  }
}

// ---- whole frame ----
inline void draw(LovyanGFX* d, const State& s, const Fx& fx) {
  const float t = fx.now / 1000.0f;
  const int stage = stageOf(s);
  const bool dry = s.water < 30, wilt = s.water < 8, hurt = s.health < 60;
  const int variant = wilt ? 2 : (dry ? 1 : 0);
  const float droop = wilt ? 28.0f : (dry ? 12.0f : 0.0f);
  float shiver = 0;
  if (fx.now < fx.jitterUntil) shiver = sinf(fx.now * 0.09f) * 3.0f;
  if (fx.now < fx.hitUntil) shiver += sinf(fx.now * 0.12f) * 7.0f * (fx.hitUntil - fx.now) / 600.0f;
  const float sway = sinf(t * 1.3f) * (wilt ? 0.6f : 1.8f) + shiver;

  d->fillScreen(C(BG));
  d->setFont(&fonts::efontCN_12);

  // header pills
  pill(d, 5, 4, 50, 17, CARD, 0x7A6A58, "小扇子");
  char buf[24];
  snprintf(buf, sizeof(buf), "第 %d 天", (int)s.day + 1);
  pill(d, W - 59, 4, 54, 17, CARD, 0x7A6A58, buf);

  // plant: pot base at (cx, potTop); stems start in the soil
  const int cx = 67, potTop = 118;
  const float bx = cx - 1, by = potTop + 2;
  const float zoom = stage <= 1 ? 0.85f : (stage == 2 ? 1.0f : 1.15f);

  // leaf layout from the photo (relative to stem base), drawn back to front
  struct L { int idx; float dx, dy, ang, side; int minStage; float z; };
  const L leaves[] = {
    {2, -9, -22, -4,  -1, 1, 1.0f},   // small leaf
    {1, 15, -45, 6,   1,  2, 1.0f},   // right fan
    {0, -12, -38, -6, -1, 3, 1.0f},   // left fan (the big one)
    {2, 14, -20, 28,  1,  4, 0.9f},   // extra small leaf when fully grown
  };

  if (stage == 0) {  // fiddlehead
    stem(d, bx, by, bx - 3, by - 20, bx + 1, by - 34);
    d->drawArc((int)bx + 8, (int)by - 36, 8, 5, 90, 380, C(0x8DBB4A));
    d->drawArc((int)bx + 8, (int)by - 36, 3, 2, 0, 300, C(0x8DBB4A));
    d->fillSmoothCircle((int)bx + 8, (int)by - 36, 2, C(0x8DBB4A));
  }
  for (const L& l : leaves) {
    if (stage < l.minStage) continue;
    float a = l.ang + l.side * droop * (l.idx == 0 ? 0.55f : 1.0f) + sway * (l.idx == 2 ? 1.3f : 1.0f);
    float drop = droop * 0.5f;
    float tx = bx + l.dx * zoom + l.side * droop * 0.15f;
    float ty = by + l.dy * zoom + drop;
    stem(d, bx, by, bx + l.dx * 0.25f, by + l.dy * 0.55f, tx, ty);
    gLeaf[l.idx][variant]->pushRotateZoom(d, tx, ty, a, zoom * l.z, zoom * l.z, (uint16_t)LEAF_KEY);
  }

  drawPot(d, cx, potTop, dry || hurt, wilt, hurt);

  // sweat drop when thirsty, droplets after watering
  if (dry && !wilt) {
    d->fillSmoothCircle(cx + 42, potTop - 4, 3, C(0x9CCFE8));
    d->fillTriangle(cx + 39, potTop - 5, cx + 45, potTop - 5, cx + 42, potTop - 11, C(0x9CCFE8));
  }
  if (fx.now < fx.waterUntil) {
    float k = (fx.waterUntil - fx.now) / 900.0f;
    for (int i = 0; i < 5; ++i) {
      int x = cx - 20 + i * 10, y = (int)(potTop - 40 + (1 - k) * 36 + (i % 2) * 6);
      d->fillSmoothCircle(x, y, 2, C(0x8FC9E8));
    }
  }

  // status pill
  const char* mood = wilt ? "蔫了" : (dry ? "口渴" : (hurt ? "受伤" : "精神"));
  snprintf(buf, sizeof(buf), "%s · %s", kStageNames[stage], mood);
  uint32_t pbg = (wilt || dry) ? 0xFBE3C8 : (hurt ? 0xF9D9DE : 0xE3EFC8);
  uint32_t pfg = (wilt || dry) ? 0x8A5A1E : (hurt ? 0x8C3A4A : 0x4F6B1F);
  pill(d, cx - 36, 176, 72, 17, pbg, pfg, buf);

  // bars
  bar(d, 196, "水分", s.water, s.water < 30 ? WATER_LO : WATER);
  bar(d, 210, "元气", s.health, VIGOR);

  // click counter + last waveform
  d->fillSmoothRoundRect(5, 223, W - 10, 16, 8, C(CARD));
  d->setTextColor(C(INK_SOFT));
  d->setTextDatum(textdatum_t::middle_left);
  snprintf(buf, sizeof(buf), "咔哒 %lu", (unsigned long)s.clicks);
  d->drawString(buf, 12, 231);
  if (fx.lastClip && fx.lastLen) {
    int x0 = 66, w = 58, py = 231;
    for (int i = 0; i < w; ++i) {
      int idx = (int)((long)i * fx.lastLen / w);
      int y = 231 - fx.lastClip[idx] * 7 / 28000;
      if (i) d->drawLine(x0 + i - 1, py, x0 + i, y, C(0xE8907A));
      py = y;
    }
  } else {
    d->setTextDatum(textdatum_t::middle_right);
    d->drawString(s.soundSet ? "烟草声" : "番茄声", W - 12, 231);
  }

  // speech bubble
  if (fx.now < fx.bubbleUntil) {
    switch (fx.bubble) {
      case BUBBLE_DRY:   bubbleAt(d, 94, 36, "咔哒 渴了", CORAL); break;
      case BUBBLE_CUT:   bubbleAt(d, 94, 36, "咔哒! 好痛", CORAL); break;
      case BUBBLE_WATER: bubbleAt(d, 94, 36, "咕嘟咕嘟", 0x2F7F6A); break;
      case BUBBLE_INFO:  bubbleAt(d, 67, 36, fx.infoText ? fx.infoText : "", INK); break;
      default: break;
    }
  }
}

}  // namespace scene
