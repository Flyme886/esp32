# Notes for coding agents: pin-hao-wa (M5Stack StickS3 merge game)

The board is an M5Stack StickS3 (ESP32-S3-PICO-1-N8R8, 8 MB flash, 8 MB PSRAM) connected to the user's Mac over USB-C.

```bash
cd pin-hao-wa
pio run -t upload               # build and flash
pio device monitor              # optional, 115200 baud
```

- The pictures and the laugh are already converted and committed in `src/assets/` (`sprites.h`, `laugh.h`). Only rerun `python3 tools/build_assets.py` after changing a file in `assets/source/` (needs pillow, numpy, scipy; ffmpeg only for video or mp3 input).
- If PlatformIO is missing: `brew install platformio`. If upload cannot open the port, ask the user to hold the side reset button until the green LED blinks, then retry.
- Library versions are pinned in `platformio.ini` (M5Unified 0.3.0, M5GFX 0.3.0, M5PM1 1.0.7).
