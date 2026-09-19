# Switchboard — X4 Pro boot sequence

A PlatformIO firmware for the **Xteink X4 Pro** that runs a boot sequence on
the e-ink panel:

1. **Splash** — the Switchboard logo (an MDI `remote` mark), name, and slogan.
2. **Wi-Fi** — runs through the Wi-Fi connection with live status on screen.
3. **Home** — a blank layout scaffold: a status bar (brand, Wi-Fi signal,
   battery meter) up top, a footer bar along the bottom, and an otherwise
   empty content area between them. Not interactive yet — this is where the
   device's main layout gets nailed down before any real screen is built on
   top of it.
4. **Debug** — hold the Home key from the Home screen to reach an interactive
   hardware self-test: it checks all four buttons, the touchscreen, and the
   backlight.

> ### ⚠️ About the `Network.h` error (now fixed in this config)
>
> Arduino-ESP32 core 3.x split the `Network` library out of `WiFi`, so
> `WiFi.h` now `#include`s a sibling `<Network.h>` that PlatformIO's dependency
> finder must pull in. Two things break that, and both are handled here:
>
> - **Wrong platform.** The stock PlatformIO `espressif32` ships the ancient
>   core 2.0.17 (no `Network.h` at all). This project pins the **pioarduino**
>   platform (core 3.3.x) — keep that `platform =` URL.
> - **Aggressive LDF mode.** Setting `lib_ldf_mode = chain+`/`deep+` changes how
>   the framework's own inter-library dependencies resolve and stops WiFi from
>   finding Network. This config leaves LDF at its default and instead adds the
>   framework's `Network/src` path explicitly, so it resolves either way. Don't
>   add a `lib_ldf_mode` override.
>
> If you still hit `Network.h` after pulling this config, it's a stale platform
> cached from an earlier attempt. Close VS Code and, in PowerShell, delete the
> framework packages so they re-extract clean:
>
> ```powershell
> Remove-Item -Recurse -Force "$env:USERPROFILE\.platformio\packages\framework-arduinoespressif32*"
> Remove-Item -Recurse -Force "<your-project-folder>\.pio"
> ```
>
> Then rebuild (the correct core re-downloads — a few hundred MB).


## The hardware self-test (stage 4)

The debug screen is interactive — it verifies every input surface and the
backlight on real hardware:

- **Buttons** — four check-off boxes that tick the first time each is pressed:
  **Left** (GPIO0), **Right** (GPIO7), **Power** (GPIO3), and **Home** (the
  GT911 capacitive key below the panel). On the X4 Pro these *are* the four
  buttons — there is no separate Back/Confirm key in hardware, so the fourth
  tested button is the capacitive Home key. A "N / 4 pressed" tally tracks
  progress.
- **Touchscreen** — a target box showing live `x=… y=…` coordinates, a running
  tap count, and a crosshair drawn at your last touch.
- **Backlight** — steps `0 → 25 → 50 → 75 → 100%` with a level bar. Tap the
  on-screen "Step backlight" button, or press the **Home** key, to advance it.
- **Refresh** — the test repaints with the panel's **partial (fast) refresh**,
  which is what keeps button/touch feedback snappy. Partial refreshes ghost
  over time, so there's a **"Full refresh"** button that does a clean full
  refresh and scrubs the accumulated ghosting. A status line shows the current
  mode and how many partials have piled up since the last full; the test also
  auto-promotes to a full refresh once that count gets high, so it never looks
  dirty during a long session.

**Hold the Home key** to restart the test.

> The partial/full split is exactly the e-ink tradeoff: fast refresh is
> responsive but leaves ghosts; full refresh is clean but flashes the whole
> panel. The button lets you trigger the clean pass on demand.

It's a **standalone project** (not a CrossPoint fork). It drives the hardware
through the FreeInk SDK's stable HAL — the same `EInkDisplay` / `InputManager`
/ `BoardConfig` libraries CrossPoint uses — linked by `symlink://`, so it runs
on the real device without vendoring the HAL.

