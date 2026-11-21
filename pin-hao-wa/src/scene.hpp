// Drawing for 拼好蛙 (135 x 240 portrait). No Arduino calls, so it can also be rendered on a PC.
#pragma once

#include <M5GFX.h>
#include <math.h>
#include "game.hpp"

// Your own pictures (made by tools/build_assets.py, kept out of git) or drawn placeholders.
#if __has_include("assets/sprites_user.h")
#include "assets/sprites_user.h"
#define HAS_USER_SPRITES 1
#else
#define HAS_USER_SPRITES 0
#define WA_ICON 40
#define WA_BIG 120
#define WA_KEY 0xF81F
#endif

namespace scene {

static constexpr int W = 135, H = 240;
static inline uint16_t C(uint32_t rgb) { return lgfx::color565(rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF); }
static const uint32_t BG = 0xFFF4D6, LANE = 0xFBE7B5, CARD = 0xFFFFFF, INK = 0x6B4A1E, INK_SOFT = 0xA8885A;
static const uint32_t ACCENT = 0xF2A93B, BLUE = 0x5A6FB8;

static LGFX_Sprite* gIcon[3];
static LGFX_Sprite* gBig;

// ---- placeholder art (plain numbered faces, used only when your pictures were not converted) ----
static void drawPlaceholder(LGFX_Sprite* s, int tier, int size) {
  s->fillScreen(WA_KEY);   // no anti-aliasing here: soft edges would blend with the key colour
  static const uint32_t col[3] = {0x8CCB8A, 0xF6B26B, 0xF28DA6};
  int c = size / 2, r = size * 9 / 20;
  s->fillCircle(c, c, r, C(col[tier - 1]));
  int e = size / 10;
  if (tier < 3) {
    s->fillCircle(c - size / 6, c - size / 10, e, C(0x3A2A1A));
    s->fillCircle(c + size / 6, c - size / 10, e, C(0x3A2A1A));
    s->drawArc(c, c + size / 20, size / 7, size / 7 - 2, 30, 150, C(0x3A2A1A));
  } else {
    s->drawArc(c - size / 6, c - size / 8, e + 1, e - 1, 200, 340, C(0x3A2A1A));
    s->drawArc(c + size / 6, c - size / 8, e + 1, e - 1, 200, 340, C(0x3A2A1A));
    s->fillArc(c, c + size / 20, size / 5, 0, 0, 180, C(0x3A2A1A));
  }
  if (size > 60) s->setFont(&fonts::Font4); else s->setFont(&fonts::Font0);
  s->setTextColor(C(0xFFFFFF));
  s->setTextDatum(textdatum_t::bottom_center);
  char n[2] = {(char)('0' + tier), 0};
  s->drawString(n, c, size - 1);
}

inline void init(LovyanGFX* parent) {
  for (int i = 0; i < 3; ++i) {
    auto* s = new LGFX_Sprite(parent);
    s->setColorDepth(16);
    s->createSprite(WA_ICON, WA_ICON);
#if HAS_USER_SPRITES
    s->pushImage(0, 0, WA_ICON, WA_ICON, (const lgfx::rgb565_t*)wa_icons[i]);
#else
    drawPlaceholder(s, i + 1, WA_ICON);
#endif
    s->setPivot(WA_ICON / 2.0f, WA_ICON / 2.0f);
    gIcon[i] = s;
  }
  gBig = new LGFX_Sprite(parent);
  gBig->setColorDepth(16);
  gBig->setPsram(true);
  gBig->createSprite(WA_BIG, WA_BIG);
#if HAS_USER_SPRITES
  gBig->pushImage(0, 0, WA_BIG, WA_BIG, (const lgfx::rgb565_t*)wa_big_3);
#else
  drawPlaceholder(gBig, 3, WA_BIG);
#endif
  gBig->setPivot(WA_BIG / 2.0f, WA_BIG / 2.0f);
}

// ---- layout ----
inline int colX(int c) { return 23 + c * 44; }
inline int rowY(int r) { return 214 - r * 43; }
static constexpr int NEXT_Y = 42;

static void pill(LovyanGFX* d, int x, int y, int w, int h, uint32_t bg, uint32_t fg, const char* text) {
  d->fillSmoothRoundRect(x, y, w, h, h / 2, C(bg));
  d->setTextColor(C(fg));
  d->setTextDatum(textdatum_t::middle_center);
  d->drawString(text, x + w / 2, y + h / 2 + 1);
}

static void icon(LovyanGFX* d, int tier, float x, float y, float zoom = 1.0f, float angle = 0) {
  if (tier < 1 || tier > 3) return;
  gIcon[tier - 1]->pushRotateZoom(d, x, y, angle, zoom, zoom, (uint16_t)WA_KEY);
}

static float easeOutBack(float t) { const float c1 = 1.70158f, c3 = c1 + 1; float u = t - 1; return 1 + c3 * u * u * u + c1 * u * u; }

inline void draw(LovyanGFX* d, const game::State& g, uint32_t now) {
  d->fillScreen(C(BG));
  d->setFont(&fonts::efontCN_12);
  char buf[32];

  // celebration takes the whole screen
  if (g.phase == game::CELEBRATE) {
    float t = (now - g.phaseStart) / 1000.0f;
    // confetti
    for (int i = 0; i < 26; ++i) {
      float x = fmodf(i * 37.0f + t * (20 + i % 5 * 6), (float)W);
      float y = fmodf(i * 53.0f + t * (60 + i % 7 * 12), (float)H);
      static const uint32_t cols[] = {0xF2A93B, 0x5A6FB8, 0xF08A8A, 0x7CC6A8, 0xF7CB3E};
      d->fillSmoothRoundRect((int)x, (int)y, 4, 6, 2, C(cols[i % 5]));
    }
    pill(d, 22, 12, 91, 22, ACCENT, 0xFFFFFF, "拼好蛙!");
    float bounce = 1.0f + 0.06f * sinf(t * 9.0f);
    float zin = t < 0.35f ? easeOutBack(t / 0.35f) : 1.0f;
    gBig->pushRotateZoom(d, W / 2.0f, 118, sinf(t * 7.0f) * 4.0f, zin * bounce, zin * bounce, (uint16_t)WA_KEY);
    snprintf(buf, sizeof(buf), "+10   得分 %lu", (unsigned long)g.score);
    pill(d, 12, 186, 111, 20, CARD, INK, buf);
    d->setTextColor(C(INK_SOFT));
    d->setTextDatum(textdatum_t::middle_center);
    if (t > 0.8f) d->drawString("按任意键继续", W / 2, 224);
    return;
  }

  // header
  snprintf(buf, sizeof(buf), "得分 %lu", (unsigned long)g.score);
  pill(d, 4, 3, 62, 17, CARD, INK, buf);
  snprintf(buf, sizeof(buf), "最高 %lu", (unsigned long)g.best);
  pill(d, W - 66, 3, 62, 17, CARD, INK_SOFT, buf);

  // lanes
  for (int c = 0; c < game::COLS; ++c) {
    uint32_t lane = (c == g.cursor && g.phase != game::OVER) ? 0xF9DC98 : LANE;
    d->fillSmoothRoundRect(colX(c) - 21, rowY(game::ROWS - 1) - 22, 42, 43 * game::ROWS + 1, 12, C(lane));
  }

  // babies on the board
  for (int c = 0; c < game::COLS; ++c) {
    for (int r = 0; r < game::ROWS; ++r) {
      int t = g.grid[c][r];
      if (!t) continue;
      float z = 1.0f;
      if (g.phase == game::POP && c == g.popCol && r == g.popRow) {
        float p = (now - g.phaseStart) / (float)g.phaseLen;
        z = 0.45f + 0.55f * easeOutBack(p > 1 ? 1 : p);
      }
      icon(d, t, colX(c), rowY(r), z * 0.95f, 0);
    }
  }

  // sparkles around a merge
  if (g.phase == game::POP) {
    float p = (now - g.phaseStart) / (float)g.phaseLen;
    int cx = colX(g.popCol), cy = rowY(g.popRow);
    for (int i = 0; i < 8; ++i) {
      float a = i * 0.785f, r = 12 + p * 18;
      int rr = 3 - (int)(p * 2.5f);
      if (rr > 0) d->fillSmoothCircle(cx + (int)(cosf(a) * r), cy + (int)(sinf(a) * r), rr, C(i % 2 ? ACCENT : 0xF08A8A));
    }
  }

  // falling baby
  if (g.phase == game::DROP) {
    float p = (now - g.phaseStart) / (float)g.phaseLen;
    if (p > 1) p = 1;
    float y = NEXT_Y + (rowY(g.dropRow) - NEXT_Y) * p * p;
    icon(d, g.dropTier, colX(g.dropCol), y, 0.95f, 0);
  }

  // next baby waiting above the cursor, with a small bouncing arrow
  if (g.phase == game::IDLE || g.phase == game::POP) {
    float bob = sinf(now / 180.0f) * 2.0f;
    icon(d, g.next, colX(g.cursor), NEXT_Y + bob, 0.95f, sinf(now / 300.0f) * 6.0f);
    int ax = colX(g.cursor), ay = 63 + (int)bob;
    d->fillTriangle(ax - 5, ay, ax + 5, ay, ax, ay + 5, C(ACCENT));
  }

  // floating +N
  if (now < g.gainUntil) {
    float p = 1.0f - (g.gainUntil - now) / 700.0f;
    snprintf(buf, sizeof(buf), "+%d", g.gain);
    d->setTextColor(C(0xE07B1F));
    d->setTextDatum(textdatum_t::middle_center);
    d->drawString(buf, colX(g.gainCol), rowY(g.gainRow) - 24 - (int)(p * 14));
  }

  // "轻松绷住" toast with a little shake
  if (now < g.toastUntil) {
    float age = (now - g.toastStart) / 1000.0f;
    int dx = age < 0.4f ? (int)(sinf(age * 60.0f) * 4 * (1 - age / 0.4f)) : 0;
    pill(d, 18 + dx, 118, 99, 26, 0x6B4A1E, 0xFFFFFF, "轻松绷住");
  }

  if (g.phase == game::OVER) {
    d->fillSmoothRoundRect(10, 82, W - 20, 92, 16, C(CARD));
    d->setTextDatum(textdatum_t::middle_center);
    d->setTextColor(C(INK));
    d->setFont(&fonts::efontCN_16);
    d->drawString("绷不住了", W / 2, 104);
    d->setFont(&fonts::efontCN_12);
    snprintf(buf, sizeof(buf), "得分 %lu", (unsigned long)g.score);
    d->drawString(buf, W / 2, 130);
    d->setTextColor(C(INK_SOFT));
    d->drawString("按正面键再来", W / 2, 154);
  }
}

}  // namespace scene
