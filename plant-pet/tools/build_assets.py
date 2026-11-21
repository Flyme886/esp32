#!/usr/bin/env python3
"""Generate firmware assets for plant-pet.

Reads the plant photo and the Khait et al. plant-sound WAV clips in this repo and
writes C headers into ../src/assets/. The generated headers are committed, so you
only need to run this script again if you change the photo, the leaf cut-outs or
the chosen sound clips.

Usage (from the plant-pet folder):
    python3 tools/build_assets.py

Requires: pillow, numpy, scipy
"""
from pathlib import Path
import random
import wave

import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[1]          # plant-pet/
REPO = ROOT.parent                                   # repo root
PHOTO = ROOT / "assets" / "plant_photo.png"
SOUNDS = REPO / "plant-sounds" / "PlantSounds"
OUT = ROOT / "src" / "assets"
PREVIEW = ROOT / "assets" / "preview"

BG = (0xF3, 0xEE, 0xE4)        # screen background, used to blend leaf edges
KEY = 0xF81F                   # transparent colour (magenta) in RGB565
LEAF_SCALE = 0.25              # photo pixels -> screen pixels

# Leaves cut from the photo: (name, crop box in photo, stalk joint in photo)
# The joint is where the black stalk meets the leaf; it becomes the rotation pivot.
LEAVES = [
    ("left",  (130, 145, 318, 350), (297, 317)),
    ("right", (316, 205, 490, 350), (392, 310)),
    ("small", (238, 357, 338, 425), (307, 407)),
]

# Sound clips per category (picked automatically, most click-like first)
CLIPS = [
    ("tomato_dry", "Tomato Dry", 24),
    ("tomato_cut", "Tomato Cut", 16),
    ("tobacco_dry", "Tobacco Dry", 16),
    ("tobacco_cut", "Tobacco Cut", 12),
]


def rgb565(r, g, b):
    v = ((int(r) & 0xF8) << 8) | ((int(g) & 0xFC) << 3) | (int(b) >> 3)
    return KEY ^ 0x0020 if v == KEY else v   # never collide with the key colour


def leaf_mask(rgb):
    a = rgb.astype(float) / 255
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    mx, mn = a.max(-1), a.min(-1)
    s = (mx - mn) / (mx + 1e-6)
    m = (g >= r) & (g > b) & (s > 0.35) & (mx > 0.25)
    m = ndimage.binary_opening(m, iterations=1)
    m = ndimage.binary_fill_holes(m)
    lab, n = ndimage.label(m)
    if n == 0:
        return m
    sizes = ndimage.sum(m, lab, range(1, n + 1))
    return lab == (int(np.argmax(sizes)) + 1)


def tint(rgb, mode):
    """Return a dry / wilted colour variant of a leaf."""
    hsv = np.asarray(Image.fromarray(rgb).convert("HSV")).astype(float)
    if mode == "dry":
        hsv[..., 0] = hsv[..., 0] * 0.80          # towards yellow
        hsv[..., 1] *= 0.80
        hsv[..., 2] *= 0.97
    elif mode == "wilt":
        hsv[..., 0] = hsv[..., 0] * 0.55          # yellow-brown
        hsv[..., 1] *= 0.60
        hsv[..., 2] *= 0.85
    hsv = np.clip(hsv, 0, 255).astype(np.uint8)
    return np.asarray(Image.fromarray(hsv, "HSV").convert("RGB"))


