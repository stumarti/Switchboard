# Updates

Remotes can update their firmware over Wi-Fi from Switchboard Server. It's off until it's switched on in the server's **Settings → Remote updates**, and the firmware always comes from your own server, never straight from the internet. See the server's [Remote updates](https://github.com/stumarti/Switchboard-Server/blob/main/wiki/Remote-Updates.md) page for choosing a release, pilots and the schedule.

## Installing one

- **By button:** **Settings → Device info → Check for update** (unless the server has switched the button off). The remote says it's up to date, or shows the version on offer to install.
- **On a schedule:** during the window set on the server, on a normal timer wake. The screen counts down 10 seconds first; press any button to put it off.

While it installs, the screen shows the version it's going from and to, a progress bar, the time left and when it started. Then it restarts.

## What keeps it safe

- **Battery:** it won't start below the server's minimum battery (30% by default).
- **Integrity:** the image downloads into the spare app slot and its SHA-256 is checked before the remote switches to it.
- **Rollback:** new firmware counts as good only once it reaches the server on its first run. If it restarts or sleeps before that, the bootloader goes back to the previous firmware by itself.
- **Reporting:** every attempt, good or bad, is reported to the server.
- **Retries:** a scheduled update that fails twice at the same version isn't tried again until a different version is released.

Firmware from before updates existed needs one USB (or [browser flasher](https://stumarti.github.io/Switchboard/)) install first.
