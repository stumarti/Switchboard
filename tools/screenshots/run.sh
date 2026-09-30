#!/usr/bin/env bash
# Draws the remote's screens for the manual, as PNGs, from the firmware's own
# drawing code compiled for this PC (render.cpp, stubs/).
#
# It needs a Switchboard Server to talk to, on this machine at the port the
# firmware uses (45678): the server repo's demo does that —
#   DEMO_PORT=45678 node tools/demo/demo.js      (in Switchboard-Server)
# then:
#   tools/screenshots/run.sh [out-dir]           (default docs/manual/images/remote)
#
# Needs g++ and zlib; fetches ArduinoJson and Nayuki's QR code generator on
# first run. FREEINK_SDK points at the SDK (default ../freeink-sdk).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
out="${1:-$root/docs/manual/images/remote}"
cache="$here/.cache"
sdk="${FREEINK_SDK:-$root/../freeink-sdk}"
aj="${ARDUINOJSON_DIR:-$root/test/host/.cache/ArduinoJson}"
mkdir -p "$cache" "$out"
if [ ! -f "$aj/src/ArduinoJson.h" ]; then
  git -c advice.detachedHead=false clone --quiet --depth 1 --branch v7.4.2 https://github.com/bblanchon/ArduinoJson "$aj"
fi
if [ ! -f "$cache/qrcodegen/c/qrcodegen.c" ]; then
  git clone --quiet --depth 1 https://github.com/nayuki/QR-Code-generator "$cache/qrcodegen"
fi
version="${SCREENSHOT_VERSION:-v1.3.0}"
gcc -c -O1 "$cache/qrcodegen/c/qrcodegen.c" -o "$cache/qrcodegen.o"
g++ -std=gnu++17 -O1 -w -DFIRMWARE_VERSION="\"$version\"" \
  -I "$here/stubs" -I "$root/include" -I "$aj/src" -I "$cache/qrcodegen/c" \
  -I "$sdk/libs/ui/FreeInkUI/include" -I "$sdk/libs/assets/Icons/include" \
  "$here/render.cpp" "$sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp" "$cache/qrcodegen.o" -lz -o "$cache/render"
"$cache/render" "$out"
