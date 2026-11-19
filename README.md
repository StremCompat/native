# StremCompat Native (beta)

<p align="center"><b>English</b> · <a href="README.pt.md">Português</a></p>

The native version of StremCompat: the same app, drawn by the TV's own GPU in C with SDL2 and
OpenGL ES 2, for LG webOS 3 and newer, Samsung Tizen and Android TV. Its backend is the local
server in [StremCompat/stremcompat](https://github.com/StremCompat/stremcompat).

> **Beta, not stable yet.** To install it, go to your TV's page:
> [LG](https://github.com/StremCompat/webos) · [Samsung](https://github.com/StremCompat/samsung) ·
> [Android TV](https://github.com/StremCompat/android-tv). For everyday use, the web version
> (StremCompat 4.5) is the stable one.

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
