# Screens

The **carousel** is the home screen. Left/Right cycles through whichever of these your room has turned on (set per room in Switchboard Server):

| Screen | What it does |
|---|---|
| **Status** | The default page: today's date, weather, indoor temperature, wind, humidity, and a 3-day forecast. What the remote shows when it's just sitting on the dock. |
| **Lighting** | An all-lights on/off toggle with a brightness bar and Warm/Day/Cool presets, plus grids of individual lights and one-tap scenes. Lights that support it also get colour and effects. |
| **Blinds** | Up/Stop/Down for the whole room, plus each blind or cover individually. |
| **Music** | Now-playing album art, track and artist, volume, and Previous/Pause/Next. |
| **TV** | A D-pad remote (with OK/Back/Home), up to four app-launch buttons (each with its own icon if you pick one on the server, otherwise the YouTube/Netflix logo or a generic app icon), and mute/volume. |
| **Receiver** | An Enigma2 satellite/cable box (Vu+, Dreambox, …) through Home Assistant's Enigma2 integration: the channel on now with its picon, the programme on now (with its times) and next, up to six favourite-channel buttons (each with the channel's picon or an icon), channel up/down, power, mute and volume. "Next" and the favourites' picons need the box's address on the server (it reads the box's own web interface); without it the page shows what Home Assistant has. Off until switched on for the room. |
| **Xbox** | Now playing, and a library of games to launch: a list set on the server, or the console's own library read by the server (up to 36). |
| **Wifi** | QR codes for your household's Wi-Fi networks, so a guest can join without asking for the password out loud. |
| **Climate** | A thermostat dial with target temperature and mode (Auto/Heat/Off), plus any extra temperature sensors you've added for the room. |

## Passive and live pages

Most pages are **passive**: they show what the last refresh brought and wait for a press. Music and Xbox are **live** while something is playing — and only then, only while that page is on screen and Wi-Fi is already up: the remote holds one request open on the server, which answers the moment the track or game changes. A new track repaints in full with its art (resized and dithered by the server, so the remote never decodes an image); a pause or a volume change is a quick partial repaint. The Wi-Fi timeout (Settings → Timeouts) still turns the radio off on schedule, which ends the watch. Against an older server the pages fall back to re-reading every 30 s while playing.

## Refreshes

The remote wakes on a timer to refresh, at the interval its room (or the remote itself) sets on the server. With **On the clock** switched on there, a 15, 30 or 60 minute refresh lands on the clock instead — every 30 minutes means on the hour and half past. Each remote is offset by a few seconds from the next so they don't all reach the server at once.

## Jump list and control shade

Reachable from any carousel page:

- **Tap Home** → the **jump list**: a grid to jump straight to any visible screen, Settings, or the hardware self-test, instead of stepping through the carousel one page at a time.
- **Hold Home** → the **control shade**: quick sliders for backlight brightness and warmth (warm/cool), without leaving whatever page you're on.

## Settings

Reached from the jump list, or the shade's cog icon:

- **Select room** — attach this remote to a different room's profile. This always wins over the room the server assigned by MAC address.
- **Device info** — firmware version, connection status, battery and updates, with **Check for update** when the server allows it (see [Updates](Updates.md)).
- **Wi-Fi setup** — forget the current network and reconnect.
- **Refresh now** — force an immediate pull from Home Assistant.
- **Timeouts** — how long before the screen sleeps, a control page reverts to Status, how often it refreshes, and how long the Wi-Fi radio stays on while idle (by default, the same as the screen; the next button press reconnects).
- **Developer** — a pixel-grid overlay, a "don't sleep" toggle, the hardware [self-test](Self-Test.md), and a hard reset that clears the picked room (useful if a bad room config ever gets the device stuck).

## When something's unreachable

If the server or Home Assistant can't be reached, the device shows a plain error screen instead of hanging — any button retries, and Home still gets you into Settings.
