# Notes for coding agents: pin-hao-wa (M5Stack StickS3 merge game)

The board is an M5Stack StickS3 (ESP32-S3-PICO-1-N8R8, 8 MB flash, 8 MB PSRAM) connected to the user's Mac over USB-C.

```bash
cd pin-hao-wa
python3 tools/build_assets.py   # converts the user's local pictures and laugh, if present
pio run -t upload               # build and flash
pio device monitor              # optional, 115200 baud
```

- The character pictures and the laugh sound are the user's local files in `assets/local/` (`wa1.png`, `wa2.png`, `wa3.png`, `laugh.wav` or `laugh.mp4`). They and the headers generated from them (`src/assets/sprites_user.h`, `src/assets/laugh_user.h`) are git-ignored on purpose: the pictures may be copyrighted. Never commit them.
- If those files are missing, ask the user to put them in `assets/local/`. The firmware still builds without them, using numbered placeholder faces and a synthesised laugh.
- Video or mp3 input needs ffmpeg (`brew install ffmpeg`); the script cuts seconds 1–4.
- If PlatformIO is missing: `brew install platformio`. If upload cannot open the port, ask the user to hold the side reset button until the green LED blinks, then retry.
- Library versions are pinned in `platformio.ini` (M5Unified 0.3.0, M5GFX 0.3.0, M5PM1 1.0.7).
