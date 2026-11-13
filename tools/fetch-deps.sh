#!/bin/bash
# Downloads the third-party pieces that are not kept in git: SDL2 (zlib licence) for the
# Windows build and its source for the TV builds.
set -euo pipefail
cd "$(dirname "$0")/../third_party"
SDL=2.30.9
[ -d "SDL2-$SDL/x86_64-w64-mingw32" ] || {
  curl -fsSL -o sdl-mingw.tar.gz "https://github.com/libsdl-org/SDL/releases/download/release-$SDL/SDL2-devel-$SDL-mingw.tar.gz"
  tar xzf sdl-mingw.tar.gz && rm sdl-mingw.tar.gz
}
[ -f "SDL2-src.tar.gz" ] || curl -fsSL -o SDL2-src.tar.gz "https://github.com/libsdl-org/SDL/releases/download/release-$SDL/SDL2-$SDL.tar.gz"
echo "third-party ready"
