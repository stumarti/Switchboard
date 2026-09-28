#!/usr/bin/env bash
# Build and run the host tests (test/host/test_main.cpp) with g++ — no
# hardware, no PlatformIO. ArduinoJson is header-only: pass its checkout in
# ARDUINOJSON_DIR, or it's cloned into test/host/.cache on first run.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
aj="${ARDUINOJSON_DIR:-$here/.cache/ArduinoJson}"
if [ ! -f "$aj/src/ArduinoJson.h" ]; then
  git -c advice.detachedHead=false clone --quiet --depth 1 --branch v7.4.2 https://github.com/bblanchon/ArduinoJson "$aj"
fi
out="$here/.cache/host_tests"
mkdir -p "$(dirname "$out")"
g++ -std=gnu++17 -Wall -Wextra -Werror -Wno-unused-parameter \
  -I "$here/stubs" -I "$root/include" -I "$aj/src" \
  "$here/test_main.cpp" -o "$out"
"$out"
