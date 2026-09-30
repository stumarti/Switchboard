# Remote updates (over the air)

**Settings → Remote updates** sends new firmware to remotes over Wi-Fi. It's **off until you switch it on**.

<figure class="shot"><img src="../images/admin/settings-updates.png" alt="Settings, Remote updates"><figcaption><b>Settings → Remote updates</b>: the release, how it rolls out, and every remote's state. Here v1.4.0 has reached the pilot remote.</figcaption></figure>

1. **Add a build**: upload a `switchboard-app-<version>.bin`, or pick a release from one of the **GitHub repositories** listed on the page. The list starts with the Switchboard firmware; add your own, such as a fork (`owner/name` or its GitHub URL). A release needs its `switchboard-app-<version>.bin` and `.sha256`, which the firmware's release workflow publishes, and the download is checked against the published checksum. Only listed repositories are ever read, and only when you press a button. The first in the list is the one "Get latest release" uses. The server only accepts a Switchboard remote image for the ESP32-S3 that fits the update slot, and takes the version from the image itself. A build from uncommitted changes (`-dirty`) is refused.
2. **Choose the release**: the version remotes should run. An older build rolls remotes back to it.
3. **Pilot first**: tick a remote or two as pilots. A new release goes only to them. When they've updated and still work, press **Release to everyone**.
4. **How remotes install it**: from **Settings → Device info → Check for update** on the remote (can be switched off here), and/or **on a schedule**: during a window you set, on a remote's normal timer wake.
5. **Update now**: while remotes are still due the release, the Home page's Devices card has **Update N remotes to vX now**. Each of them installs it at its next timer wake (within its refresh interval), whatever the schedule, and even with no schedule set. It keeps to the minimum battery, and to the release stage: while the release is with the pilots the button updates just them, and **Update all N now (skip pilot)** releases it to everyone in the same step (after asking), so every remote not on it installs it at its next wake. A remote that tries and fails isn't asked again at every wake; press the button again to retry. **Cancel** takes it back, and choosing another release clears it. Needs remote firmware that knows about it (later than v0.2.3); older remotes keep to the schedule.

On the remote:
- **Battery:** it needs the minimum battery set here (30% by default).
- **Integrity:** it downloads into the spare app slot and checks the SHA-256 before switching.
- **Rollback:** new firmware counts as good only once it reaches this server on its first run. If it can't, the bootloader goes back to the previous firmware by itself.
- **Reporting:** every attempt is reported. Failures show on this page and on the Home page.
- **Retries:** a scheduled update that fails twice at the same version isn't retried until you release another version.

Remotes on firmware from before updates existed need one USB (or web-flasher) install first.

## Where builds are kept

Builds live on this server, in `<DATA_DIR>/firmware/` (with `firmware.json` recording each one's version, size, checksum and source). Remotes only ever download from here — GitHub is read only when you press a button to add a release.

See [Security](security.md) for what turning this on means, and the remote's [Updates](../remote/updates.md) page for what a remote shows while it updates.
