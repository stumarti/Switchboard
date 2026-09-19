# Switchboard

A smart-home remote for the **Xteink X4 Pro** e-reader. One e-ink panel, four buttons and a touchscreen, driving your lights, blinds, music, TV and climate through Home Assistant — no app, no phone, just pick it up off the wall dock and tap.

**[⚡ Flash it from your browser](https://stumarti.github.io/Switchboard/)** — no PlatformIO install needed (Chrome/Edge on desktop only).

## Gallery

<table>
<tr>
  <td><img src="docs/images/standby.jpg" width="220" alt="Standby screen"><br><sub>Standby</sub></td>
  <td><img src="docs/images/lighting.jpg" width="220" alt="Lighting screen"><br><sub>Lighting</sub></td>
  <td><img src="docs/images/climate.jpg" width="220" alt="Climate screen"><br><sub>Climate</sub></td>
</tr>
<tr>
  <td><img src="docs/images/blinds.jpg" width="220" alt="Blinds screen"><br><sub>Blinds</sub></td>
  <td><img src="docs/images/music.jpg" width="220" alt="Music screen"><br><sub>Music</sub></td>
  <td><img src="docs/images/tv.jpg" width="220" alt="TV screen"><br><sub>TV</sub></td>
</tr>
<tr>
  <td><img src="docs/images/menu.jpg" width="220" alt="Jump to menu"><br><sub>Jump to menu</sub></td>
</tr>
</table>

## What you need

- An **Xteink X4 Pro**.
- A **Home Assistant** instance on your network.
- **[Switchboard Server](https://github.com/stumarti/Switchboard-Server)** running somewhere on your LAN (a Docker container — a spare mini PC, NAS, or Unraid box all work). This is what tells your remote which lights/blinds/TV etc. actually exist in the room it's in — the firmware doesn't know anything about your house on its own.

## Setup

1. **Get Switchboard Server running first.** Follow its [README](https://github.com/stumarti/Switchboard-Server) — it's a couple of minutes with `docker compose up -d`. Create at least one room (e.g. "Kitchen") and fill in its Home Assistant entities before moving on.
2. **Flash the device.** Easiest way: plug the X4 Pro into your computer over USB and use the [browser flasher](https://stumarti.github.io/Switchboard/) (Chrome or Edge on desktop). Prefer to build it yourself? See [Building from source](#building-from-source) below.
3. **First boot.** The device shows a splash, then walks you through joining your Wi-Fi (pick your network, type the password on the on-screen keyboard).
4. **Pick your room.** Tap the **Home** key → **Settings** → **Select room**. This pulls the room list from Switchboard Server and lets you attach this physical remote to one of them. Nothing else to configure on the device itself.

That's it — the carousel now reflects whatever you set up for that room on the server. Move the remote to a different room later by repeating step 4; nothing needs re-flashing.

## What each screen does

The **carousel** is the home screen — Left/Right cycles through whichever of these your room has turned on (set per-room in Switchboard Server):

| Screen | What it does |
|---|---|
| **Status** | The default page: today's date, weather, indoor temperature, wind, humidity, and a 3-day forecast. What the remote shows when it's just sitting on the dock. |
| **Lighting** | An all-lights on/off toggle with a brightness bar and Warm/Day/Cool presets, plus grids of individual lights and one-tap scenes. |
| **Blinds** | Up/Stop/Down for the whole room, plus each blind or cover individually. |
| **Music** | Now-playing album art, track and artist, volume, and Previous/Pause/Next. |
| **TV** | A D-pad remote (with OK/Back/Home), app-launch buttons (YouTube, Netflix, etc.), and mute/volume. |
| **Xbox** | Reserved for a future Xbox controller screen — currently shows "coming soon". |
| **Wifi** | QR codes for your household's Wi-Fi networks, so a guest can join without asking for the password out loud. |
| **Climate** | A thermostat dial with target temperature and mode (Auto/Heat/Off), plus any extra temperature sensors you've added for the room. |

Two more things, reachable from any carousel page:

- **Tap Home** → the **jump list**: a grid to jump straight to any visible screen, Settings, or the hardware self-test, instead of stepping through the carousel one page at a time.
- **Hold Home** → the **control shade**: quick sliders for backlight brightness and warmth (warm/cool), without leaving whatever page you're on.

### Settings

Reached from the jump list, or the shade's cog icon:

- **Select room** — attach this remote to a different room's profile (see Setup above).
- **Device info** — firmware version and connection status.
- **Wi-Fi setup** — forget the current network and reconnect.
- **Refresh now** — force an immediate pull from Home Assistant.
- **Timeouts** — how long before the screen sleeps, a control page reverts to Status, and how often it refreshes.
- **Developer** — a pixel-grid overlay, a "don't sleep" toggle, the hardware self-test, and a hard reset that clears the picked room (useful if a bad room config ever gets the device stuck).

If the server or Home Assistant can't be reached, the device shows a plain error screen instead of hanging — any button retries, and Home still gets you into Settings.

## The hardware self-test

**Settings → Developer → Button checker.** Confirms every input actually works on a freshly-assembled or newly-flashed unit:

- **Buttons** — ticks off Left, Right, Power, and Home (the capacitive key below the panel) the first time each is pressed.
- **Touchscreen** — shows live coordinates and a running tap count as you touch the screen.
- **Backlight** — steps through 0/25/50/75/100% brightness.
- **Refresh** — lets you trigger a full (clean) e-ink refresh on demand, separate from the fast partial refreshes used everywhere else.

Hold Home to restart the test.

---

## Building from source

### Hardware it targets

Selected by `-DFREEINK_DEVICE_X4PRO`:

- ESP32-S3 (16 MB flash, 8 MB PSRAM), 800×480 e-ink (SSD1677, or UC8179 on newer units — auto-detected)
- GT911 capacitive touch
- Nav keys: Left = GPIO0, Right = GPIO7, Power = GPIO3
- Warm/cool PWM frontlight, PCF8563/BM8563 RTC

### One-time setup

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
2. Set your Wi-Fi in `include/config.h` (`WIFI_SSID` / `WIFI_PASS`).
3. If your Switchboard Server isn't at the default `switchboard.local:45678`, point `SWITCHBOARD_SERVER_HOST`/`PORT` in the same file at it.

### Build & flash

```
pio run -e x4pro -t upload -t monitor
```

Wake the device first if it's asleep, or PlatformIO may not find the port.

> **Hit a `Network.h` error?** Arduino-ESP32 core 3.x needs the `pioarduino` platform (already pinned here) — a stale cached core from an older attempt is the usual cause. `Remove-Item -Recurse -Force "$env:USERPROFILE\.platformio\packages\framework-arduinoespressif32*"` and `.pio`, then rebuild.

## Releases & the web flasher

```
git tag v0.4.0
git push origin v0.4.0
```

GitHub Actions builds the firmware, merges it into one flashable image, attaches it to a GitHub Release, and publishes it to `docs/firmware/` — which is what the [browser flasher](https://stumarti.github.io/Switchboard/) actually serves, once GitHub Pages is turned on for this repo (Settings → Pages → `develop` / `/docs`). Chrome/Edge desktop only — Web Serial isn't available elsewhere.

## The Switchboard Server

This firmware is a client — it doesn't talk to Home Assistant directly and carries no room setup on board. All of that lives in [Switchboard Server](https://github.com/stumarti/Switchboard-Server), published as `ghcr.io/stumarti/switchboard-server` with an Unraid template included. It's found automatically via mDNS (`switchboard.local:45678` by default — see `include/config.h` to change it) and exposes what the firmware needs: the room list, each room's full config, and the household's shared Wi-Fi/Home Assistant connection.

## Layout

```
platformio.ini        env:x4pro, links the FreeInk SDK libs by symlink
include/
  config.h            Wi-Fi creds, server host/port, device slug
  screen_*.h           one file per carousel/settings/menu screen
  *_client.h            HTTP clients for the server's API
  ui.h, atkinson_font.h, weather_icons.h, assets.h   drawing + fonts + icons
src/
  main.cpp             the state machine: boot, carousel, settings, sleep/wake
tools/
  gen_version.py        derives FIRMWARE_VERSION from git describe
  gen_atkinson_fonts.sh  regenerates fonts from tools/fonts/*.ttf
  gen_weather_icons.py   regenerates icons from tools/weather_svg/*.svg
docs/
  index.html            the browser flasher, served via GitHub Pages
  firmware/               latest release's flashable image + manifest.json
  images/                  the gallery photos above
```

## Notes

- Rendering is landscape-native (800×480), drawn straight into the panel's framebuffer.
- Splash and the self-test use a full (clean) e-ink refresh; the carousel uses fast partial refreshes, auto-promoting to a full refresh once enough have piled up.
- Every text face is Atkinson Hyperlegible, baked into `include/atkinson_font.h` at fixed sizes — regenerate via `tools/gen_atkinson_fonts.sh` for a new size.
