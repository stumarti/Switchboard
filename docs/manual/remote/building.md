# Building the firmware

## Hardware it targets

Selected by `-DFREEINK_DEVICE_X4PRO`:

- ESP32-S3 (16 MB flash, 8 MB PSRAM), 800×480 e-ink (SSD1677, or UC8179 on newer units — auto-detected)
- GT911 capacitive touch
- Nav keys: Left = GPIO0, Right = GPIO7, Power = GPIO3
- Warm/cool PWM frontlight, PCF8563/BM8563 RTC

## One-time setup

1. Clone the [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) next to this project:
   ```
   git clone https://github.com/Free-Ink/freeink-sdk
   ```
   So you end up with:
   ```
   parent/
     freeink-sdk/
     Switchboard/     <- this project
   ```
   CI and releases build against a pinned SDK commit (see `.github/workflows/`); check that commit out if you want the same build.
2. Set your Wi-Fi in `include/config.h` (`WIFI_SSID` / `WIFI_PASS`).
3. If your Switchboard Server isn't at the default `switchboard.local:45678`, point `SWITCHBOARD_SERVER_HOST`/`PORT` in the same file at it.

## Build & flash

```
pio run -e x4pro -t upload -t monitor
```

Wake the device first if it's asleep, or PlatformIO may not find the port.

The firmware version comes from `git describe` (`tools/gen_version.py`): a build of a release tag reports that tag, e.g. `v0.4.0`, and a build with uncommitted changes ends in `-dirty`.

> **Hit a `Network.h` error?** Arduino-ESP32 core 3.x needs the `pioarduino` platform (already pinned here) — a stale cached core from an older attempt is the usual cause. `Remove-Item -Recurse -Force "$env:USERPROFILE\.platformio\packages\framework-arduinoespressif32*"` and `.pio`, then rebuild.

## Tests

`./test/host/run.sh` builds the firmware's pure logic — the Home Assistant state parsers every page uses, the HTTP helpers' host-name handling, the refresh schedule and the update policy — with `g++` on your PC and runs it; no hardware or PlatformIO needed (it fetches ArduinoJson on first run). CI runs it on every push and pull request, beside the firmware build.

`tools/screenshots/run.sh` goes further: it compiles the whole firmware for your PC and draws every screen, talking to a real server (see [Screenshots](../screenshots.md)).

## Source layout

```
platformio.ini        env:x4pro, links the FreeInk SDK libs by symlink
include/
  config.h            Wi-Fi creds, server host/port, device slug
  screen_*.h          one file per carousel/settings/menu screen
  *_client.h          HTTP clients for the server's API
  app/                the state machine's pieces: boot, stages, refresh, quick access
  ota_*.h             over-the-air updates
  ui.h, atkinson_font.h, weather_icons.h, assets.h   drawing + fonts + icons
src/
  main.cpp            the state machine: boot, carousel, settings, sleep/wake
tools/
  gen_version.py         derives FIRMWARE_VERSION from git describe
  gen_atkinson_fonts.sh  regenerates fonts from tools/fonts/*.ttf
  gen_weather_icons.py   regenerates icons from tools/weather_svg/*.svg
  screenshots/           draws the remote's screens for this manual
test/host/            host-side unit tests
docs/                 GitHub Pages
  index.html          the browser flasher
  firmware/           latest release's flashable image + manifest.json
  images/             the README's gallery photos
  manual/             this manual
```

## Rendering notes

- Rendering is landscape-native (800×480), drawn straight into the panel's framebuffer.
- Splash and the self-test use a full (clean) e-ink refresh; the carousel uses fast partial refreshes, auto-promoting to a full refresh once enough have piled up.
- Every text face is Atkinson Hyperlegible, baked into `include/atkinson_font.h` at fixed sizes — regenerate via `tools/gen_atkinson_fonts.sh` for a new size.
