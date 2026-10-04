# Settings

Settings are shared by every room and device. The server's clock is shown beside your browser's on every tab.

## Home Assistant

<figure class="shot"><img src="../images/admin/settings-home-assistant.png" alt="Settings, Home Assistant"><figcaption>The connection every room uses, with a test.</figcaption></figure>

The address, port and long-lived access token the server uses to reach Home Assistant. The server fetches Home Assistant's state for every device; a remote only asks Home Assistant itself when the server can't reach it.

The same tab can **publish every device's battery to Home Assistant** as sensors, off by default. See [Battery life](battery-life.md#in-home-assistant).

## Wi-Fi

<figure class="shot"><img src="../images/admin/settings-wifi.png" alt="Settings, Wi-Fi"><figcaption>The network remotes join, and the networks they show as QR codes.</figcaption></figure>

The network remotes join, and the household's networks, which the remote's [Wi-Fi page](../remote/screens.md#wi-fi) shows as join-QR codes for guests.

## Clock

<figure class="shot"><img src="../images/admin/settings-clock.png" alt="Settings, Clock"><figcaption>The time server, and whether the server's clock agrees with your browser's.</figcaption></figure>

Remotes set their clock from the server, so this tab says if the server's clock drifts or its time zone differs from your browser's. It also sets the NTP server devices use.

**Time zone.** Every time a remote or viewport shows (clocks, calendars, meetings, quiet hours) is in the server's time zone. Leave it on **Automatic** and the server takes Home Assistant's, re-checked hourly. Without Home Assistant (meeting-room signs on calendar links, for example), choose it here: a container is otherwise on UTC, and the Home page warns that times are shown in it. `TZ` in the server's environment wins over this setting. A change takes effect at once, and on each display at its next refresh.

## Theme and Updates

See [Theme](theme.md) and [Updates](remote-updates.md).

## Security

<figure class="shot"><img src="../images/admin/settings-security.png" alt="Settings, Security"><figcaption>Allowed networks, sign-in, and new devices.</figcaption></figure>

Which networks the server answers, how sign-in is protected, and whether new devices may ask to pair. See [Security](security.md).

## Account

<figure class="shot"><img src="../images/admin/settings-account.png" alt="Settings, Account"><figcaption>Changing the admin password.</figcaption></figure>

Change the admin password. If `ADMIN_PASSWORD` is set on the container, it replaces the password on every restart (see [Pairing & auth](pairing-and-auth.md)).
