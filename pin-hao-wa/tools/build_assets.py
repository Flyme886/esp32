#!/usr/bin/env python3
"""Turn your three character pictures (and an optional laugh sound) into firmware headers.

Put these files in pin-hao-wa/assets/local/ (this folder is git-ignored, so the
pictures stay on your computer and are never pushed to GitHub):

    wa1.png   level 1 (the starting baby)
    wa2.png   level 2
    wa3.png   level 3 (the big laugh)
    laugh.wav                optional, looped while level 3 is on screen
    (or laugh.mp4 / .mov / .mp3: seconds 1-4 are cut out automatically, needs ffmpeg)
                             silence at the start/end is trimmed automatically

Then run, from the pin-hao-wa folder:

    python3 tools/build_assets.py

It writes src/assets/sprites_user.h and src/assets/laugh_user.h (both git-ignored).
Without them the firmware falls back to built-in drawn placeholders and a
synthesised laugh, so the project always builds.

Requires: pillow, numpy, scipy (optional) and ffmpeg (only for mp3 or video input).
"""
from pathlib import Path
import shutil
import subprocess
import sys
import wave

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
LOCAL = ROOT / "assets" / "local"
OUT = ROOT / "src" / "assets"

KEY = 0xF81F                 # transparent colour in RGB565
ICON = 40                    # board tile size (pixels)
BIG = 120                    # celebration picture size
LAUGH_RATE = 16000
LAUGH_MAX_S = 4.0
VIDEO_CUT = (1.0, 4.0)       # for a video or mp3 source: use seconds 1-4 of it


def rgb565(r, g, b):
    v = ((int(r) & 0xF8) << 8) | ((int(g) & 0xFC) << 3) | (int(b) >> 3)
    return KEY ^ 0x0020 if v == KEY else v


def cut_out(path):
    """Remove the plain white background by flood-filling from the borders."""
    im = Image.open(path).convert("RGB")
    a = np.asarray(im).astype(int)
    near_white = (a.min(-1) > 228) & ((a.max(-1) - a.min(-1)) < 24)
    h, w = near_white.shape
    bg = np.zeros_like(near_white)
    stack = [(y, x) for y in range(h) for x in (0, w - 1)] + [(y, x) for x in range(w) for y in (0, h - 1)]
    while stack:
        y, x = stack.pop()
        if bg[y, x] or not near_white[y, x]:
            continue
        bg[y, x] = True
        if y > 0: stack.append((y - 1, x))
        if y < h - 1: stack.append((y + 1, x))
        if x > 0: stack.append((y, x - 1))
        if x < w - 1: stack.append((y, x + 1))
    fg = ~bg
    try:                                   # keep only the biggest blob (drops stray specks)
        from scipy import ndimage
        lab, n = ndimage.label(fg)
        if n > 1:
            sizes = ndimage.sum(fg, lab, range(1, n + 1))
            fg = lab == (int(np.argmax(sizes)) + 1)
    except ImportError:
        pass
    alpha = fg.astype(np.uint8) * 255
    rgba = np.dstack([np.asarray(im), alpha])
    ys, xs = np.where(alpha > 0)
    return Image.fromarray(rgba, "RGBA").crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))


