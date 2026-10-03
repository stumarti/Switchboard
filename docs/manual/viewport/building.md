# Building the viewport firmware

The firmware is a [PlatformIO](https://platformio.org) project: [stumarti/Switchboard-Viewport](https://github.com/stumarti/Switchboard-Viewport).

```
pio run -e e1002 -t upload -t monitor
```

Nothing about your house is compiled in. `src/config.h` holds only the hardware (pins, the battery thresholds) and the fallbacks for when the server can't be reached. The firmware version comes from `git describe` (`tools/gen_version.py`), and the image carries the marker `SWITCHBOARD_FW:e1002:<version>`, which the server checks before it offers an update.

## What's where

| Path | What it is |
|---|---|
| `src/main.cpp` | One wake, from start to sleep: buttons, battery, Wi-Fi, server, pairing, bundle, theme, update, carousel, the screen's state, drawing, sleep |
| `src/net/` | Wi-Fi (saved networks, the setup hotspot and its page), finding the server and talking to it |
| `src/app/` | The carousel, the theme packs, updates, the flash cache, the hardware |
| `src/render/` | Every screen: the dashboard's sections (`screens.cpp`) and the device's own screens (`system.cpp`) |
| `src/kd/` | The panel's built-in fonts and colour art |

## Tests

`test/host/run.sh` builds the logic and every renderer with the host's g++ and runs them, with no board needed. It renders each screen in `test/fixtures` to `test/host/out/`, and those renders are the images in this manual. `test/compare/compare.sh` runs the panel's original firmware and this one against the same captured Home Assistant, and fails if a single pixel differs on Status, Heating, Security, the error screens or the charge screen.

CI runs the ESP32 build and both tests on every push, and keeps the renders as an artifact.

## Releases

Push a tag, and the release workflow builds and publishes it:

```
git tag v0.2.0 && git push origin v0.2.0
```

The GitHub release gets `switchboard-viewport-<tag>.bin` (the whole flash image, for USB) and `switchboard-e1002-app-<tag>.bin` with its `.sha256`. The second one is what the server installs over the air ([Updates](updates.md)).
