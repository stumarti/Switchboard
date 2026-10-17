# Viewport layout reference

A [viewport layout](viewports.md) is kept on the server as one JSON object: its timing, its thresholds and its screens, and in each screen its sections. This page lists every key in it, what it holds and what it does, so you can edit a layout as JSON.

**Where to edit it.**

- **One section:** click it in the builder, and its settings drawer has a **JSON** tab beside **Settings**. The section's `id` stays as it is.
- **The whole layout:** **Edit as JSON** in the layout's side column.

**Apply** sends the JSON to the server, which checks it: unknown keys are dropped, missing ones get their defaults, numbers out of range are brought into range, and a value that isn't one of a key's choices falls back to its default. What comes back goes into the builder and its preview. **Save** keeps it, as with any other change. In the whole-layout editor, a top-level key you leave out gets its default, not its current value.

**Entities.** While you type inside a string where an entity goes (under a key such as `entity`, `entities`, `sensor`, `battery`, `weather`, `calendar` or `climate`, or any string with a dot in it), the editor suggests matching Home Assistant entities: pick one with the arrow keys and Enter or Tab, or click it; Esc closes the list. Once the JSON is valid, every entity it names is looked up, and any Home Assistant hasn't got are listed under the editor.

The editor also has **Revert** (back to what the builder has), **Copy**, and Tab to indent.

A remote layout's JSON is documented in [Remote layout format](profile-format.md).

**How to read the tables.** *Type* is the JSON type: string, number, boolean, object or list. Where a key takes one of a few values, they're listed. *Default* is what the server fills in when the key is missing or not valid. An empty string (`""`) means "not set". Colours are numbers from 0 to 5 (see [Colours](#colours)). Icons are [Material Design Icons](https://pictogrammers.com/library/mdi/) names, such as `door-open`.

## The layout

