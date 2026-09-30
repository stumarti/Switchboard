# Settings

Settings are shared by every room and device. The server's clock is shown beside your browser's on every tab.

## Home Assistant

<figure class="shot"><img src="../images/admin/settings-home-assistant.png" alt="Settings, Home Assistant"><figcaption>The connection every room uses, with a test.</figcaption></figure>

The address, port and long-lived access token the server uses to reach Home Assistant. The server fetches Home Assistant's state for every device; a remote only asks Home Assistant itself when the server can't reach it.

## Wi-Fi

<figure class="shot"><img src="../images/admin/settings-wifi.png" alt="Settings, Wi-Fi"><figcaption>The network remotes join, and the networks they show as QR codes.</figcaption></figure>

The network remotes join, and the household's networks, which the remote's [Wi-Fi page](../remote/screens.md#wi-fi) shows as join-QR codes for guests.

## Clock

<figure class="shot"><img src="../images/admin/settings-clock.png" alt="Settings, Clock"><figcaption>The time server, and whether the server's clock agrees with your browser's.</figcaption></figure>

Remotes set their clock from the server, so this tab says if the server's clock drifts or its time zone differs from your browser's. It also sets the NTP server devices use.

## Theme and Remote updates

See [Theme](theme.md) and [Remote updates](remote-updates.md).

## Account

<figure class="shot"><img src="../images/admin/settings-account.png" alt="Settings, Account"><figcaption>Changing the admin password.</figcaption></figure>

Change the admin password. If `ADMIN_PASSWORD` is set on the container, it replaces the password on every restart (see [Pairing & auth](pairing-and-auth.md)).
