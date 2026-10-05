# Releases

```
git tag v0.4.0
git push origin v0.4.0
```

GitHub Actions (`.github/workflows/release.yaml`) then:

- checks the tagged commit is on `main` — tag a commit that's been reviewed and merged, or the release stops;
- builds the firmware against the pinned FreeInk SDK commit;
- merges it into one flashable image and attaches it to a GitHub Release;
- attaches the app image on its own, `switchboard-x4pro-app-<version>.bin`, with its `.sha256`, for the server's [remote updates](../server/remote-updates.md) (`x4pro` is this firmware's board, `SWITCHBOARD_BOARD` in `platformio.ini`; the image's marker says it too);
- publishes the flashable image to `docs/firmware/`.

## The browser flasher

`docs/firmware/` is what the [browser flasher](../../) serves, and `docs/manual/` is this manual, once GitHub Pages is turned on for this repo (Settings → Pages → `main` / `/docs`). The flasher needs Chrome or Edge on a desktop: Web Serial isn't available elsewhere.

The flasher also installs the viewport. Its firmware, `docs/firmware/viewport/`, is the latest [Switchboard Viewport](https://github.com/stumarti/Switchboard-Viewport/releases) release. The **Viewport web flasher** workflow copies it there every six hours, or when you run it by hand (Actions → Viewport web flasher → Run workflow). It only commits when a new release is out. To have each viewport release update the flasher straight away, give the Switchboard-Viewport repository a `SWITCHBOARD_FLASHER_TOKEN` secret: a fine-grained token for this repository with *Contents: read and write*. Its release workflow then triggers the update when it publishes.

## Locking it down

Anything tagged on `main` can reach every remote once it's released on the server, so it's worth protecting:

- a branch protection rule on `main` (pull requests, passing CI);
- a tag ruleset so only you can push `v*` tags;
- two-factor authentication on the GitHub account.
