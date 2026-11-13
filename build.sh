#!/bin/bash
# Builds StremCompat with Zig as the C compiler, for one target:
#   ./build.sh win      Windows x64, for development and the screenshot tests
# The TV targets (webos, tizen, android) are added with their SDKs.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ZIG="${ZIG:-$HOME/stremcompat-build/tools/zig/zig}"
TARGET="${1:-win}"
SRC=$(ls "$HERE"/src/*.c)
CFLAGS="-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -I$HERE/src -I$HERE/third_party/stb"

case "$TARGET" in
  win)
    SDL="$HERE/third_party/SDL2-2.30.9/x86_64-w64-mingw32"
    OUT="$HERE/build/win"
    mkdir -p "$OUT"
    "$ZIG" cc -target x86_64-windows-gnu $CFLAGS -I"$SDL/include/SDL2" -Dmain=SDL_main $SRC \
      "$SDL/lib/libSDL2main.a" "$SDL/lib/libSDL2.dll.a" -o "$OUT/stremcompat.exe"
    cp "$SDL/bin/SDL2.dll" "$OUT/"
    rm -rf "$OUT/assets"
    cp -r "$HERE/assets" "$OUT/assets"
    echo "built $OUT/stremcompat.exe"
    ;;
  *)
    echo "unknown target: $TARGET" >&2
    exit 1
    ;;
esac
