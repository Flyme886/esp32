# Notes for coding agents

## plant-pet (M5Stack StickS3 firmware)

The board is an M5Stack StickS3 (ESP32-S3-PICO-1-N8R8, 8 MB flash, 8 MB PSRAM) connected to the user's Mac over USB-C.

To build and flash:

```bash
cd plant-pet
pio run -t upload          # builds and flashes
pio device monitor         # optional, 115200 baud
```

- If PlatformIO is missing: `brew install platformio` (or `pipx install platformio`).
- If the upload cannot find or open the port, ask the user to hold the side reset button until the internal green LED blinks (download mode), then retry.
- The generated headers in `plant-pet/src/assets/` are committed. Do not regenerate them unless the photo or the chosen sound clips change; then run `python3 tools/build_assets.py` from `plant-pet/`.
- Library versions are pinned in `platformio.ini` (M5Unified 0.3.0, M5GFX 0.3.0, M5PM1 1.0.7). Older M5Unified releases do not know the StickS3.
- Keep the speaker volume at or below about 190/255: on battery, louder playback can reboot the board.
