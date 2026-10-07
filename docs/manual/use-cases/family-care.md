# Family and care

<p class="lead">For an older parent or a relative with memory loss, a clear day display helps a lot: what day it is, who's coming, and a reminder for the tablets. A viewport shows it in large, plain text, and the family can update it from anywhere.</p>

<div class="shots wide">
  <figure><div class="panel"><img src="../images/use-cases/family-care.png" alt="A day display for a relative"></div><figcaption>The day, the weather, today's visits and a reminder until the tablets are taken</figcaption></figure>
</div>

## The case for Switchboard

- **Nothing to learn and nothing to press.** It shows the day, the date and who's coming. There's no menu, no screen to unlock and no app.
- **Calm, not bright.** E-ink looks like paper. It doesn't glow at night or flicker, and it reads easily from a chair across the room.
- **The family keeps it up to date.** Visits come from a shared calendar, so adding *Sarah visits* on a phone puts it on the wall at the next refresh.
- **Reminders that go away.** *Take your tablets with breakfast* shows in red until someone ticks it off in Home Assistant (or an automation does), then disappears.
- **Nothing to plug in.** It runs for months on a charge, so there's no cable to trip over and no charger to remember.

## Setting it up

This screen is two columns:
- **Left:** *Weather*, then a *Message* in red with a pill icon, shown only while a Home Assistant toggle such as `input_boolean.tablets_due` is on.
- **Right:** a large blue *Message*, `Good {sensor.part_of_day}` and `It's {sensor.today_long}` on two lines, then a *Calendar* of today and tomorrow.

A message fills in `{entity_id}` with the entity's state, so a template sensor can supply *morning* or *afternoon*, or the date in words. See [Viewport layouts](../server/viewports.md).
