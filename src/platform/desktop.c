/* Windows: for development and the screenshot tests. */
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "text.h"

void platform_init(void) {}

/* on the computer the server is started by hand (tools/dev-server.js) */
void platform_start_server(void) {}

/* the keyboard stands in for the remote: Escape and Backspace are Back, F1 to F4 the colours */
app_key platform_key(const SDL_Event *ev) {
  if (ev->type != SDL_KEYDOWN) return K_NONE;
  switch (ev->key.keysym.sym) {
  case SDLK_ESCAPE:
  case SDLK_BACKSPACE: return K_BACK;
  case SDLK_F1: return K_RED;
  case SDLK_F2: return K_GREEN;
  case SDLK_F3: return K_YELLOW;
  case SDLK_F4: return K_BLUE;
  default: return K_NONE;
  }
}

void platform_add_fonts(void) {
  /* Korean, Japanese, Simplified and Traditional Chinese, then symbols */
  static const char *names[] = {"malgun.ttf", "YuGothR.ttc", "msgothic.ttc", "msyh.ttc", "msjh.ttc", "seguisym.ttf"};
  const char *windir = getenv("WINDIR");
  char path[512];
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
    snprintf(path, sizeof path, "%s/Fonts/%s", windir ? windir : "C:/Windows", names[i]);
    FILE *f = fopen(path, "rb");
    if (!f) continue;
    fclose(f);
    text_add_fallback(path);
  }
}

