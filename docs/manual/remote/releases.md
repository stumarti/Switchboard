# Releases

```
git tag v0.4.0
git push origin v0.4.0
```

GitHub Actions (`.github/workflows/release.yaml`) then:

- checks the tagged commit is on `develop` — tag a commit that's been reviewed and merged, or the release stops;
- builds the firmware against the pinned FreeInk SDK commit;
- merges it into one flashable image and attaches it to a GitHub Release;
- attaches the app image on its own, `switchboard-app-<version>.bin`, with its `.sha256`, for the server's [remote updates](../server/remote-updates.md);
- publishes the flashable image to `docs/firmware/`.

## The browser flasher

`docs/firmware/` is what the [browser flasher](../../) serves, and `docs/manual/` is this manual, once GitHub Pages is turned on for this repo (Settings → Pages → `develop` / `/docs`). The flasher needs Chrome or Edge on a desktop: Web Serial isn't available elsewhere.

## Locking it down

Anything tagged on `develop` can reach every remote once it's released on the server, so it's worth protecting:

- a branch protection rule on `develop` (pull requests, passing CI);
- a tag ruleset so only you can push `v*` tags;
- two-factor authentication on the GitHub account.
