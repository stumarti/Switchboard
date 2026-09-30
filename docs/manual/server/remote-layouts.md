# Remote layouts

Every UI is defined on the server, before any device exists: dumb hardware, server control. A device is only ever *assigned* one: a remote to a room, a viewport to a [viewport layout](viewports.md).

<figure class="shot"><img src="../images/admin/layouts.png" alt="The Layouts page"><figcaption><b>Layouts</b>: remote layouts (one per room) at the top, viewport layouts below, each with the devices using it.</figcaption></figure>

A **remote layout** is one room: its Home Assistant entities, one card per page, and what its remotes show. Entity fields search Home Assistant as you type (through the server, which holds the token), show each entity's state, flag ids Home Assistant doesn't know, and fill in names, icons and which light controls a light supports. With Home Assistant unreachable they're plain text boxes.

## The carousel

<figure class="shot"><img src="../images/admin/remote-layout-carousel.png" alt="The Carousel card"><figcaption>The pages the room's remotes show, in order. Drag to reorder; switch one off to skip it; click one to set it up.</figcaption></figure>

The same card sets how often remotes refresh, and **On the clock**: a 15, 30 or 60 minute refresh lands on the hour and its halves or quarters, each remote a few seconds after the last.

## The pages

Each page has its own card: the Status page's weather and indoor climate, then lighting, blinds, music, TV, Xbox, the receiver and climate.

<figure class="shot"><img src="../images/admin/remote-layout-lighting.png" alt="The Lighting card"><figcaption>Lighting: the room's light group and which controls it has, each light, and the scenes.</figcaption></figure>

- **Lighting:** a group for the whole room, with brightness, colour temperature, colour and effects switched on as the lights support them, then single lights and scenes, each with an optional icon.
- **Blinds:** a group for the whole room, then each blind.
- **Music:** the media player.
- **TV:** the TV's media player and remote, and up to four apps (a name, the app to launch, an icon).
- **Xbox:** the console's media player and remote, and its library: a list here, or the console's own, which the server reads from Home Assistant (up to 36 games, refreshed every 30 minutes).
- **Receiver:** an Enigma2 box's media player and up to six favourite channels. It works from Home Assistant's Enigma2 integration alone (channel, programme on now, and the channel's picon if the integration's "Use channel icon" is on). Give the room the box's address (its OpenWebif, such as `http://192.168.1.50` or `http://root:password@vu.local`) and the server also reads the programme on next and each favourite's picon; **Check** shows what the box reports. The address stays on the server: remotes never see it.
- **Climate:** the thermostat, and any other temperature sensors to show.

## Settings on the remote

Under **Refresh & settings**, **Developer menu** (on by default) says whether the room's remotes have the Developer menu in their Settings: the pixel grid, stay awake, the Quick Access strips switch, the button checker, the error screens and the hard reset. Switch it off for remotes that aren't for tinkering. Hidden, those switches stop counting too, so nobody can leave a remote awake, or with a grid on it, by a switch they can't get back to. A remote customised on its own page has its own switch. It takes effect at each remote's next refresh.

## Quick Access

<figure class="shot"><img src="../images/admin/remote-layout-quick-access.png" alt="The Quick Access card"><figcaption>The buttons a Home tap shows on the remote.</figcaption></figure>

Each button opens a page, and can have a quick action in its bottom strip: **toggle** an entity, or **run** a Home Assistant service (with optional JSON data). Without any buttons, remotes show the built-in Jump to grid. See [Quick Access](../remote/navigation.md) for how they look on the remote.

## Screen previews

<figure class="shot"><img src="../images/admin/remote-layout-previews.png" alt="Screen previews"><figcaption>What a remote in this room would show, page by page, from the room as it stands in the editor (saved or not) and Home Assistant now.</figcaption></figure>

At the bottom of the editor, a preview of each page, close to what the remote draws. Click one to jump to its card.
