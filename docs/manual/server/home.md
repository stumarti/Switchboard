# Home

Open the server at `http://<server>:45678` and sign in. The menu down the left has **Layouts**, **Remotes**, **Viewports** and **Settings**; the Switchboard logo at the top is the Home page.

<figure class="shot"><img src="../images/admin/home.png" alt="The Home page"><figcaption>Home, in the demo: one remote waiting for approval and one low battery.</figcaption></figure>

Home shows how the whole setup is doing, and what needs attention, worst first. Each item links to where it's fixed.

- **At a glance:** remotes and viewports online, devices waiting for approval, low batteries, and Home Assistant's response time.
- **Needs attention:**
  - critical and low batteries (10% and 20%), and any device with about 3 days or less left ([Battery life](battery-life.md));
  - devices not heard from (a remote in a day, a viewport in three of its refresh intervals), weak Wi-Fi, and devices without a room or layout;
  - remotes on different firmware versions, and failed [updates](remote-updates.md);
  - Home Assistant not set up, unreachable, or failing requests in the last hour (with the recent errors), and any entity a layout names that Home Assistant doesn't have (a typo, or one renamed in Home Assistant).
- **Devices:** every device with its room or layout, battery and the days it has left, Wi-Fi signal, firmware (an icon shows an update waiting, installed or failed) and when it was last seen.