## Hardware it targets

Selected by `-DFREEINK_DEVICE_X4PRO`, which maps to the SDK's `XteinkX4Pro`
board profile:

- ESP32-S3 (S3R8: 16 MB flash, 8 MB PSRAM), 800×480 e-ink, SSD1677 controller
- GT911 capacitive touch on the shared I²C bus
- Nav keys: Left = GPIO0, Right = GPIO7, Power = GPIO3
- Warm/cool PWM frontlight, PCF8563/BM8563 RTC

The firmware reads its pins from `BoardConfig::ACTIVE`, so nothing is
hardcoded — swapping the device flag re-targets it.

## One-time setup

1. **Get the FreeInk SDK** next to this project:
   ```
   git clone https://github.com/Free-Ink/freeink-sdk
   ```
   So your layout is:
   ```
   parent/
     freeink-sdk/
     switchboard-boot/     <- this project
   ```
   If you put it elsewhere, edit `[freeink] sdk = ...` in `platformio.ini`
   (an absolute path is safest on Windows).

2. **Set your Wi-Fi** in `include/config.h`:
   ```c
   #define WIFI_SSID "your-network"
   #define WIFI_PASS "your-password"
   ```
   Leave `WIFI_SSID` empty (`""`) to watch the whole sequence on a bench
   without a network — the Wi-Fi stage then renders, says "No network
   configured", and moves on.

## Build & flash

With the PlatformIO CLI (or the VS Code PlatformIO extension):

```
pio run -e x4pro -t upload -t monitor
```

Wake the device before uploading if it has gone to sleep, or PlatformIO may
fail to auto-detect the port.

## How to drive it

- **Splash** auto-advances after ~2.5 s (or press any key / tap to skip).
- **Wi-Fi** advances on its own once connected, failed, or skipped.
- **Home**: nothing to interact with yet — it's a static layout scaffold.
  **Hold Home** to jump to the hardware self-test.
- **Debug / self-test**: press each of the four buttons to tick them off, touch
  the target box to see live coordinates + a crosshair, and tap "Step backlight"
  (or the Home key) to step brightness. The screen runs on **partial refresh**;
  tap **"Full refresh"** to scrub the ghosting. **Hold Home** to restart the
  test.

## Layout

```
platformio.ini        env:x4pro, links the FreeInk SDK libs by symlink
include/
  config.h            Wi-Fi creds, brand strings
  assets.h            1-bpp Switchboard logo + wifi icon (FreeInk Icon format)
  atkinson_font.h     Atkinson Hyperlegible bitmap fonts (10/12/24/28px + a 68px digit face)
  ui.h                thin drawing surface over EInkDisplay + DisplayTarget
  Logging.h           consumer-provided log shim the SDK libs #include
src/
  main.cpp            the boot state machine (Splash / Wi-Fi / Home / Debug)
```

### Why there's a Logging.h here

Several FreeInk SDK libraries do `#include <Logging.h>` and call
`LOG_INF(tag, fmt, ...)`, but the SDK deliberately doesn't ship that header —
it expects the app to provide one so log output lands in the app's own Serial
stream. `include/Logging.h` is that shim; it routes every level to Serial. If
you ever see `fatal error: Logging.h: No such file`, it means the project's
`include/` dir isn't on the compiler's include path — check that the
`include_flags` block in `platformio.ini` is intact.

## Notes

- Rendering is landscape-native (800×480); the panel's own framebuffer is
  drawn into directly via `EInkDisplay::getFrameBuffer()`.
- Splash and Debug use a **full** e-ink refresh (clean, no ghosting);
  intermediate screens use **fast** refreshes.
- Every text face is Atkinson Hyperlegible, baked into `include/atkinson_font.h`
  at fixed pixel sizes (10/12/24/28px, plus a 68px digits-only face for the
  temperature readout) — there's no runtime scaling, so a new size means
  regenerating via `tools/gen_atkinson_fonts.sh` and binding it to a slot in
  `Ui::begin()` (`ui.h`).
