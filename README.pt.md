# StremCompat Native (beta)

<p align="center"><a href="README.md">English</a> · <b>Português</b></p>

A versão nativa do StremCompat: o mesmo app, desenhado pela própria GPU da TV em C com SDL2 e
OpenGL ES 2, para LG webOS 3 ou mais novo, Samsung Tizen e Android TV. O backend é o servidor
local de [StremCompat/stremcompat](https://github.com/StremCompat/stremcompat).

> **Beta, ainda não estável.** Para instalar, vá à página da sua TV:
> [LG](https://github.com/StremCompat/webos/blob/main/README.pt.md) · [Samsung](https://github.com/StremCompat/samsung/blob/main/README.pt.md) ·
> [Android TV](https://github.com/StremCompat/android-tv/blob/main/README.pt.md). Para o uso do dia a dia, a versão web
> (StremCompat 4.5) é a estável.

## Compilar

```
tools/fetch-deps.sh
./build.sh win          # Windows, para desenvolvimento e os testes de captura
```

`build/win/stremcompat.exe --shot out.png [--keys script.txt]` desenha fora da tela em 1920x1080
e salva a imagem.

## Terceiros

- SDL2 (licença zlib)
- stb_image, stb_truetype, stb_image_write (domínio público)
- Fonte Inter (SIL Open Font License, `assets/fonts/Inter-LICENSE.txt`)

Licenciado sob a GPL-3.0-only, como o StremCompat.
