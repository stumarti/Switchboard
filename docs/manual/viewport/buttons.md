# Buttons, refresh and sleep

A viewport runs on battery, so it spends nearly all its time in deep sleep. It wakes on a timer the server sets, or when you press a button, fetches what changed, redraws if it has to, and goes back to sleep. A full refresh of the colour panel takes about 20 seconds. If a screen hasn't changed, the server answers `304` and the panel isn't touched.

## The buttons

<figure class="shot"><div class="panel"><img src="../images/viewport/device/system-info.png" alt="Device info"></div><figcaption>Device info (hold the left button): what each button does, and the display's state.</figcaption></figure>

| Button | Press | Hold (1 second) |
|---|---|---|
| **Left** | Previous screen (direct buttons: the last screen) | **Device info**: what each button does, the refresh interval, battery, firmware, server, Wi-Fi network and signal, MAC |
| **Middle** (green) | Refresh: fetch the latest and redraw this screen (direct buttons: the first screen) | **Wi-Fi setup**: start the hotspot and QR codes again ([Setting up](setup.md)) |
| **Right** | Next screen (direct buttons: the second screen) | **Clear screen**: fill the panel white. The next wake redraws it |

The layout's **Carousel → Buttons** setting picks how they work:

- **Step** (the default): right and left step through the layout's screens in order, wrapping round at either end, and the middle refreshes the one on show.
- **Straight to a screen**: each button always shows the same screen, whichever is on show: the middle the first, the right the second, the left the last. The kitchen dashboard uses this, so it works as the panel's own firmware did: middle for Status, right for Heating, left for Security. Device info then names each button's screen.

## The carousel

The layout's **Carousel → Between presses** setting decides what happens when nobody presses a button:

- **Stay on the current screen:** keep it on show and refresh it.
- **Move to the next screen:** every N minutes.
- **Go back to the first screen:** go back to the first screen N minutes after the last button press. The kitchen dashboard does this: Heating or Security goes back to Status after 30 minutes.

The display wakes in time for the next carousel step even if its normal refresh comes later.

## How often it refreshes

The server decides the time of each wake and sends it with every screen (`refreshInSec`). That time is:

- the layout's **refresh interval** (30 minutes for the kitchen dashboard);
- sooner when something is due: a meeting starting or ending, a departure becoming imminent, or a conditional section that's on show (every few minutes, so it stays current);
- never sooner than a minute, and never later than 12 hours.

If the display can't reach Wi-Fi, the server or Home Assistant, it tries again after its usual interval (quiet hours included), remembered from the last time it reached the server. While it waits to be approved, it checks every 2 minutes.

## Quiet hours

During the layout's **Quiet hours** (11pm to 6am for the kitchen dashboard), the display wakes less often: every 30, 60, 120 or 240 minutes, as you choose. The footer shows a bed-and-clock icon while they last. The display wakes at the end of quiet hours to go back to its normal interval, and a button press still works as usual.

## Health

Every request carries the display's battery, its temperature and humidity (from its built-in sensor), its Wi-Fi signal, its firmware and its board. The server's [Home page](../server/home.md) and the display's own page show them and warn about a low battery or weak signal. The server also learns [how long the battery lasts](../server/battery-life.md) and flags a display it hasn't heard from in three of its refresh intervals.
