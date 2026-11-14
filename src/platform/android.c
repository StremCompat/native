/* Android TV: SDL's own activity (NativeActivity.java in the Android project) shows the window
   over the video surface; the local server runs on the embedded Node the activity starts. */
#include "platform.h"

#include <stdio.h>

#include "text.h"

void platform_init(void) {
  /* Back comes to the app, and the window may be see-through where the video plays */
  SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
  SDL_SetHint(SDL_HINT_VIDEO_EGL_ALLOW_TRANSPARENCY, "1");
}

Uint32 platform_window_flags(void) { return SDL_WINDOW_FULLSCREEN; }

/* the activity turns the colour buttons into F1 to F4, which SDL knows */
app_key platform_key(const SDL_Event *ev) {
  if (ev->type != SDL_KEYDOWN) return K_NONE;
  switch (ev->key.keysym.sym) {
  case SDLK_AC_BACK:
  case SDLK_ESCAPE: return K_BACK;
  case SDLK_F1: return K_RED;
  case SDLK_F2: return K_GREEN;
  case SDLK_F3: return K_YELLOW;
  case SDLK_F4: return K_BLUE;
  default: return K_NONE;
  }
}

void platform_poll(void) {}

void platform_language(char *out, int size) {
  SDL_Locale *list = SDL_GetPreferredLocales();
  out[0] = 0;
  if (list && list[0].language)
    snprintf(out, (size_t)size, "%s%s%s", list[0].language, list[0].country ? "-" : "", list[0].country ? list[0].country : "");
  SDL_free(list);
}

/* the activity started Node before the window; nothing to wake */
void platform_start_server(void) {}

/* the update is a webOS package; Android versions come through the APK release */
void platform_install_update(void (*done)(const char *error)) { done("android"); }

/* the system's fonts include the CJK ones (Noto) */
void platform_add_fonts(void) {
  static const char *fonts[] = {"/system/fonts/NotoSansCJK-Regular.ttc", "/system/fonts/NotoSansSymbols-Regular-Subsetted.ttf",
                                "/system/fonts/DroidSansFallback.ttf"};
  for (size_t i = 0; i < sizeof fonts / sizeof fonts[0]; i++) text_add_fallback(fonts[i]);
}