def build_leaves():
    photo = Image.open(PHOTO).convert("RGB")
    lines = ["#pragma once", "#include <stdint.h>", "",
             "// Leaves cut from assets/plant_photo.png by tools/build_assets.py.",
             f"// RGB565, transparent colour = 0x{KEY:04X}.", "",
             f"#define LEAF_KEY 0x{KEY:04X}", ""]
    meta = []
    PREVIEW.mkdir(parents=True, exist_ok=True)
    for name, box, joint in LEAVES:
        crop = photo.crop(box)
        m = leaf_mask(np.asarray(crop))
        alpha = ndimage.gaussian_filter(m.astype(float), 1.2)
        w = max(1, round(crop.width * LEAF_SCALE))
        h = max(1, round(crop.height * LEAF_SCALE))
        px = round((joint[0] - box[0]) * LEAF_SCALE)
        py = round((joint[1] - box[1]) * LEAF_SCALE)
        a_small = np.asarray(Image.fromarray((alpha * 255).astype(np.uint8)).resize((w, h), Image.LANCZOS)) / 255.0
        for mode in ("normal", "dry", "wilt"):
            rgb = np.asarray(crop) if mode == "normal" else tint(np.asarray(crop), mode)
            small = np.asarray(Image.fromarray(rgb).resize((w, h), Image.LANCZOS)).astype(float)
            out, prev = [], np.zeros((h, w, 4), np.uint8)
            for y in range(h):
                for x in range(w):
                    a = a_small[y, x]
                    if a < 0.45:
                        out.append(KEY)
                        continue
                    k = min(1.0, (a - 0.45) / 0.4)
                    c = small[y, x] * k + np.array(BG) * (1 - k)
                    out.append(rgb565(*c))
                    prev[y, x] = (*c.astype(np.uint8), 255)
            Image.fromarray(prev, "RGBA").resize((w * 4, h * 4), Image.NEAREST).save(PREVIEW / f"leaf_{name}_{mode}.png")
            lines.append(f"static const uint16_t leaf_{name}_{mode}[{w * h}] = {{")
            for i in range(0, len(out), 16):
                lines.append("  " + ", ".join(f"0x{v:04X}" for v in out[i:i + 16]) + ",")
            lines.append("};")
        meta.append((name, w, h, px, py))
    lines.append("")
    for name, w, h, px, py in meta:
        lines.append(f"#define LEAF_{name.upper()}_W {w}")
        lines.append(f"#define LEAF_{name.upper()}_H {h}")
        lines.append(f"#define LEAF_{name.upper()}_PX {px}   // pivot (stalk joint)")
        lines.append(f"#define LEAF_{name.upper()}_PY {py}")
    (OUT / "leaves.h").write_text("\n".join(lines) + "\n")
    print("leaves:", meta)


def read_wav(path):
    with wave.open(str(path)) as w:
        x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float)
        return x, w.getframerate()


def build_sounds():
    lines = ["#pragma once", "#include <stdint.h>", "",
             "// Real plant ultrasonic clicks from Khait et al., Cell 2023 (Dryad, CC0).",
             "// Each clip was recorded at 500 kHz and is 2 ms long. The firmware plays the",
             "// same samples at 22.05 kHz (time expansion, about 23x slower), so a ~50 kHz",
             "// click becomes an audible ~2 kHz tick of about 45 ms. Samples are",
             "// peak-normalised with a short fade in/out; nothing else is changed.", "",
             "struct PlantClip { const int16_t* data; uint16_t len; };", ""]
    random.seed(7)
    for var, folder, n in CLIPS:
        files = sorted((SOUNDS / folder).glob("*.wav"))
        scored = []
        for f in files:
            x, sr = read_wav(f)
            if sr != 500000 or len(x) < 500:
                continue
            x = x - x.mean()
            peak = np.abs(x).max()
            rms = np.sqrt((x ** 2).mean()) + 1e-9
            if peak < 3000 or peak > 32000:        # skip very quiet and clipped clips
                continue
            scored.append((peak / rms, f, x))
        scored.sort(key=lambda t: -t[0])
        chosen = scored[:n]
        names = []
        for i, (_, f, x) in enumerate(chosen):
            x = x / np.abs(x).max() * 28000
            fade = 24
            ramp = np.linspace(0, 1, fade)
            x[:fade] *= ramp
            x[-fade:] *= ramp[::-1]
            arr = np.clip(np.round(x), -32768, 32767).astype(int)
            cname = f"{var}_{i}"
            names.append(cname)
            lines.append(f"// {folder}/{f.name}")
            lines.append(f"static const int16_t {cname}[{len(arr)}] = {{")
            for j in range(0, len(arr), 20):
                lines.append("  " + ", ".join(str(v) for v in arr[j:j + 20]) + ",")
            lines.append("};")
        lines.append(f"static const PlantClip {var}[] = {{")
        for c in names:
            lines.append(f"  {{ {c}, (uint16_t)(sizeof({c}) / sizeof(int16_t)) }},")
        lines.append("};")
        lines.append(f"static const int {var}_count = {len(names)};")
        lines.append("")
        print(var, len(names), "clips")
    (OUT / "plant_sounds.h").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    build_leaves()
    build_sounds()
