# Pairing

Every physical remote pairs with Switchboard Server once, by MAC address. This is what proves it's allowed to pull a room's config, which includes your Home Assistant token and Wi-Fi password.

- **First contact.** A never-paired (or revoked) device registers once, then shows "Approve this remote on the Switchboard server, then press any button". It doesn't keep polling the server; each button press checks once. If it can't reach the server at all, it retries every 30 seconds.
- **Already paired.** A paired device skips straight past it, with no network round trip.
- **Token lost or refused.** A paired remote whose token the server stops accepting quietly re-registers and picks up a fresh one, rather than dropping back to the approval screen.
- **Room.** Approving a device from the server's **Remotes** page is also how its default room gets set. Pick a room there, or leave it unset and pick one later on the device via **Settings → Select room**. A room picked on the device always wins over the server's assignment.

See the server's [Pairing & auth](https://github.com/stumarti/Switchboard-Server/blob/main/wiki/Pairing-and-Auth.md) page for the admin side.