| Key | Type | Default | Meaning |
|---|---|---|---|
| `version` | number | `2` | The layout format. The server always writes `2`. |
| `refreshIntervalMin` | `5`, `10`, `15`, `30` or `60` | `30` | Minutes between refreshes, outside quiet hours. |
| `refreshAligned` | boolean | `false` | On the clock: refresh at the interval's marks (:00 and :30 for 30 minutes, counted from midnight), each display a few seconds after the last, instead of an interval after it last slept. |
| `rotation` | `0`, `90`, `180` or `270` | `0` | How the display hangs: degrees clockwise from its usual landscape. `90` and `270` are portrait (480 × 800). |
| `boardSize` | `"large"` or `"small"` | `"large"` | On a 13.3-inch board (the E1004): `"large"` draws everything twice the size; `"small"` draws at the board's full resolution, with four times the room. |
| `carousel` | object | | What the display does between button presses. |
| `carousel.mode` | `"stay"`, `"advance"` or `"returnFirst"` | `"stay"` | `"stay"` refreshes the screen it's on; `"advance"` moves to the next screen every `everyMin`; `"returnFirst"` goes back to the first screen every `everyMin`. |
| `carousel.everyMin` | number, 5–240 | `30` | Minutes, for `"advance"` and `"returnFirst"`. |
| `quietHours` | object | | When the display wakes less often. See [Buttons, refresh and sleep](../viewport/buttons.md#quiet-hours). |
| `quietHours.enabled` | boolean | `false` | Quiet hours on or off. |
| `quietHours.start` | number, 0–23 | `23` | The hour (local time) quiet hours start. |
| `quietHours.end` | number, 0–23 | `6` | The hour they end. It can be earlier than `start`: the span wraps past midnight. |
| `quietHours.intervalMin` | `30`, `60`, `120` or `240` | `60` | Minutes between wakes during quiet hours. |
| `quietHours.weekends` | boolean | `false` | All of Saturday and Sunday quiet too (for an office). |
| `thresholds` | object | | Numbers the sections share. |
| `thresholds.batteryCritical` | number | `10` | Home battery: at or below this %, critical (and its `critical` colour, when the status comes from a status sensor). |
| `thresholds.batteryLow` | number | `20` | Home battery: at or below this %, low. |
| `thresholds.transportUrgentMin` | number | `5` | Departures: red when leaving within this many minutes. The display also wakes when the next departure turns red. |
| `thresholds.motionRecentMin` | number | `30` | Motion: blue when the sensor changed within this many minutes (or is on). |
| `thresholds.climateTolerance` | number | `1` | Room climate: degrees either side of the target that count as at target. |
| `screens` | list of [screens](#screens) | one empty screen called *Home* | The screens the buttons step through, in order. Up to 12. |

A small layout:

```json
{
  "version": 2,
  "refreshIntervalMin": 30,
  "refreshAligned": false,
  "rotation": 0,
  "boardSize": "large",
  "carousel": { "mode": "returnFirst", "everyMin": 30 },
  "quietHours": { "enabled": true, "start": 23, "end": 6, "intervalMin": 60, "weekends": false },
  "thresholds": {
    "batteryCritical": 10,
    "batteryLow": 20,
    "transportUrgentMin": 5,
    "motionRecentMin": 30,
    "climateTolerance": 1
  },
  "screens": [
    {
      "id": "home",
      "title": "Home",
      "icon": "view-dashboard-outline",
      "enabled": true,
      "kind": "sections",
      "template": "sidebar",
      "columns": [[], []],
      "background": {
        "enabled": false,
        "source": { "kind": "favorites", "album": "", "person": "" },
        "every": 60,
        "order": "random",
        "caption": "none",
        "cards": "white"
      }
    }
  ]
}
```

## Screens

Every screen has these keys:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `id` | string | a random id | The screen's id, up to 64 characters. Keep it when editing: devices ask for screens by it. |
| `title` | string | `""` | The screen's name. |
| `icon` | string | by kind (below) | Its mark in the display's footer, beside the other screens'. |
| `enabled` | boolean | `true` | Off: the display skips it. |
| `kind` | `"sections"`, `"meetingRoom"`, `"roomFinder"` or `"photoFrame"` | `"sections"` | What the screen is. The rest of its keys depend on it. |

The default icons are `view-dashboard-outline` (sections), `calendar-account-outline` (meeting room), `door-sliding-open` (room finder) and `image-frame` (photo frame).

### Sections screen (`sections`)

| Key | Type | Default | Meaning |
|---|---|---|---|
| `template` | `"sidebar"`, `"columns"`, `"triple"` or `"single"` | `"sidebar"` | How the screen is split into columns (below). |
| `columns` | list of lists of [sections](#sections) | | One list per column, top to bottom. Up to 8 sections in each. |
| `background` | object | | A photo behind the whole screen (below). |

| Template | Columns | Widths on the 800-pixel panel |
|---|---|---|
| `sidebar` | Sidebar, Main | 250 px and 550 px |
| `columns` | Left, Right | 400 px each |
| `triple` | Left, Middle, Right | about 267 px each |
| `single` | one | 800 px |

If `columns` has more lists than the template has columns, the extra sections are added to the last column (up to 16 there). A section whose `type` isn't one of the [section types](#sections) is dropped.

**The background.** A photo from Immich behind the sections (see [Photos from Immich](../viewport/photos.md)).

| Key | Type | Default | Meaning |
|---|---|---|---|
| `background.enabled` | boolean | `false` | A photo behind this screen. |
| `background.source` | object | favourites | Where the photos come from: see [Photo sources](#photo-sources). |
| `background.every` | number, 5–10080 | `60` | Minutes between photos. |
| `background.order` | `"random"` or `"sequential"` | `"random"` | `"random"`: any photo each time. `"sequential"`: one after another, oldest first, and round again at the end. |
| `background.caption` | `"none"`, `"date"`, `"place"` or `"both"` | `"none"` | A caption: when the photo was taken, where, or both. |
| `background.cards` | `"white"` or `"none"` | `"white"` | `"white"`: each section on a white card over the photo. `"none"`: the sections straight on the photo, with a white edge round what they draw. |

```json
{
  "id": "home",
  "title": "Home",
  "icon": "view-dashboard-outline",
  "enabled": true,
  "kind": "sections",
  "template": "columns",
  "columns": [[], []],
  "background": {
    "enabled": true,
    "source": { "kind": "memories", "album": "", "person": "" },
    "every": 60,
    "order": "sequential",
    "caption": "none",
    "cards": "none"
  }
}
```

### Meeting room (`meetingRoom`)

One room's bookings, filling the screen. Its settings are under `meeting`. See [Viewports in the office](../viewport/office.md).

| Key | Type | Default | Meaning |
|---|---|---|---|
| `meeting.calendar` | string | `""` | The room's calendar: a Home Assistant calendar entity, or a calendar link (an iCal address). Up to 2000 characters. |
| `meeting.name` | string | `""` | The room's name. Blank: the calendar's own name. |
| `meeting.occupancy` | string | `""` | An occupancy sensor. `on`, `home`, `detected` or `occupied` count as someone there. |
| `meeting.hideTitles` | boolean | `false` | Show every meeting as *Booked*. |
| `meeting.soonMin` | number, 0–60 | `10` | *Starting soon* this many minutes before a meeting, between meetings. |
| `meeting.emptyMin` | number, 0–60 | `10` | With an occupancy sensor: *booked but empty* once a meeting has run this long with no one there for this long. |
| `meeting.upcoming` | number, 0–8 | `4` | How many of today's later meetings to list. |
| `meeting.freeIcon` | string | `"door-open"` | The status icon while free. |
| `meeting.occupiedIcon` | string | `"account-group"` | The status icon while in use (booked or not). Starting soon and booked but empty have fixed icons. |
| `meeting.labels` | object | below | The status bar's words for each state, up to 40 characters each. A blank one gets its default. |
| `meeting.timelineHours` | `0`, `1`, `2` or `3` | `2` | The timeline of the next hours under the status. `0`: no timeline. |
| `meeting.climate.show` | boolean | `false` | The room's climate in the bottom-right corner. |
| `meeting.climate.temperature` | string | `""` | A thermostat (`climate.*`, which also gives humidity) or a temperature sensor. |
| `meeting.climate.humidity` | string | `""` | A humidity sensor. |
| `meeting.climate.co2` | string | `""` | A CO2 sensor: yellow from 1000 ppm, red from 1500. |
| `meeting.aheadMin` | number, 0–10 | `2` | Change the sign this many minutes before a meeting starts or ends. See [When a sign changes](../viewport/office.md#when-a-sign-changes). |

The labels' keys and defaults: `free` *Available*, `soon` *Starting soon*, `busy` *In use*, `bookedEmpty` *Booked — no one here*, `occupied` *In use — not booked*.

```json
{
  "id": "meeting",
  "title": "Boardroom",
  "icon": "calendar-account-outline",
  "enabled": true,
  "kind": "meetingRoom",
  "meeting": {
    "calendar": "calendar.boardroom",
    "name": "",
    "occupancy": "binary_sensor.boardroom_occupancy",
    "hideTitles": false,
    "soonMin": 10,
    "emptyMin": 10,
    "upcoming": 4,
    "freeIcon": "door-open",
    "occupiedIcon": "account-group",
    "labels": {
      "free": "Available",
      "soon": "Starting soon",
      "busy": "In use",
      "bookedEmpty": "Booked — no one here",
      "occupied": "In use — not booked"
    },
    "timelineHours": 2,
    "climate": { "show": true, "temperature": "climate.boardroom", "humidity": "", "co2": "sensor.boardroom_co2" },
    "aheadMin": 2
  }
}
```

### Room finder (`roomFinder`)

The other rooms, free ones first, the longest free at the top. Its settings are under `finder`.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `finder.rooms` | list | `[]` | Up to 12 rooms, each with `id`, `calendar` (an entity or a calendar link), `name` (blank: the calendar's name) and `occupancy` (an occupancy sensor, or `""`). |
| `finder.showBusy` | boolean | `true` | List busy rooms after the free ones. Off: leave them out. |
| `finder.soonMin` | number, 0–60 | `10` | As for a meeting room. |
| `finder.emptyMin` | number, 0–60 | `10` | As for a meeting room. |
| `finder.freeIcon` | string | `"door-open"` | As for a meeting room. |
| `finder.occupiedIcon` | string | `"account-group"` | As for a meeting room. |
| `finder.labels` | object | below | As for a meeting room, but `free` and `soon` both default to *Free*. |
| `finder.aheadMin` | number, 0–10 | `2` | As for a meeting room. |

Meeting titles are never shown on a room finder.

```json
{
  "id": "rooms",
  "title": "Other rooms",
  "icon": "door-sliding-open",
  "enabled": true,
  "kind": "roomFinder",
  "finder": {
    "rooms": [
      { "id": "room-1", "calendar": "calendar.huddle_room", "name": "Huddle", "occupancy": "" }
    ],
    "showBusy": true,
    "soonMin": 10,
    "emptyMin": 10,
    "freeIcon": "door-open",
    "occupiedIcon": "account-group",
    "labels": {
      "free": "Free",
      "soon": "Free",
      "busy": "In use",
      "bookedEmpty": "Booked — no one here",
      "occupied": "In use — not booked"
    },
    "aheadMin": 2
  }
}
```

### Photo frame (`photoFrame`)

A photo from Immich filling the screen, with a few lines written over it in one corner: in white, in black, or in whichever contrasts with the photo under them. Its settings are under `frame`. Each line is left out when its key is off or empty.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `frame.source` | object | favourites | Where the photos come from: see [Photo sources](#photo-sources). |
| `frame.every` | number, 5–10080 | `60` | Minutes between photos. |
| `frame.order` | `"random"` or `"sequential"` | `"random"` | As for a background. |
| `frame.caption` | `"none"`, `"date"`, `"place"` or `"both"` | `"none"` | When and where the photo was taken, last and small. |
| `frame.corner` | `"bottomLeft"`, `"bottomRight"`, `"topLeft"` or `"topRight"` | `"bottomLeft"` | Where the lines go. |
| `frame.size` | `"normal"` or `"large"` | `"normal"` | The size of the lines. |
| `frame.ink` | string | `white` | The lines' colour: `white`, `black`, or `auto`. With `auto` the server looks at each photo where the lines go: black on a light photo, white on a dark one, with no edge. If it can't tell (Immich slow to answer), it uses white with an edge. |
| `frame.outline` | boolean | `true` | An edge round the text and icons in the other colour (black round white, white round black), so it reads on any photo. It covers the lines, the corner note and the footer. Not used with `auto`. |
| `frame.footer` | boolean | `false` | The display's footer in the bottom right: the screens' icons, the time and the battery. It takes the frame's text colour and outline, and the battery is drawn in that colour too, red only when it's low. With `auto`, the server looks at the photo under the footer separately. Lines or a note in the bottom right go above it. Off, the battery shows only when it's low. |
| `frame.date` | boolean | `true` | Today's date, first and large. |
| `frame.weather` | string | `""` | A weather entity: its temperature and conditions. |
| `frame.calendars` | list of strings | `[]` | Up to 8 calendars (entities or calendar links): the next event today or tomorrow. |
| `frame.message` | string | `""` | Your own words, up to 300 characters. `{entity_id}` is filled in as in a [Message](#message). |
| `frame.note` | object | none | A corner note of your own: `icon` (an icon name), `text` (one line, up to 120 characters, `{entity_id}` filled in) and `corner` (`topRight` by default; `bottomLeft`, `bottomRight`, `topLeft`). Either can be left blank. In the lines' own corner it becomes their last line. It takes the frame's text colour and outline. |

```json
{
  "id": "frame",
  "title": "Photos",
  "icon": "image-frame",
  "enabled": true,
  "kind": "photoFrame",
  "frame": {
    "source": { "kind": "favorites", "album": "", "person": "" },
    "every": 60,
    "order": "random",
    "caption": "none",
    "corner": "bottomLeft",
    "size": "normal",
    "ink": "white",
    "outline": true,
    "date": true,
    "weather": "weather.home",
    "calendars": ["calendar.family"],
    "message": ""
  }
}
```

### Photo sources

A photo section, a background and a photo frame each have a `source`:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `kind` | `"album"`, `"favorites"`, `"person"`, `"memories"` or `"random"` | `"favorites"` | `"album"`: one album's photos. `"favorites"`: your favourites. `"person"`: one recognised person's photos. `"memories"`: photos taken on today's date in earlier years (favourites on a day with none). `"random"`: any photo in the library. |
| `album` | string | `""` | For `"album"`: the Immich album's id. |
| `person` | string | `""` | For `"person"`: the Immich person's id. |

## Common to every section

Every section has these keys, as well as its own:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `id` | string | a random id | The section's id, up to 64 characters. |
| `type` | string | (required) | One of the [section types](#sections). A section without a known type is dropped. |
| `title` | string | `""` | The section's heading. Blank: none. |
| `showWhen` | object | always | When the section shows at all (below). |

**`showWhen`.** A hidden section is left out and the rest of its column closes up.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `showWhen.mode` | `"always"`, `"playing"` or `"entity"` | `"always"` | `"always"`: always shown. `"playing"`: only while one of its players is playing (Now playing sections only; on any other type it becomes `"always"`). `"entity"`: only while an entity's state matches. |
| `showWhen.entity` | string | `""` | For `"entity"`: the entity whose state is checked. |
| `showWhen.cond` | [condition](#conditions) | `"eq"` | For `"entity"`: how the state is compared. |
| `showWhen.value` | string | `""` | For `"entity"`: what it's compared with. |
| `showWhen.liveMin` | number, 0–30 | `3` (`0` for `"always"`) | While the section shows, the display wakes this often (minutes) to keep it current. `0`: no extra wakes. Always `0` with `"always"`. |

A calendar in the same column as a conditional section that's showing drops to its `shrinkTo` lines.

An alarm section shown only while the alarm is armed. Typed like this, with the other keys left out, **Apply** fills them in:

```json
{
  "type": "alarm",
  "entity": "alarm_control_panel.home",
  "showWhen": { "mode": "entity", "entity": "alarm_control_panel.home", "cond": "startsWith", "value": "armed", "liveMin": 3 }
}
```

## Sections

One part per section type. Each example is what the server keeps, with entity ids filled in.

<a id="weather"></a>
### Weather (`weather`)

The weather now, "later" and the next days, and optionally when rain starts or stops.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `entity` | string | `""` | The weather entity. |
| `later` | boolean | `true` | A *Later* forecast, about three hours ahead, from the hourly forecast. |
| `days` | number, 0–5 | `2` | How many days of the daily forecast, from tomorrow. |
| `hourlyEntity` | string | `""` | Another weather entity for the hourly forecast. Blank: `entity`. |
| `rain` | boolean | `false` | *Rain at 14:00*, *Rain stops 16:00* or *Rain continuing*: the first change between wet and dry in the hourly forecast. |
| `rainHours` | number, 1–24 | `12` | How many hours ahead to look for rain. |
| `rainThresholdMm` | number, 0–10 | `0.1` | An hour is wet above this many mm. |
| `solarForecast` | string | `""` | A solar forecast sensor (kWh): *Expecting 12.3kWh today*. |

```json
{
  "id": "weather-1",
  "type": "weather",
  "title": "",
  "entity": "weather.home",
  "later": true,
  "days": 2,
  "hourlyEntity": "",
  "rain": true,
  "rainHours": 12,
  "rainThresholdMm": 0.1,
  "solarForecast": "sensor.solar_forecast_today",
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="energy"></a>
### Energy totals (`energy`)

Today's solar, house use and grid figures, to one decimal place.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `style` | `"grid"` or `"list"` | `"grid"` | `"grid"`: a 2 × 2 block of tiles. `"list"`: one row per figure (for a sidebar). |
| `solarToday` | string | `""` | Solar generated today. |
| `solarExpected` | string | `""` | Solar predicted for today. With `solarToday`, also gives the percentage of the prediction reached. |
| `loadToday` | string | `""` | House use today. |
| `gridExport` | string | `""` | Exported to the grid today. |
| `gridImport` | string | `""` | Imported from the grid today. |

```json
{
  "id": "energy-1",
  "type": "energy",
  "title": "",
  "style": "grid",
  "solarToday": "sensor.solar_energy_today",
  "solarExpected": "sensor.solar_forecast_today",
  "loadToday": "sensor.home_consumption_today",
  "gridExport": "sensor.grid_export_today",
  "gridImport": "sensor.grid_import_today",
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="energygraph"></a>
### Energy graph (`energyGraph`)

Two panels: actual solar against the forecast, and house use stacked by source (solar, battery, grid), with battery charging and grid export below the line.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `range` | `"today"` or `"24h"` | `"today"` | Today from midnight, or the last 24 hours. |
| `bucketMin` | `15`, `30` or `60` | `60` | Minutes per bar. |
| `solar` | series | | Solar production. |
| `load` | series | | House use. |
| `gridImport` | series | | Grid import. |
| `gridExport` | series | | Grid export. |
| `batteryCharge` | series | | Battery charging. |
| `batteryDischarge` | series | | Battery discharging. |
| `batteryPower` | series | | One signed battery power sensor, in place of `batteryCharge` and `batteryDischarge`. |
| `batteryChargingWhen` | `"negative"` or `"positive"` | `"negative"` | Which sign `batteryPower` has while charging. |
| `forecast.entity` | string | `""` | The entity holding the solar forecast. |
| `forecast.attribute` | string | `""` | Its attribute with the hourly or half-hourly list, such as Solcast's `detailedForecast`. |
| `forecast.unit` | `"auto"`, `"kW"` or `"W"` | `"auto"` | The forecast's unit. `"auto"`: watts if any value is over 100. |
| `colors` | object | below | A colour for each part of the graph. |

Each series is `{ "entity": "", "kind": "power" }`: `kind` is `"power"` for a power sensor (W or kW, averaged per bar) or `"energy"` for an energy meter (kWh, differenced per bar). An empty `entity` leaves that series out.

The `colors` keys and defaults: `solar` 3 (the actual solar bars), `forecast` 5 (the forecast line), `fromSolar` 3, `fromBattery` 5, `fromGrid` 2, `gridExport` 4, `toBattery` 5.

```json
{
  "id": "energy-graph-1",
  "type": "energyGraph",
  "title": "",
  "range": "today",
  "bucketMin": 60,
  "solar": { "entity": "sensor.solar_power", "kind": "power" },
  "load": { "entity": "sensor.home_power", "kind": "power" },
  "gridImport": { "entity": "sensor.grid_import_power", "kind": "power" },
  "gridExport": { "entity": "sensor.grid_export_power", "kind": "power" },
  "batteryCharge": { "entity": "", "kind": "power" },
  "batteryDischarge": { "entity": "", "kind": "power" },
  "batteryPower": { "entity": "sensor.battery_power", "kind": "power" },
  "batteryChargingWhen": "negative",
  "forecast": { "entity": "sensor.solcast_forecast_today", "attribute": "detailedForecast", "unit": "auto" },
  "colors": { "solar": 3, "forecast": 5, "fromSolar": 3, "fromBattery": 5, "fromGrid": 2, "gridExport": 4, "toBattery": 5 },
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="battery"></a>
### Home battery (`battery`)

The battery's charge and whether it's charging, discharging or idle, with when it will be full or empty.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `soc` | string | `""` | The state of charge sensor (%). |
| `status` | string | `""` | A status sensor reading `charging`, `discharging`, `full` and so on. Not used when `power` is set. |
| `eta` | string | `""` | With `status`: a sensor whose text is shown as it is (such as the time to full). |
| `power` | string | `""` | A battery power sensor (W or kW). Set, the status comes from it instead of `status`. |
| `idleWatts` | number, 0–10000 | `100` | With `power`: idle within this many watts either side of zero. |
| `chargingWhen` | `"negative"` or `"positive"` | `"negative"` | With `power`: which sign it has while charging. |
| `chargeEta` | string | `""` | With `power`, while charging: *full at 16:20*, from a timestamp sensor, an `input_datetime` or the time left (s, min, h, d, or `1:45:00`). |
| `dischargeEta` | string | `""` | The same while discharging: *empty at 04:05*. |
| `colors` | object | below | The colour for each status. |

The `colors` keys and defaults: `charging` 4, `full` 4, `discharging` 3, `critical` 2, `idle` 1. `critical` is used at or below `thresholds.batteryCritical`, only when the status comes from a status sensor.

```json
{
  "id": "battery-1",
  "type": "battery",
  "title": "",
  "soc": "sensor.battery_soc",
  "status": "",
  "eta": "",
  "power": "sensor.battery_power",
  "idleWatts": 100,
  "chargingWhen": "negative",
  "chargeEta": "sensor.battery_charge_eta",
  "dischargeEta": "sensor.battery_discharge_eta",
  "colors": { "charging": 4, "full": 4, "discharging": 3, "critical": 2, "idle": 1 },
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="statusicons"></a>
### Status icons (`statusIcons`)

A row of icons, each following an entity (or several), coloured, changed or hidden by its rules.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `icons` | list | `[]` | Up to 12 icons. Each icon's keys are in [Status icons and rules](#status-icons-and-rules). |

```json
{
  "id": "status-icons-1",
  "type": "statusIcons",
  "title": "",
  "icons": [
    {
      "id": "icon-1",
      "name": "Front door",
      "entity": "binary_sensor.front_door",
      "entities": [],
      "attribute": "",
      "icon": "door-closed",
      "color": 1,
      "showByDefault": true,
      "rules": [
        { "cond": "eq", "value": "on", "entity": "", "attribute": "", "also": null, "color": 2, "icon": "door-open", "hide": false }
      ]
    }
  ],
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="now"></a>
### Now (`now`)

What needs attention, as coloured two-line items: one for each thing that's active.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `items` | list | `[]` | Up to 12 items, each of one kind. Their keys are in ["Now" items](#now-items). |

```json
{
  "id": "now-1",
  "type": "now",
  "title": "Now",
  "items": [
    {
      "id": "now-heating",
      "kind": "heating",
      "name": "",
      "icon": "radiator",
      "color": 2,
      "title": "Heating",
      "titleMany": "",
      "detail": "{n} zones heating",
      "entity": "",
      "eventEntity": "",
      "battery": "",
      "attribute": "",
      "cond": "eq",
      "value": "",
      "items": [{ "id": "zone-1", "name": "Kitchen", "entity": "climate.kitchen" }],
      "join": "list",
      "callingDelta": 0.5,
      "maxChars": 50
    }
  ],
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="heating"></a>
### Heating (`heating`)

The heating on or off, flooded red while any zone calls, how many are calling, each zone's bar against its setpoint, and the hot water.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `entity` | string | `""` | A whole-house thermostat: its mode, and its temperature and setpoint with `houseTemps`. |
| `zones` | list | `[]` | Up to 16 zones, each with `id`, `name`, `entity` (its thermostat, `climate.*`) and `icon` (drawn beside the name, or `""`). |
| `hotWater` | string | `""` | A water heater (`water_heater.*`): its temperature, target, and whether it's heating. |
| `callingDelta` | number, 0–5 | `0.5` | A zone is calling when it's in `heat` or `auto` and more than this many degrees below its target. |
| `houseTemps` | boolean | `true` | Show the whole-house thermostat's temperature and setpoint. Only `false` turns it off. |

```json
{
  "id": "heating-1",
  "type": "heating",
  "title": "",
  "entity": "climate.whole_house",
  "zones": [
    { "id": "zone-1", "name": "Kitchen", "entity": "climate.kitchen", "icon": "fridge-outline" }
  ],
  "hotWater": "water_heater.home_tank",
  "callingDelta": 0.5,
  "houseTemps": true,
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="alerts"></a>
### Alert lines (`alerts`)

A sentence for each slot that matches, such as *Front door, Garage +1 open*, or *All clear*.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `slots` | list | `[]` | Up to 12 slots (below). |
| `maxLines` | number, 1–10 | `6` | At most this many lines. |
| `allClear` | string | `"All clear"` | What it says when no slot matches. |

Each slot:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `id` | string | a random id | The slot's id. |
| `enabled` | boolean | `true` | Off: the slot is skipped. |
| `suffix` | string | `""` | The words after the names, such as `open`. |
| `cond` | [condition](#conditions) | `"eq"` | How each entity's state is compared. |
| `value` | string | `"on"` | What it's compared with. |
| `color` | [colour](#colours) | `2` | The line's colour. |
| `entities` | list | `[]` | Up to 8 entities, each `{ "entity": "", "name": "" }`. A blank name uses the entity's friendly name. |

The line names the first two matching entities and counts the rest (*+2*), and is cut to 42 characters.

```json
{
  "id": "alerts-1",
  "type": "alerts",
  "title": "",
  "slots": [
    {
      "id": "alert-1",
      "enabled": true,
      "suffix": "open",
      "cond": "eq",
      "value": "on",
      "color": 2,
      "entities": [
        { "entity": "binary_sensor.front_door", "name": "Front door" },
        { "entity": "binary_sensor.garage_door", "name": "Garage" }
      ]
    }
  ],
  "maxLines": 6,
  "allClear": "All clear",
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="calendar"></a>
### Calendar (`calendar`)

Upcoming events from one or more calendars, each in its calendar's colour; or just today's.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `entities` | list of strings | `[]` | Up to 8 calendars: Home Assistant calendar entities or calendar links. |
| `colors` | object | `{}` | A [colour](#colours) for each calendar, keyed by its entity or link. One not listed is black. |
| `days` | number, 1–31 | `7` | How many days ahead to look. |
| `lines` | number, 1–20 | `4` | At most this many events. |
| `today` | boolean | `false` | Only today's events, including those already over. |
| `timedFirst` | boolean | `false` | Timed events first, by time, then all-day ones. |
| `descriptions` | boolean | `false` | The first words of each event's description under it. |
| `shrinkTo` | number, 0–8 | `2` | While a conditional section in the same column shows, at most this many lines. `0`: keep them all. |

```json
{
  "id": "calendar-1",
  "type": "calendar",
  "title": "",
  "entities": ["calendar.family", "calendar.work"],
  "colors": { "calendar.family": 1, "calendar.work": 5 },
  "days": 7,
  "lines": 4,
  "today": false,
  "timedFirst": false,
  "descriptions": false,
  "shrinkTo": 2,
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="heatpump"></a>
### Heat pump (`heatPump`)

The heat pump's mode and action, its temperature and setpoint, the outside temperature and the COP.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `entity` | string | `""` | The heat pump (`climate.*`). |
| `outsideTemperature` | string | `""` | An outside temperature sensor (or an entity with a `temperature` attribute, such as a weather entity). |
| `cop` | string | `""` | A COP sensor. |

```json
{
  "id": "heat-pump-1",
  "type": "heatPump",
  "title": "",
  "entity": "climate.heat_pump",
  "outsideTemperature": "sensor.outside_temperature",
  "cop": "sensor.heat_pump_cop",
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="roomclimate"></a>
### Room climate (`roomClimate`)

Each room against its target: red calling for heat, green at target, blue over, black with no target.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `rooms` | list | `[]` | Up to 12 [rooms](#rooms). |

A room is red while its thermostat's `hvac_action` is `heating` and blue while it's `cooling`. Otherwise it's red when more than `thresholds.climateTolerance` below its target, blue when more than that above, and green between.

```json
{
  "id": "room-climate-1",
  "type": "roomClimate",
  "title": "",
  "rooms": [
    {
      "id": "room-1",
      "name": "Living",
      "icon": "sofa",
      "floor": "downstairs",
      "temperature": "sensor.living_temperature",
      "humidity": "sensor.living_humidity",
      "climate": "climate.living_room",
      "target": null
    }
  ],
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="rooms"></a>
**Rooms.** Room climate and Room temperatures use the same room keys:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `id` | string | a random id | The room's id. |
| `name` | string | `""` | Its name. Blank: the entity's friendly name. |
| `icon` | string | `""` | An icon beside it. |
| `floor` | `"upstairs"` or `"downstairs"` | `"downstairs"` | Which group it's in, for `byFloor`. |
| `temperature` | string | `""` | A temperature sensor. Blank: the thermostat's current temperature. |
| `humidity` | string | `""` | A humidity sensor. Blank: the thermostat's current humidity. |
| `climate` | string | `""` | A thermostat or TRV (`climate.*`): the target, and whether it's heating or cooling. |
| `target` | number or `null` | `null` | A fixed target, used when there's no thermostat (or it gives none). |

<a id="roomlist"></a>
### Room temperatures (`roomList`)

Each room's temperature and humidity, grouped by floor.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `rooms` | list | `[]` | Up to 12 [rooms](#rooms). Only their temperature and humidity are shown. |
| `byFloor` | boolean | `true` | Group the rooms by `floor`. |

```json
{
  "id": "room-list-1",
  "type": "roomList",
  "title": "",
  "rooms": [
    {
      "id": "room-1",
      "name": "Main Bed",
      "icon": "bed",
      "floor": "upstairs",
      "temperature": "sensor.main_bed_temperature",
      "humidity": "sensor.main_bed_humidity",
      "climate": "",
      "target": 21
    }
  ],
  "byFloor": true,
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="people"></a>
### People (`people`)

Who is home (green), away, or in which zone.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `people` | list | `[]` | Up to 8 people, each with `id`, `name` (blank: the friendly name) and `entity` (a `person.*` or `device_tracker.*`). |

```json
{
  "id": "people-1",
  "type": "people",
  "title": "",
  "people": [{ "id": "person-1", "name": "Anna", "entity": "person.anna" }],
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="media"></a>
### Now playing (`media`)

What each player is playing or has paused: the app, the title and the artist, with the cover.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `players` | list | `[]` | Up to 3 players, each with `id`, `name` (blank: the friendly name), `entity` (a `media_player.*`) and `icon`. |

This is the only type that can use `showWhen.mode` `"playing"`.

```json
{
  "id": "media-1",
  "type": "media",
  "title": "",
  "players": [{ "id": "player-1", "name": "Kitchen", "entity": "media_player.kitchen", "icon": "speaker" }],
  "showWhen": { "mode": "playing", "entity": "", "cond": "eq", "value": "", "liveMin": 3 }
}
```

<a id="transport"></a>
### Departures (`transport`)

The next two departures per line, red when within `thresholds.transportUrgentMin`.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `routes` | list | `[]` | Up to 4 routes (below). |

Each route reads either a stop sensor with a list of arrivals, or one sensor per departure.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `id` | string | a random id | The route's id. |
| `name` | string | `""` | The line's name, such as `42 · City Centre` or `C3 Maynooth`. With a stop sensor: blank shows a stop board (a line per route and headsign); a route name (`C3`) shows only that route. |
| `stop` | string | `""` | The stop's name, shown with the line. |
| `icon` | string | `"bus"` | The line's icon. |
| `color` | [colour](#colours) | `4` | The route's colour: live times are drawn in it. |
| `departure1` | string | `""` | One sensor per departure: the next one. Its state reads *Due*, minutes (`12`, `12 min`), a time (`17:05`) or a timestamp. |
| `departure1Attribute` | string | `""` | Read this attribute of `departure1` instead of its state. |
| `departure2` | string | `""` | The one after. |
| `departure2Attribute` | string | `""` | Read this attribute of `departure2` instead of its state. |
| `stopEntity` | string | `""` | A stop sensor with every arrival in a list attribute (route, headsign, live and timetabled times). Set, it's used instead of `departure1` and `departure2`. |
| `listAttribute` | string | `"arrivals"` | The stop sensor's list attribute. |
| `routes` | string | `""` | Only these routes from the stop sensor, comma-separated (`C3, 66`). |
| `lines` | number, 1–6 | `3` | An unnamed stop board shows at most this many lines. |

```json
{
  "id": "transport-1",
  "type": "transport",
  "title": "",
  "routes": [
    {
      "id": "route-1",
      "name": "C3",
      "stop": "Main Street",
      "icon": "bus",
      "color": 4,
      "departure1": "",
      "departure1Attribute": "",
      "departure2": "",
      "departure2Attribute": "",
      "stopEntity": "sensor.main_street_stop",
      "listAttribute": "arrivals",
      "routes": "",
      "lines": 3
    },
    {
      "id": "route-2",
      "name": "42 · City Centre",
      "stop": "",
      "icon": "bus",
      "color": 4,
      "departure1": "sensor.bus_42_next",
      "departure1Attribute": "",
      "departure2": "sensor.bus_42_next2",
      "departure2Attribute": "",
      "stopEntity": "",
      "listAttribute": "arrivals",
      "routes": "",
      "lines": 3
    }
  ],
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="alarm"></a>
### Alarm (`alarm`)

The alarm's state and since when, and optionally when it was last armed, disarmed or triggered.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `entity` | string | `""` | The alarm panel (`alarm_control_panel.*`). |
| `summary` | boolean | `false` | Under the badge, a count line for each Doors & windows, Motion and Cameras section on the same screen. |
| `lastArmed` | string | `""` | An entity giving when it was last armed (a timestamp state, else when it last changed). |
| `lastDisarmed` | string | `""` | The same for disarmed. |
| `lastTriggered` | string | `""` | The same for triggered. |

```json
{
  "id": "alarm-1",
  "type": "alarm",
  "title": "",
  "entity": "alarm_control_panel.home",
  "summary": true,
  "lastArmed": "sensor.alarm_last_armed",
  "lastDisarmed": "",
  "lastTriggered": "",
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="openings"></a>
### Doors & windows (`openings`)

Each door, window or gate, open or closed, and when it last changed. `on`, `open`, `opening` and `unlocked` count as open.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `items` | list | `[]` | Up to 10, each with `id`, `name` (blank: the friendly name) and `entity`. |
| `openColor` | [colour](#colours) | `2` | The colour of an open one. |
| `closedColor` | [colour](#colours) | `4` | The colour of a closed one. |

```json
{
  "id": "openings-1",
  "type": "openings",
  "title": "Doors",
  "items": [{ "id": "door-1", "name": "Front door", "entity": "binary_sensor.front_door" }],
  "openColor": 2,
  "closedColor": 4,
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="motion"></a>
### Motion (`motion`)

The last motion per sensor, blue while on or within `thresholds.motionRecentMin`.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `sensors` | list | `[]` | Up to 10, each with `id`, `name` (blank: the friendly name) and `entity`. |

```json
{
  "id": "motion-1",
  "type": "motion",
  "title": "",
  "sensors": [{ "id": "motion-1", "name": "Hall", "entity": "binary_sensor.hall_motion" }],
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="cameras"></a>
### Cameras (`cameras`)

The last motion per camera: the time its entity holds (a timestamp sensor), else when it last changed.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `cameras` | list | `[]` | Up to 10, each with `id`, `name` (blank: the friendly name) and `entity` (a camera's motion sensor or last-motion timestamp). |

```json
{
  "id": "cameras-1",
  "type": "cameras",
  "title": "",
  "cameras": [{ "id": "camera-1", "name": "Front camera", "entity": "binary_sensor.camera_front_motion" }],
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="spacer"></a>
### Spacer (`spacer`)

Nothing: a gap, to move the sections under it down the column.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `height` | number, 0–400 | `20` | The gap, in pixels. |

```json
{
  "id": "spacer-1",
  "type": "spacer",
  "title": "",
  "height": 40,
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="announcements"></a>
### Announcements (`announcements`)

The newest items of an RSS or Atom feed: headline, short summary and when posted. The server reads the feed at most every 10 minutes.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `url` | string | `""` | The feed's address, up to 500 characters. |
| `count` | number, 1–6 | `3` | How many items. |
| `summary` | boolean | `true` | A short summary under each headline. |
| `maxAgeDays` | number, 0–365 | `0` | Leave out items older than this many days. `0`: no limit. |
| `showDate` | boolean | `true` | When each item was posted. |
| `color` | [colour](#colours) | `1` | The colour of the headlines. |

```json
{
  "id": "announcements-1",
  "type": "announcements",
  "title": "",
  "url": "https://intranet.example.com/news/feed.xml",
  "count": 3,
  "summary": true,
  "maxAgeDays": 0,
  "showDate": true,
  "color": 1,
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="guestwifi"></a>
### Guest Wi-Fi (`guestWifi`)

A network from **Settings → Wi-Fi networks** as a QR code that phones join by scanning.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `network` | string | `""` | The network's name, as in **Settings → Wi-Fi networks**. Blank: the first one. |
| `showPassword` | boolean | `true` | Write the password beside the code. |
| `caption` | string | `"Scan to join the Wi-Fi"` | The words beside the code. |

```json
{
  "id": "guest-wifi-1",
  "type": "guestWifi",
  "title": "",
  "network": "Guests",
  "showPassword": true,
  "caption": "Scan to join the Wi-Fi",
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="message"></a>
### Message (`message`)

A few lines of your own. `{entity_id}` is filled in with that entity's state and unit. A message that comes out blank isn't shown.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `text` | string | `""` | The words, up to 600 characters. `\n` starts a new line. |
| `size` | `"normal"`, `"bold"` or `"large"` | `"bold"` | The text size. |
| `align` | `"left"` or `"center"` | `"left"` | Left or centred. |
| `color` | [colour](#colours) | `1` | The text colour. |
| `icon` | string | `""` | An icon beside it. Blank: none. |

```json
{
  "id": "message-1",
  "type": "message",
  "title": "",
  "text": "Welcome, {input_text.visitor}",
  "size": "large",
  "align": "center",
  "color": 1,
  "icon": "hand-wave",
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="bins"></a>
### Bin collection (`bins`)

Each bin's next collection, soonest first, today's and tomorrow's in red, with *Put out tonight* the evening before.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `calendar` | string | `""` | Your council's calendar: a Home Assistant calendar or a calendar link. |
| `bins` | list | `[]` | Up to 8 bins (below). |
| `days` | number, 1–60 | `21` | Only collections within this many days. |
| `count` | number, 1–8 | `3` | At most this many lines. |
| `tonightFrom` | number, 0–23 | `16` | *Put out tonight* from this hour the day before. `0`: never. |

Each bin:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `id` | string | a random id | The bin's id. |
| `name` | string | `""` | Its name. |
| `match` | string | `""` | Words to find its events in the calendar's titles, separated by commas or semicolons. Blank: its `name`. |
| `entity` | string | `""` | Its own sensor instead of the calendar: a date, a timestamp, or a number of days until collection. |
| `icon` | string | `""` | Its icon. Blank: `trash-can-outline`. |
| `color` | [colour](#colours) | `1` | Its colour. |

```json
{
  "id": "bins-1",
  "type": "bins",
  "title": "",
  "calendar": "calendar.bins",
  "bins": [
    { "id": "bin-1", "name": "Recycling", "match": "recycling", "entity": "", "icon": "recycle", "color": 5 },
    { "id": "bin-2", "name": "General", "match": "", "entity": "sensor.general_waste_days", "icon": "", "color": 1 }
  ],
  "days": 21,
  "count": 3,
  "tonightFrom": 16,
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="photo"></a>
### Photo (`photo`)

A photo from Immich that changes on its own. See [Photos from Immich](../viewport/photos.md).

| Key | Type | Default | Meaning |
|---|---|---|---|
| `source` | object | favourites | Where the photos come from: see [Photo sources](#photo-sources). |
| `every` | number, 5–10080 | `60` | Minutes between photos. |
| `order` | `"random"` or `"sequential"` | `"random"` | `"random"`: any photo each time. `"sequential"`: one after another, oldest first, and round again at the end. |
| `caption` | `"none"`, `"date"`, `"place"` or `"both"` | `"none"` | When the photo was taken, where, or both. |
| `height` | number, 0–480 | `0` | The photo's height in pixels. `0`: the rest of its column. |

```json
{
  "id": "photo-1",
  "type": "photo",
  "title": "",
  "source": { "kind": "album", "album": "b3c1e6f0-1111-2222-3333-444455556666", "person": "" },
  "every": 60,
  "order": "random",
  "caption": "date",
  "height": 240,
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

<a id="airquality"></a>
### Air quality (`airQuality`)

Each reading with a dot in green, yellow or red, and *Good*, *Fair* or *Poor*.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `items` | list | `[]` | Up to 10 readings (below). |
| `showLevel` | boolean | `true` | Write *Good*, *Fair* or *Poor* beside each number. |

Each reading:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `id` | string | a random id | The reading's id. |
| `name` | string | `""` | Its name. Blank: the friendly name. |
| `entity` | string | `""` | The sensor. |
| `kind` | `"auto"`, `"co2"`, `"pm25"`, `"pm10"`, `"voc"`, `"aqi"`, `"humidity"`, `"pollen"` or `"other"` | `"auto"` | Which limits apply. `"auto"`: from the sensor's device class, else its entity id. |
| `fair` | number or `null` | `null` | Fair from this value. `null`: the kind's own. |
| `poor` | number or `null` | `null` | Poor from this value. `null`: the kind's own. |

The kinds' own limits (fair from, poor from): CO2 1000 and 1500 ppm, PM2.5 15 and 35, PM10 45 and 100, VOC 250 and 400, AQI 51 and 101, pollen 3 and 5. Humidity is good from 30 to 60%, fair from 20 to 70%, poor outside that. A sensor that gives words (low, moderate, high and the like) is coloured by the word. `"other"` has no limits of its own.

```json
{
  "id": "air-quality-1",
  "type": "airQuality",
  "title": "",
  "items": [
    { "id": "air-1", "name": "CO2", "entity": "sensor.office_co2", "kind": "auto", "fair": null, "poor": null },
    { "id": "air-2", "name": "Pollen", "entity": "sensor.pollen_grass", "kind": "pollen", "fair": null, "poor": null }
  ],
  "showLevel": true,
  "showWhen": { "mode": "always", "entity": "", "cond": "eq", "value": "", "liveMin": 0 }
}
```

## "Now" items

Each item in a [Now](#now) section has a `kind`, and shows only while its kind says it's active (the alarm always). Every item has the same keys; each kind uses some of them.

| Key | Type | Default | Meaning |
|---|---|---|---|
| `id` | string | a random id | The item's id. |
| `kind` | `"alarm"`, `"heating"`, `"hotWater"`, `"openings"`, `"plants"`, `"robot"` or `"entity"` | `"entity"` | What it watches (below). |
| `name` | string | `""` | The name for `{name}`. Blank: the entity's friendly name. |
| `icon` | string | by kind | The item's icon. |
| `color` | [colour](#colours) | by kind | The item's colour. |
| `title` | string | by kind | The first line. |
| `titleMany` | string | by kind | `openings` and `plants`: the first line when more than one matches. Blank: `title`. |
| `detail` | string | by kind | The second line. |
| `entity` | string | `""` | The entity it watches (`alarm`, `hotWater`, `robot`, `entity`; `heating` without `items`). |
| `eventEntity` | string | `""` | `alarm`: an entity whose text is the last event, for the second line. |
| `battery` | string | `""` | `robot`: its battery sensor, for `{battery}`. |
| `attribute` | string | `""` | `entity`: read this attribute instead of the state. `"@heating"`: its [heating state](#heating-state). |
| `cond` | [condition](#conditions) | `"eq"` (`"contains"` for `plants`) | `plants` and `entity`: how the state is compared. |
| `value` | string | by kind | `plants` and `entity`: what it's compared with. `robot`: the state that counts as working. |
| `items` | list | `[]` | `heating`, `openings`, `plants`: up to 10, each with `id`, `name` and `entity`. |
| `join` | `"and"` or `"list"` | `"list"` | `openings`: how names are joined. `"and"`: *Front & back*. `"list"`: *Kitchen, Side*. |
| `callingDelta` | number, 0–5 | `0.5` | `heating` and `hotWater`: degrees below target that count as calling. |
| `maxChars` | number, 10–120 | `50` | `alarm`: the second line is cut to this many characters. |

**Placeholders.** `title`, `titleMany` and `detail` can use these. A placeholder the kind doesn't fill comes out empty.

| Placeholder | Filled with | Kinds |
|---|---|---|
| `{n}` | how many match | `heating`, `openings`, `plants` |
| `{names}` | the names of those that match | `openings`, `plants` |
| `{name}` | the item's `name`, else the entity's friendly name (see below) | `heating`, `hotWater`, `openings`, `plants`, `robot`, `entity` |
| `{state}` | the state (or attribute) read | `robot`, `entity` |
| `{battery}` | the battery %, rounded | `robot` |
| `{current}` | the water's temperature, rounded | `hotWater` |
| `{target}` | the water heater's target, rounded | `hotWater` |

On `hotWater`, `openings` and `plants`, `{name}` is only the item's own `name`. On `heating` without a `name`, it's the first zone's friendly name.

### Kind `alarm`

Always shown while `entity` is set. The kind words it itself: *Alarm disarmed* (green, `shield-check`), *Alarm part set* (red, `shield-alert`) for a state containing `partly` or starting with `partset`, *Alarm armed* (red, `shield-alert`) for a state starting with `armed`, and *Alarm triggered* (red, `shield-alert`) for `triggered`. Any other state uses the item's `title` (blank: *Alarm*), `icon` and `color`. The second line is `eventEntity`'s text, cut to `maxChars`.

Defaults: `icon` `shield-check`, `color` 1.

### Kind `heating`

Shown while any of its `items` (zones) is calling for heat, by its [heating state](#heating-state) with this item's `callingDelta`. Without `items`, it watches `entity` alone (a whole-house thermostat).

Defaults: `icon` `radiator`, `color` 2, `title` *Heating*, `detail` *{n} zones heating*.

### Kind `hotWater`

Shown while the water heater in `entity` is calling: its operation mode is `heating`, or it's more than `callingDelta` below its target.

Defaults: `icon` `water-boiler`, `color` 2, `title` *Hot water*. A blank `detail` is *{current}C / {target}C target*.

### Kind `openings`

Shown while any of its `items` is open (`on`, `open`, `opening` or `unlocked`). `{names}` is cut to 72 characters.

Defaults: `icon` `door-open`, `color` 2, `title` *Open*, `titleMany` *{n} open*. A blank `detail` is *{names}*.

### Kind `plants`

Shown while any of its `items`' states matches `cond` and `value` (by default, contains *Dry*).

Defaults: `icon` `watering-can`, `color` 5, `title` *Plant needs water*, `titleMany` *Plants need water*, `cond` `contains`, `value` *Dry*. A blank `detail` is *{names}*.

### Kind `robot`

Shown while the vacuum or mower in `entity` is exactly in the state `value`. `cond` isn't used.

Defaults: `icon` `robot-vacuum`, `color` 4, `title` *{name} active*, `detail` *{battery}% battery*, `value` *cleaning* (use *mowing* for a mower).

### Kind `entity`

Shown while `entity`'s state (or `attribute`) matches `cond` and `value`.

Defaults: `icon` `alert`, `color` 2, `title` *{name}*, `detail` *{state}*, `cond` `eq`, `value` *on*.

```json
{
  "id": "now-dryer",
  "kind": "entity",
  "name": "Dryer",
  "icon": "tumble-dryer",
  "color": 2,
  "title": "{name} finished",
  "titleMany": "",
  "detail": "Empty it",
  "entity": "sensor.dryer_status",
  "eventEntity": "",
  "battery": "",
  "attribute": "",
  "cond": "eq",
  "value": "finished",
  "items": [],
  "join": "list",
  "callingDelta": 0.5,
  "maxChars": 50
}
```

## Status icons and rules

Each icon in a [Status icons](#statusicons) section:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `id` | string | a random id | The icon's id. |
| `name` | string | `""` | Its name. |
| `entity` | string | `""` | The entity it follows. |
| `entities` | list of strings | `[]` | Up to 12 more entities. A rule then matches when any of them does (*a door is open*). |
| `attribute` | string | `""` | Read this attribute instead of the state. `"@heating"`: the [heating state](#heating-state). |
| `icon` | string | `""` | The icon drawn when no rule changes it. |
| `color` | [colour](#colours) | `1` | The colour when no rule matches. |
| `showByDefault` | boolean | `true` | Shown when no rule matches. Off: shown only when a rule matches. |
| `rules` | list | `[]` | Up to 8 rules (below). |

Each rule:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `cond` | [condition](#conditions) | `"eq"` | How the value is compared. |
| `value` | string | `""` | What it's compared with. |
| `entity` | string | `""` | Read this entity instead of the icon's. Blank: the icon's `entity` and `entities`. |
| `attribute` | string | `""` | Read this attribute. Blank: the icon's `attribute`. |
| `also` | object or `null` | `null` | A second condition that must hold too: `{ "entity": "", "attribute": "", "cond": "eq", "value": "" }`. A blank `entity` is the entity that matched. Its `attribute` doesn't fall back to the icon's. |
| `color` | [colour](#colours) | `1` | The icon's colour when the rule matches. |
| `icon` | string | `""` | A different icon when it matches. Blank: the icon's own. |
| `hide` | boolean | `false` | Hide the icon when it matches. |

The rules are tried in order and **the first match wins**. If none matches, the icon has its own `color` and `icon`, and is shown only if `showByDefault` is on. An icon's `color` can also be given as `defaultColor`; the server keeps it as `color`.

For example, a vacuum blue while charging below 100% (from its own charging sensor):

```json
{ "cond": "eq", "value": "on", "entity": "binary_sensor.vacuum_charging", "attribute": "",
  "also": { "entity": "sensor.vacuum_battery", "attribute": "", "cond": "lt", "value": "100" },
  "color": 5, "icon": "", "hide": false }
```

**Presets.** Under a Status icons section's icons, **Or start from:** adds an icon from one of these. Each sets the icon's name, icon and rules (and, for some, `attribute` or `showByDefault`), leaving `entity` for you to fill in; every key stays editable.

| Preset | Icon | What it does |
|---|---|---|
| Alarm | `shield-home` | `triggered` red with `shield-alert`; containing `armed_` red; `arming` yellow; `disarmed` green with `shield-off-outline`. |
| Door | `door-closed` | `on` red with `door-open`. |
| Window | `window-closed-variant` | `on` blue with `window-open-variant`. |
| Lock | `lock` | `unlocked` red with `lock-open-variant`; `jammed` red with `lock-alert`. |
| Vacuum | `robot-vacuum` | `cleaning` green; `returning` blue; `error` red with `robot-vacuum-alert`. |
| Mower | `robot-mower` | `mowing` green; `error` red. |
| Plants | `flower` | `due` red with `watering-can`; `watered` green. |
| Heating | `radiator` | Follows the [heating state](#heating-state): `calling` red, `idle` black, `off` black with `radiator-off`. |
| Hot water | `water-boiler` | Follows the heating state: `calling` red, `idle` black, `off` black with `water-boiler-off`. |
| Switch | `power` | `on` red. |
| Alert | `alert` | `showByDefault` off: shown only while `on`, in red. |

Otherwise the icon is black.

<a id="heating-state"></a>
## The heating state (`@heating`)

Set `attribute` to `"@heating"` and the value read isn't the entity's state but its heating state: `calling`, `idle` or `off`.

- **`calling`:**
  - a thermostat (`climate.*`) in `heat` or `auto` and more than 0.5° below its target. This is exactly how the Heating section and the kitchen panel work it out: `auto` alone isn't calling;
  - a water heater (`water_heater.*`) whose operation mode is `heating`, or that's more than 0.5° below its target;
  - any other entity (a switch or a sensor) whose state is `on`, `heat` or `heating`.
- **`idle`:** not off, but not calling (a thermostat at its target or in another mode, a water heater not heating and near its target, a sensor in any other state).
- **`off`:** its state is `off`.

An entity Home Assistant doesn't have reads as empty, so no rule matches it.

It works:

- in a status icon's `attribute` (its rules read it too, unless they name their own `attribute`);
- in a rule's own `attribute`, or its `also`'s;
- in a "now" item of kind `entity`, so it shows only while, say, `calling`. The `heating` and `hotWater` kinds already go by the heating state, with their own `callingDelta`.

Here the margin is always 0.5°. The **Heating** and **Hot water** icon presets use it: red while calling, black while idle, and their *off* icon when off. Their colours are ordinary rules, so you can change them.

```json
{
  "id": "icon-heating",
  "name": "Heating",
  "entity": "climate.whole_house",
  "entities": [],
  "attribute": "@heating",
  "icon": "radiator",
  "color": 1,
  "showByDefault": true,
  "rules": [
    { "cond": "eq", "value": "calling", "entity": "", "attribute": "", "also": null, "color": 2, "icon": "", "hide": false },
    { "cond": "eq", "value": "idle", "entity": "", "attribute": "", "also": null, "color": 1, "icon": "", "hide": false },
    { "cond": "eq", "value": "off", "entity": "", "attribute": "", "also": null, "color": 1, "icon": "radiator-off", "hide": false }
  ]
}
```

To hide it when off instead, set `"hide": true` on the last rule.

## Values

### Conditions

Used by rules, `also`, alert slots, "now" items and `showWhen`. The comparison is on text and is case-sensitive. A state that's empty, `unknown`, `unavailable` or `none` never matches, whatever the condition (`ne` too).

| `cond` | Matches when the value… |
|---|---|
| `eq` | is exactly `value`. |
| `ne` | is anything but `value`. |
| `contains` | contains `value` (`armed_` matches `armed_away` but not `disarmed`). |
| `startsWith` | starts with `value`. |
| `in` | is one of a comma-separated list: `value` is split at commas and each part trimmed (`"heat, auto"`). |
| `gt` | is a number greater than `value`. Both must be numbers. |
| `lt` | is a number less than `value`. Both must be numbers. |

### Colours

The panel's six inks:

| Number | Colour |
|---|---|
| `0` | white |
| `1` | black |
| `2` | red |
| `3` | yellow |
| `4` | green |
| `5` | blue |

Any other number gets the key's default.

### Types and limits

- **Booleans** can also be `1` or `0` (or `"1"` and `"0"`).
- **Numbers** can also be numeric strings (`"30"`). A number outside its range is moved to the nearest end of it.
- **Strings** are trimmed. A choice (`"sidebar"`, `"eq"`…) that isn't one of the listed values gets the default.
- **Ids** are kept up to 64 characters. A missing one is made up.
- **Lists** longer than their limit are cut. In a list of objects (rooms, rules, items…), entries that aren't objects are dropped.

| List | At most |
|---|---|
| Screens in a layout | 12 |
| Sections in a column | 8 (16 in the last column when extra columns are merged into it) |
| Status icons in a section | 12 |
| Extra entities (`entities`) on a status icon | 12 |
| Rules on a status icon | 8 |
| Alert slots | 12 |
| Entities in an alert slot | 8 |
| Calendars (in a calendar section or a photo frame) | 8 |
| Rooms (room climate, room temperatures) | 12 |
| People | 8 |
| Now playing players | 3 |
| Departure routes | 4 |
| Doors & windows, motion sensors, cameras, and a "now" item's `items` | 10 |
| Room finder rooms | 12 |
| "Now" items | 12 |
| Heating zones | 16 |
| Bins | 8 |
| Air quality readings | 10 |
