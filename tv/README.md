# Switchboard TV

An Android TV / Google TV app that shows a viewport layout from Switchboard Server on the TV, live and in full colour. Photos can slowly zoom and drift (a photo's **Slow motion on a TV** setting).

The app is a full-screen window onto the server's display page (`/display`). The page does the pairing and drawing; the app finds the server, keeps the screen awake and adds:

- **Server discovery:** it finds Switchboard Server by mDNS (`_switchboard._tcp`), or you type its address.
- **Start with the TV:** it can open when the TV turns on.
- **Screensaver:** the layout whenever the TV is idle (Settings → Device Preferences → Screen saver → Switchboard).

## On the TV

- **Arrows:** previous / next screen.
- **OK:** the first screen.
- **Hold BACK, or press MENU:** choose another server.
- **BACK:** leave.

The TV pairs like a viewport. The first time, it shows its id. On the server, approve it on the **Viewports** (or **Home**) page and pick its layout. It's listed under the TV's own name, with the board **Android TV**.

## Install

Download `switchboard-tv-<version>.apk` from this repository's releases (tags `tv-v…`). Then install it in one of two ways:

- **Downloader:** in the Downloader app on the TV, open the APK's link.
- **adb:** `adb connect <tv-ip>`, then `adb install -r switchboard-tv-<version>.apk`.

Either way, allow installs from unknown sources when the TV asks.

## Build

```
cd tv
./gradlew assembleRelease
```

This needs the Android SDK (`ANDROID_HOME`, or `sdk.dir` in `local.properties`) and JDK 17. CI builds it on every push that touches `tv/`. A `tv-vX.Y.Z` tag publishes a release.

### Signing

A release installs over the previous one only when both are signed with the same key. Make the key once, then give it to CI as repository secrets:

```
keytool -genkeypair -v -keystore switchboard-tv.jks -alias switchboard-tv \
  -keyalg RSA -keysize 4096 -validity 10000
base64 -w0 switchboard-tv.jks      # → SB_TV_KEYSTORE_B64
```

| Secret | Value |
|---|---|
| `SB_TV_KEYSTORE_B64` | the keystore, base64 |
| `SB_TV_KEYSTORE_PASSWORD` | its password |
| `SB_TV_KEY_ALIAS` | `switchboard-tv` |
| `SB_TV_KEY_PASSWORD` | the key's password |

Without the secrets, CI signs with a throwaway debug key. Those builds work, but each one has to be uninstalled before the next is installed.

Keep the keystore safe: losing it means uninstalling and reinstalling every TV, and approving each one again.
