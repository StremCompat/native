#!/bin/bash
# Builds StremCompat with Zig as the C compiler, for one target:
#   ./build.sh win      Windows x64, for development and the screenshot tests
#   ./build.sh webos    LG webOS (ARM)
#   ./build.sh tizen    Samsung Tizen: WebAssembly for the web package (Emscripten)
#   ./build.sh android  the assets the Android TV app packs (the code is built by its Gradle project)
#   ./build.sh test     the unit tests (Windows)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ZIG="${ZIG:-$HOME/stremcompat-build/tools/zig/zig}"
TARGET="${1:-win}"
# the version the local server must report before the app talks to it (screens/boot.c)
# the version is the native profile's (public-repo/profiles/lg-native.json), so the app and the
# server it checks at start never disagree; APP_VERSION still overrides it
PROFILE_JSON="$(dirname "$0")/../LG/public-repo/profiles/lg-native.json"
PROFILE_VERSION="$(grep -o '"version": *"[0-9.]*"' "$PROFILE_JSON" 2>/dev/null | head -1 | grep -o '[0-9][0-9.]*')"
VERSION="${APP_VERSION:-${PROFILE_VERSION:-5.0.0}}"
[ -n "$PROFILE_VERSION" ] || echo "warning: no version in $PROFILE_JSON, using $VERSION" >&2
SRC=$(ls "$HERE"/src/*.c "$HERE"/src/core/*.c "$HERE"/src/screens/*.c 2>/dev/null)
CFLAGS="-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -I$HERE/src -I$HERE/src/core -I$HERE/src/platform -I$HERE/third_party/stb"

case "$TARGET" in
  win)
    SDL="$HERE/third_party/SDL2-2.30.9/x86_64-w64-mingw32"
    OUT="$HERE/build/win"
    mkdir -p "$OUT"
    "$ZIG" cc -target x86_64-windows-gnu $CFLAGS -I"$SDL/include/SDL2" -Dmain=SDL_main $SRC "$HERE/src/platform/desktop.c" "$HERE/src/platform/desktop_player.c" \
      "$SDL/lib/libSDL2main.a" "$SDL/lib/libSDL2.dll.a" -lws2_32 -o "$OUT/stremcompat.exe"
    cp "$SDL/bin/SDL2.dll" "$OUT/"
    rm -rf "$OUT/assets"
    cp -r "$HERE/assets" "$OUT/assets"
    echo "built $OUT/stremcompat.exe"
    ;;
  webos)
    # ARM, against the TV's own SDL2, GLES2 and C library (webOS Homebrew toolchain)
    TC="${WEBOS_TOOLCHAIN:-$HOME/stremcompat-build/webos/arm-webos-linux-gnueabi_sdk-buildroot}"
    SYSROOT="$TC/arm-webos-linux-gnueabi/sysroot"
    OUT="$HERE/build/webos"
    mkdir -p "$OUT"
    "$TC/bin/arm-webos-linux-gnueabi-gcc" $CFLAGS -D_GNU_SOURCE -DAPP_VERSION="\"$VERSION\"" -DSC_PORT=8083 -DIMG_BUDGET="(48u << 20)" -I"$SYSROOT/usr/include/SDL2" \
      -I"$SYSROOT/usr/include/glib-2.0" -I"$SYSROOT/usr/lib/glib-2.0/include" \
      $SRC "$HERE/src/platform/webos.c" "$HERE/src/platform/webos_player.c" "$HERE/src/platform/webos_luna.c" "$HERE/src/platform/webos_acb.c" \
      -Wl,--as-needed -lSDL2 -lluna-service2 -lglib-2.0 -ldl -lpthread -lm -o "$OUT/stremcompat"
    "$TC/bin/arm-webos-linux-gnueabi-strip" "$OUT/stremcompat"
    rm -rf "$OUT/assets"
    cp -r "$HERE/assets" "$OUT/assets"
    echo "built $OUT/stremcompat"
    ;;
  tizen)
    # WebAssembly with SDL2 and WebGL from Emscripten, and the same in plain JavaScript for the TVs
    # without WebAssembly (Tizen 4); the network and the player go through the page
    EMSDK="${EMSDK:-$HOME/stremcompat-build/emsdk}"
    source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1
    OUT="$HERE/build/tizen"
    mkdir -p "$OUT"
    WEB_SRC=$(ls $SRC | grep -v '/core/net.c$')
    emcc $CFLAGS -DAPP_VERSION="\"$VERSION\"" -DIMG_BUDGET="(48u << 20)" -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH -sMAX_WEBGL_VERSION=1 -sFULL_ES2 -sWASM=2 -sMIN_CHROME_VERSION=56 \
      -sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToUTF8 -sEXPORTED_FUNCTIONS=_main,_malloc \
      --preload-file "$HERE/assets@assets" $WEB_SRC "$HERE/src/platform/tizen.c" "$HERE/src/platform/tizen_player.c" \
      "$HERE/src/platform/web_net.c" -o "$OUT/stremcompat.js"
    echo "built $OUT/stremcompat.js"
    ;;
  android)
    # public-repo/android builds the code with CMake (app/src/main/cpp); this stages the assets
    OUT="$HERE/build/android"
    rm -rf "$OUT"
    mkdir -p "$OUT"
    cp -r "$HERE/assets" "$OUT/assets"
    echo "staged $OUT/assets"
    ;;
  test)
    # unit tests, built for Windows like the app; run them from Windows
    SDL="$HERE/third_party/SDL2-2.30.9/x86_64-w64-mingw32"
    OUT="$HERE/build/test"
    mkdir -p "$OUT"
    for t in "$HERE"/tests/test_*.c; do
      name=$(basename "$t" .c)
      "$ZIG" cc -target x86_64-windows-gnu $CFLAGS -I"$SDL/include/SDL2" "$t" "$HERE/src/core/json.c" "$HERE/src/core/util.c" "$HERE/src/core/qr.c" \
        "$SDL/lib/libSDL2.dll.a" -o "$OUT/$name.exe"
    done
    cp "$SDL/bin/SDL2.dll" "$OUT/"
    echo "built tests in $OUT"
    ;;
  *)
    echo "unknown target: $TARGET" >&2
    exit 1
    ;;
esac
