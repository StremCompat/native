/* LG webOS 3 and newer. */
#include "platform.h"

#include <SDL_webOS.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "text.h"

void platform_init(void) {
  /* Back comes to the app instead of closing it; the app decides when Back means leave */
  SDL_SetHint(SDL_HINT_WEBOS_ACCESS_POLICY_KEYS_BACK, "true");
  SDL_SetHint(SDL_HINT_WEBOS_REGISTER_APP, "true");
  SDL_SetHint(SDL_HINT_WEBOS_CURSOR_SLEEP_TIME, "5000");
}

app_key platform_key(const SDL_Event *ev) {
  if (ev->type != SDL_KEYDOWN) return K_NONE;
  switch ((int)ev->key.keysym.scancode) {
  case SDL_WEBOS_SCANCODE_BACK: return K_BACK;
  case SDL_WEBOS_SCANCODE_RED: return K_RED;
  case SDL_WEBOS_SCANCODE_GREEN: return K_GREEN;
  case SDL_WEBOS_SCANCODE_YELLOW: return K_YELLOW;
  case SDL_WEBOS_SCANCODE_BLUE: return K_BLUE;
  default: return K_NONE;
  }
}

/* every font file under the folder; the text module opens one only when a character needs it */
static void add_fonts(const char *dir, int depth) {
  DIR *d = opendir(dir);
  if (!d) return;
  struct dirent *e;
  while ((e = readdir(d))) {
    if (e->d_name[0] == '.') continue;
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
    const char *dot = strrchr(e->d_name, '.');
    if (dot && (!strcasecmp(dot, ".ttf") || !strcasecmp(dot, ".otf") || !strcasecmp(dot, ".ttc")))
      text_add_fallback(path);
    else if (depth < 4)
      add_fonts(path, depth + 1);
  }
  closedir(d);
}

/* the TV's own fonts include Korean, Japanese and Chinese ones */
void platform_add_fonts(void) { add_fonts("/usr/share/fonts", 0); }
