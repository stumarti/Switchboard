Import("env")
import os
import shutil

# ---------------------------------------------------------------------------
# Make the app-provided <Logging.h> reachable by the FreeInk SDK libraries.
#
# Several SDK libs (currently FrontlightManager) do `#include <Logging.h>` and
# expect the consuming app to supply it. Our copy is include/Logging.h.
#
# Two mechanisms, belt-and-suspenders, because PlatformIO's handling of
# cross-target include paths varies by platform/version:
#
#   1. Add our include/ to CPPPATH for the project env AND every library build.
#   2. Physically copy Logging.h into the include/ dir of each SDK library that
#      needs it. A library's own include/ is always on its compile path, so this
#      resolves the header even if (1) doesn't propagate. The SDK checkout is a
#      throwaway dependency, so writing one header into it is harmless and
#      self-healing (re-copied every build).
# ---------------------------------------------------------------------------

project_include = os.path.join(env["PROJECT_DIR"], "include")
logging_h = os.path.join(project_include, "Logging.h")

# (1) CPPPATH for the project environment.
env.Append(CPPPATH=[project_include])

# (1) CPPPATH for each dependency library build.
try:
    for lb in env.GetLibBuilders():
        lb.env.PrependUnique(CPPPATH=[project_include])
except Exception as e:
    print("[add_include_path] GetLibBuilders skipped:", e)

# (2) Copy Logging.h into the include/ dir of any SDK library build.
def ensure_logging_header():
    if not os.path.isfile(logging_h):
        print("[add_include_path] WARNING: include/Logging.h not found at", logging_h)
        return
    copied = 0
    try:
        builders = env.GetLibBuilders()
    except Exception as e:
        print("[add_include_path] GetLibBuilders unavailable for header copy:", e)
        return
    for lb in builders:
        src_dir = lb.src_dir  # .../<Lib>/src
        lib_root = os.path.dirname(src_dir)
        inc_dir = os.path.join(lib_root, "include")
        target_dir = inc_dir if os.path.isdir(inc_dir) else src_dir
        if os.path.abspath(target_dir) == os.path.abspath(project_include):
            continue
        try:
            shutil.copyfile(logging_h, os.path.join(target_dir, "Logging.h"))
            copied += 1
        except Exception as e:
            print("[add_include_path] could not copy Logging.h to", target_dir, ":", e)
    print("[add_include_path] Logging.h provisioned into", copied, "library include dirs")

ensure_logging_header()
