Import("env")
import subprocess

# ---------------------------------------------------------------------------
# Derive FIRMWARE_VERSION from git so every build knows what it is without
# hand-editing a version number. `git describe` on an exact tag (e.g. after
# `git tag v0.3.0`) gives just "v0.3.0"; between tags it gives something like
# "v0.3.0-4-gabc1234" (4 commits past v0.3.0, at abc1234); a dirty working
# tree appends "-dirty". Falls back to the raw commit hash if there's no tag
# yet, and to "unknown" if this isn't a git checkout at all (e.g. a source
# zip) so the build never fails over this.
# ---------------------------------------------------------------------------

def get_version():
    try:
        v = subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty", "--match", "v*"],
            cwd=env["PROJECT_DIR"],
            stderr=subprocess.DEVNULL,
        ).decode().strip()
        return v if v else "unknown"
    except Exception as e:
        print("[gen_version] git describe failed, falling back to 'unknown':", e)
        return "unknown"

version = get_version()
print("[gen_version] FIRMWARE_VERSION =", version)
env.Append(BUILD_FLAGS=['-DFIRMWARE_VERSION=\\"%s\\"' % version])