def fit(img, size):
    s = size / max(img.width, img.height)
    small = img.resize((max(1, round(img.width * s)), max(1, round(img.height * s))), Image.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.paste(small, ((size - small.width) // 2, size - small.height), small)
    return canvas


def to_array(name, img):
    a = np.asarray(img)
    vals = [KEY if px[3] < 128 else rgb565(px[0], px[1], px[2]) for row in a for px in row]
    lines = [f"static const uint16_t {name}[{len(vals)}] = {{"]
    for i in range(0, len(vals), 16):
        lines.append("  " + ", ".join(f"0x{v:04X}" for v in vals[i:i + 16]) + ",")
    lines.append("};")
    return lines


def build_sprites():
    files = [LOCAL / f"wa{i}.png" for i in (1, 2, 3)]
    missing = [f.name for f in files if not f.exists()]
    if missing:
        print("skip pictures, missing:", ", ".join(missing))
        return
    lines = ["#pragma once", "#include <stdint.h>",
             "// Generated locally by tools/build_assets.py from assets/local/wa1-3.png. Not in git.",
             f"#define WA_ICON {ICON}", f"#define WA_BIG {BIG}", f"#define WA_KEY 0x{KEY:04X}", ""]
    cuts = [cut_out(f) for f in files]
    for i, c in enumerate(cuts, 1):
        lines += to_array(f"wa_icon_{i}", fit(c, ICON))
    lines += to_array("wa_big_3", fit(cuts[2], BIG))
    lines.append("static const uint16_t* const wa_icons[3] = {wa_icon_1, wa_icon_2, wa_icon_3};")
    (OUT / "sprites_user.h").write_text("\n".join(lines) + "\n")
    print("wrote src/assets/sprites_user.h")


def read_audio(path):
    if path.suffix.lower() == ".wav":
        with wave.open(str(path)) as w:
            sr, ch, sw = w.getframerate(), w.getnchannels(), w.getsampwidth()
            raw = w.readframes(w.getnframes())
        if sw != 2:
            sys.exit("laugh.wav must be 16-bit PCM (or give an mp3 and install ffmpeg)")
        x = np.frombuffer(raw, dtype=np.int16).astype(float).reshape(-1, ch).mean(1)
    else:
        if not shutil.which("ffmpeg"):
            sys.exit("ffmpeg is needed for mp3/mp4 (brew install ffmpeg), or use the ready-cut laugh.wav")
        start, end = VIDEO_CUT
        out = subprocess.run(["ffmpeg", "-v", "error", "-ss", str(start), "-t", str(end - start), "-i", str(path),
                              "-vn", "-ac", "1", "-ar", str(LAUGH_RATE), "-f", "s16le", "-"],
                             capture_output=True, check=True).stdout
        return np.frombuffer(out, dtype=np.int16).astype(float)
    n = int(len(x) * LAUGH_RATE / sr)                    # simple resample
    return np.interp(np.linspace(0, len(x) - 1, n), np.arange(len(x)), x)


def trim_for_loop(x):
    """Drop leading/trailing silence and fade the ends so the laugh loops without a gap or a pop."""
    frame = LAUGH_RATE // 100
    rms = np.array([np.sqrt(np.mean(x[i:i + frame] ** 2)) for i in range(0, len(x) - frame, frame)])
    loud = np.where(rms > rms.max() * 0.08)[0]
    if len(loud):
        x = x[loud[0] * frame:(loud[-1] + 1) * frame]
    fade = int(0.015 * LAUGH_RATE)
    ramp = np.linspace(0, 1, fade)
    x = x.copy()
    x[:fade] *= ramp
    x[-fade:] *= ramp[::-1]
    return x


def write_laugh(x, path, note):
    x = x[: int(LAUGH_MAX_S * LAUGH_RATE)]
    peak = np.abs(x).max() or 1
    x = np.clip(np.round(x / peak * 26000), -32768, 32767).astype(int)
    lines = ["#pragma once", "#include <stdint.h>", f"// {note}",
             f"#define LAUGH_RATE {LAUGH_RATE}", f"static const int16_t laugh_pcm[{len(x)}] = {{"]
    for i in range(0, len(x), 24):
        lines.append("  " + ", ".join(str(v) for v in x[i:i + 24]) + ",")
    lines.append("};")
    path.write_text("\n".join(lines) + "\n")
    print("wrote", path.relative_to(ROOT), f"({len(x) / LAUGH_RATE:.1f} s)")


def build_laugh():
    for name in ("laugh.wav", "laugh.mp3", "laugh.mp4", "laugh.mov", "laugh.m4a"):
        p = LOCAL / name
        if p.exists():
            write_laugh(trim_for_loop(read_audio(p)), OUT / "laugh_user.h",
                        f"Generated locally from assets/local/{name}. Not in git.")
            return
    print("no laugh.wav / laugh.mp3, the built-in synthesised laugh will be used")


def synth_default_laugh():
    """A cartoon 'ha-ha-ha-ha' made from scratch (no recording), committed as the fallback."""
    sr, out = LAUGH_RATE, []
    for k, f0 in enumerate([330, 360, 340, 310, 280]):
        n = int(0.16 * sr)
        t = np.arange(n) / sr
        pitch = f0 * (1 + 0.15 * np.sin(np.pi * t / t[-1]))
        phase = 2 * np.pi * np.cumsum(pitch) / sr
        voice = sum(np.sin(h * phase) / h ** 0.8 for h in range(1, 12))
        env = np.sin(np.pi * np.minimum(1, t / 0.12)) ** 1.5
        formant = np.sin(2 * np.pi * 800 * t) * 0.3 + 1            # rough "a" colour
        out.append(voice * formant * env)
        out.append(np.zeros(int(0.06 * sr)))
    write_laugh(np.concatenate(out), OUT / "laugh_default.h", "Synthesised cartoon laugh, made by tools/build_assets.py.")


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    if "--default-laugh" in sys.argv:
        synth_default_laugh()
    build_sprites()
    build_laugh()
