/* Samsung Tizen, as WebAssembly inside the app's web package (index.html starts the service and
   then this). The remote's own buttons are asked of the TV and reach the app as the keys the
   computer build uses: Back as Escape, the colours as F1 to F4. */
#include "platform.h"

#include <emscripten.h>
#include <stdio.h>

void platform_init(void) {}

Uint32 platform_window_flags(void) { return 0; }

EMSCRIPTEN_KEEPALIVE void tizen_key(int code) {
  SDL_Event ev = {.type = SDL_KEYDOWN};
  switch (code) {
  case 10009: ev.key.keysym.sym = SDLK_ESCAPE; break;
  case 403: ev.key.keysym.sym = SDLK_F1; break;
  case 404: ev.key.keysym.sym = SDLK_F2; break;
  case 405: ev.key.keysym.sym = SDLK_F3; break;
  case 406: ev.key.keysym.sym = SDLK_F4; break;
  default: ev.key.keysym.sym = SDLK_AUDIOPLAY; break; /* play, pause, play-pause */
  }
  SDL_PushEvent(&ev);
}

EM_JS(void, js_take_keys, (void), {
  var names = ["ColorF0Red", "ColorF1Green", "ColorF2Yellow", "ColorF3Blue", "MediaPlay", "MediaPause", "MediaPlayPause"];
  names.forEach(function(name) {
    try {
      tizen.tvinputdevice.registerKey(name);
    } catch (e) {}
  });
  var ours = {10009: 1, 403: 1, 404: 1, 405: 1, 406: 1, 415: 1, 19: 1, 10252: 1};
  window.addEventListener("keydown", function(e) {
    if (!ours[e.keyCode]) return;
    e.preventDefault();
    e.stopImmediatePropagation();
    _tizen_key(e.keyCode);
  }, true);
});

app_key platform_key(const SDL_Event *ev) {
  if (ev->type != SDL_KEYDOWN) return K_NONE;
  switch (ev->key.keysym.sym) {
  case SDLK_ESCAPE: return K_BACK;
  case SDLK_F1: return K_RED;
  case SDLK_F2: return K_GREEN;
  case SDLK_F3: return K_YELLOW;
  case SDLK_F4: return K_BLUE;
  default: return K_NONE;
  }
}

void platform_poll(void) {}

const char *platform_server_status(void) { return ""; }
void platform_run_server(void) {}

void platform_fatal(const char *message) { fprintf(stderr, "%s\n", message); }

void platform_language(char *out, int size) {
  SDL_Locale *list = SDL_GetPreferredLocales();
  out[0] = 0;
  if (list && list[0].language)
    snprintf(out, (size_t)size, "%s%s%s", list[0].language, list[0].country ? "-" : "", list[0].country ? list[0].country : "");
  SDL_free(list);
}

/* index.html started the service before loading the app; the keys are asked for here */
void platform_start_server(void) { js_take_keys(); }

/* a Tizen package cannot install another; new versions come through the release */
void platform_install_update(void (*done)(const char *error)) { done("tizen"); }

/* the page carries only Inter; the TV's fonts are not reachable from it */
void platform_add_fonts(void) {}
/* no system link opens a title here */
int platform_take_link(char *type, char *id, int size) {
  (void)type;
  (void)id;
  (void)size;
  return 0;
}
