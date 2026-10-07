# Switchboard

**Your smart home, on paper.** Switchboard puts Home Assistant on e-ink: remotes you pick up off the wall, colour wall displays that show your whole house at a glance, and meeting-room signs for the office. One small server sets them all up. No app, no phone, no tablet with a charger cable hanging off it.

**[⚡ Flash one from your browser](https://stumarti.github.io/Switchboard/)** · **[Read the manual](https://stumarti.github.io/Switchboard/manual/)** · **[Try the demo, no hardware needed](https://stumarti.github.io/Switchboard/manual/demo.html)**

<p><img src="docs/images/fridge-status.jpg" width="800" alt="A Switchboard viewport and remote on a kitchen fridge: the weather, the energy, what's happening now and tomorrow's bins, with the remote on Lighting"></p>
<sub>A viewport and a remote on a kitchen fridge, in an evening's room light.</sub>

## Forget about charging

E-ink only uses power to change the picture. Switchboard is built around that:

| | On a charge | |
|---|---|---|
| 🔋 **Remote** (Xteink X4 Pro) | **about 30 days** with light use | The screen and the Wi-Fi go to sleep as soon as you put it down. A press wakes it. |
| 🔋 **Viewport** (Seeed reTerminal E1002) | **around 3 months** | It sleeps between refreshes, and when nothing has changed it doesn't redraw at all. It refreshes on the clock, and less often overnight. |

The server does all the heavy lifting, so the devices don't have to. It talks to Home Assistant, works out every value, colour and icon, and dithers the album art. The devices just wake up, draw and go back to sleep.

You'll know before a battery runs out. The server learns how fast each device drains, shows **the days each one has left**, warns you a few days ahead, and can send the battery levels to Home Assistant for your own automations.

## Use cases

### A remote in every room

**One remote replaces the five or six switches by the door.** Every light and dimmer, the scenes, the blinds, the heating, the TV and the music are on one screen you can pick up and carry to the sofa.

**There's nothing to learn.** Every button says what it does, in plain English and in your own words: *All lights*, *Movie night*, *Close blinds*, *Kitchen music*. Guests, grandparents and the babysitter can use it without being shown.

<table>
<tr>
  <td valign="top"><img src="docs/images/lighting.jpg" width="200" alt="Lighting"><br><sub><b>Living room.</b> Every light and dimmer, and scenes like <i>Movie night</i></sub></td>
  <td valign="top"><img src="docs/images/tv.jpg" width="200" alt="TV"><br><sub>The TV, its apps and volume: the remote that doesn't get lost</sub></td>
  <td valign="top"><img src="docs/images/music.jpg" width="200" alt="Music"><br><sub>The speakers, with album art</sub></td>
</tr>
<tr>
  <td valign="top"><img src="docs/images/blinds.jpg" width="200" alt="Blinds"><br><sub><b>Bedroom.</b> Blinds down and lights off without getting up</sub></td>
  <td valign="top"><img src="docs/images/climate.jpg" width="200" alt="Climate"><br><sub>The room's heating for the night</sub></td>
  <td valign="top"><img src="docs/images/menu.jpg" width="200" alt="Quick Access"><br><sub><b>Kitchen.</b> Island lights, the extractor, the music: two taps away</sub></td>
</tr>
<tr>
  <td valign="top"><img src="docs/manual/images/remote/lighting-colour.png" width="200" alt="Colour lighting"><br><sub><b>Kids' room.</b> Their lights and a colour nightlight, and nothing else to wander into</sub></td>
  <td valign="top"><img src="docs/manual/images/remote/xbox.png" width="200" alt="Xbox"><br><sub><b>Den or media room.</b> The Xbox and its games</sub></td>
  <td valign="top"><img src="docs/manual/images/remote/receiver.png" width="200" alt="Receiver"><br><sub>The satellite box's favourite channels</sub></td>
</tr>
<tr>
  <td valign="top"><img src="docs/manual/images/remote/wifi-qr.png" width="200" alt="Guest Wi-Fi QR code"><br><sub><b>Guest room or holiday let.</b> The Wi-Fi as a code to scan</sub></td>
  <td valign="top"><img src="docs/manual/images/remote/climate.png" width="200" alt="Climate"><br><sub>The heating, without a manual on the bedside table</sub></td>
  <td valign="top"><img src="docs/manual/images/remote/blinds.png" width="200" alt="Blinds"><br><sub><b>Meeting room.</b> Blinds and lights for a presentation</sub></td>
</tr>
</table>

### A viewport wherever you'd glance

<table>
<tr>
  <td valign="top"><img src="docs/manual/images/viewport/device/kitchen-panel-status.png" width="380" alt="Kitchen dashboard"><br><sub><b>Kitchen or fridge.</b> The weather, what needs attention now, today's calendar and the home battery</sub></td>
  <td valign="top"><img src="docs/manual/images/viewport/device/hall.png" width="380" alt="Hall panel"><br><sub><b>Front door.</b> The bins, a reminder on the way out, the air and the pollen</sub></td>
</tr>
<tr>
  <td valign="top"><img src="docs/manual/images/viewport/device/kitchen-panel-energy.png" width="380" alt="Energy"><br><sub><b>Energy.</b> Solar against the forecast, and where your power went</sub></td>
  <td valign="top"><img src="docs/manual/images/viewport/device/kitchen-panel-heating.png" width="380" alt="Heating"><br><sub><b>Heating.</b> Every zone against its setpoint, and the hot water</sub></td>
</tr>
<tr>
  <td valign="top"><img src="docs/manual/images/viewport/device/kitchen-panel-presence.png" width="380" alt="Presence and departures"><br><sub><b>Family.</b> Who's home, the room temperatures, what's playing, the next bus</sub></td>
  <td valign="top"><img src="docs/manual/images/viewport/device/kitchen-panel-security.png" width="380" alt="Security"><br><sub><b>Security.</b> The alarm, doors and windows, motion and cameras</sub></td>
</tr>
<tr>
  <td valign="top"><img src="docs/manual/images/viewport/device/boardroom-meeting.png" width="380" alt="Meeting room sign"><br><sub><b>Meeting room door.</b> Free or in use, until when, and what's next</sub></td>
  <td valign="top"><img src="docs/manual/images/viewport/device/boardroom-rooms.png" width="380" alt="Room finder"><br><sub><b>By the lifts.</b> Which rooms are free now</sub></td>
</tr>
<tr>
  <td valign="top"><img src="docs/manual/images/viewport/device/reception-visitors.png" width="380" alt="Reception visitors"><br><sub><b>Reception.</b> The guest Wi-Fi to scan, a welcome for today's visitor, the office's air</sub></td>
  <td valign="top"><img src="docs/manual/images/viewport/device/reception-home.png" width="380" alt="Reception"><br><sub><b>Lobby.</b> The weather, today's meetings and company news</sub></td>
</tr>
</table>

### Coming: a 13.3" board for events and shops

The **Seeed reTerminal E1004** is the same idea at 13.3", portrait or landscape: event signs that point the way, menus and offers in a shop window, photos from Immich with live information beside them. It's planned, not built yet. These are mock-ups at the panel's real resolution and in its six inks.

<table>
<tr>
  <td valign="top"><img src="docs/images/e1004-directory.png" width="250" alt="Mock-up: an event directory with an arrow, room and status for each event"><br><sub><b>Event directory.</b> Every event with its arrow, where it is and what's on now</sub></td>
  <td valign="top"><img src="docs/images/e1004-pointer.png" width="250" alt="Mock-up: a sign pointing the way to one event, with the distance and directions"><br><sub><b>This way to…</b> One event, the way there and the next talk</sub></td>
  <td valign="top"><img src="docs/images/e1004-cafe.jpg" width="330" alt="Mock-up: a café's special with a picture, price and an order-ahead QR code"><br><sub><b>Shop or café.</b> Today's special, the price and a QR code, with live weather and opening hours</sub></td>
</tr>
</table>

## The remote

A smart-home remote on the **Xteink X4 Pro** e-reader. It has a crisp e-ink panel, four buttons and a touchscreen, and it controls each room's lights, blinds, music, TV and heating.

- **One remote per room, or one for the whole house.** Pick its room in Settings and it changes in seconds, with nothing to re-flash.
- **Pages for everything:** Status, Lighting, Blinds, Music, TV, Receiver, Xbox, a Wi-Fi QR code for guests, and Climate. Flip through them with the buttons, or jump straight to one from Quick Access.
- **Readable in any light**, with a backlight shade for the dark.
- **It updates itself** over Wi-Fi from your server. If a new version can't reach home, it rolls back on its own.

## The viewport

<p>
  <img src="docs/images/fridge-energy.jpg" width="400" alt="The viewport's energy screen on the fridge: solar against the forecast and where the power went, beside the remote's thermostat">
  <img src="docs/images/fridge-heating.jpg" width="400" alt="The viewport's heating screen on the fridge, every zone against its setpoint, beside the remote playing music">
</p>

A colour e-ink wall display on the **Seeed reTerminal E1002**. Its 7.3" Spectra 6 panel shows six colours, it has no glow, and it reads from across the room. Put one in the kitchen, the hall or beside a meeting room door.

- **Build any screen.** Choose from twenty-five section types: weather, energy totals and graph, the home battery, status icons, "what needs attention now", heating, calendar, alarm, doors and windows, motion, cameras, people, now playing, bus and train departures, bin collection, air quality and pollen, a guest Wi-Fi QR code, your own messages, announcements and more. Arrange them in one, two or three columns, or in the kitchen dashboard's sidebar layout.
- **Three buttons:** previous, next and home, on every screen. It can also move through its screens on its own.
- **Set up with your phone:** scan the QR code on the panel, then pick your Wi-Fi. No keyboard needed.
- **It's a room sensor too:** its temperature and humidity can go to Home Assistant, along with its battery.

### In the office

<p><img src="docs/images/office-door.jpg" width="800" alt="A render of a Switchboard viewport on a glass meeting-room door, showing the Boardroom in use until 12:00"></p>
<sub>A render: a viewport on a meeting room's glass door, showing the room's real screen as the firmware draws it.</sub>

The viewport makes a meeting-room sign that reads like paper from across the corridor, with no glow and no cable.

- **Straight from your calendar.** Each sign reads its room's calendar from a Google Calendar, Outlook / Microsoft 365 or iCloud link, or any app's iCal link. You don't need Home Assistant for this. Recurring meetings, moved ones and cancellations come through as they do in the calendar.
- **On time.** A sign changes two minutes before each meeting starts or ends, so the redraw is finished by the time people arrive. Back-to-back meetings read as one block.
- **Free rooms at the press of a button.** The second screen lists the other rooms, free ones first.
- **A whole floor at once.** Paste the list of rooms from a spreadsheet. Each room gets its sign, and its display is named and assigned before it's even switched on.
- **Months on a charge.** Signs sleep through nights and weekends, and only wake to redraw when something has changed.
- **A reception screen too.** Guests scan a QR code to join the guest Wi-Fi, a welcome names today's visitor, and the office's air quality and pollen sit beside it.

<p><img src="docs/images/reception-builder.png" width="800" alt="Switchboard Server's layout builder with a reception screen: a guest Wi-Fi QR code, a welcome message and the office's air quality in the live preview, with the screen's sections listed below"></p>
<sub>Setting up a reception screen in Switchboard Server: the live preview above, each column's sections below.</sub>

See [Viewports in the office](https://stumarti.github.io/Switchboard/manual/viewport/office.html).

## The server

[**Switchboard Server**](https://github.com/stumarti/Switchboard-Server) is a small Docker container on your network, and it holds every device's whole setup. Build a room's remote or a wall display's screens in the browser, with a **live preview from Home Assistant**, and every device picks it up on its own.

<p><img src="docs/manual/images/admin/home.png" width="760" alt="The server's Home page: every device, its battery and days left"></p>

- **Every device on one page:** battery and days left, Wi-Fi signal, firmware, and anything that needs attention.
- **Approve devices with one click.** They find the server by themselves (mDNS), so there are no addresses to type.
- **Firmware updates for everything:** update one device at a time or all at once, from GitHub releases.
- **Make them yours** with themed icons and fonts.

## Get started

1. **Run the server.** Follow the [Switchboard Server README](https://github.com/stumarti/Switchboard-Server): with `docker compose up -d`, it takes a couple of minutes. Connect it to Home Assistant and create a room (for a remote) or a viewport layout.
2. **Flash the device** from the [browser flasher](https://stumarti.github.io/Switchboard/) over USB (Chrome or Edge, on a desktop). It has a button for the remote and one for the viewport.
3. **Put it on your Wi-Fi.** The remote has an on-screen keyboard; the viewport shows two QR codes for your phone.
4. **Approve it** on the server, and pick its room or layout. That's it.

The [Getting started](https://stumarti.github.io/Switchboard/manual/getting-started.html) guide walks through it with screenshots. Would you rather build the firmware yourself? See [Building the firmware](https://stumarti.github.io/Switchboard/manual/remote/building.html).

## The manual

**[The Switchboard manual](https://stumarti.github.io/Switchboard/manual/)** covers the remote, the viewport and the server, with a screenshot of every screen. It's served from [`docs/manual/`](docs/manual/) with GitHub Pages.

- **The remote:** [screens](https://stumarti.github.io/Switchboard/manual/remote/screens.html), [Quick Access and the shade](https://stumarti.github.io/Switchboard/manual/remote/navigation.html), [settings](https://stumarti.github.io/Switchboard/manual/remote/settings.html), [updates](https://stumarti.github.io/Switchboard/manual/remote/updates.html)
- **The viewport:** [setting up](https://stumarti.github.io/Switchboard/manual/viewport/setup.html), [screens](https://stumarti.github.io/Switchboard/manual/viewport/screens.html), [buttons, refresh and sleep](https://stumarti.github.io/Switchboard/manual/viewport/buttons.html), [updates](https://stumarti.github.io/Switchboard/manual/viewport/updates.html)
- **The server:** [Home](https://stumarti.github.io/Switchboard/manual/server/home.html), [remote layouts](https://stumarti.github.io/Switchboard/manual/server/remote-layouts.html), [viewport layouts](https://stumarti.github.io/Switchboard/manual/server/viewports.html), [battery life](https://stumarti.github.io/Switchboard/manual/server/battery-life.html)
- **Builders:** [building the firmware](https://stumarti.github.io/Switchboard/manual/remote/building.html), [releases](https://stumarti.github.io/Switchboard/manual/remote/releases.html), [the demo](https://stumarti.github.io/Switchboard/manual/demo.html)

## The repositories

| | |
|---|---|
| **Switchboard** (this one) | The remote's firmware, the browser flasher and the manual |
| [Switchboard Server](https://github.com/stumarti/Switchboard-Server) | The server: setup, Home Assistant, updates, the admin UI |
| [Switchboard Viewport](https://github.com/stumarti/Switchboard-Viewport) | The viewport's firmware |
