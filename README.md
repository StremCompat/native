# StremCompat native

The native rewrite of [StremCompat](https://github.com/Alexesmm/stremcompat-lg-webos): the same
app, drawn by the TV's own GPU in C with SDL2 and OpenGL ES 2, for LG webOS 3 and newer,
Samsung Tizen and Android TV. The local server of the current app stays as its backend.

## Build

```
tools/fetch-deps.sh
./build.sh win          # Windows, for development and the screenshot tests
```

`build/win/stremcompat.exe --shot out.png [--keys script.txt]` draws offscreen at 1920x1080
and saves the picture.

## Third-party

- SDL2 (zlib licence)
- stb_image, stb_truetype, stb_image_write (public domain)
- Inter typeface (SIL Open Font License, `assets/fonts/Inter-LICENSE.txt`)

Licensed under GPL-3.0-only, like StremCompat.
