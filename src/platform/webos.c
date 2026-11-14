/* LG webOS 3 and newer. */
#include "platform.h"

#include <SDL_webOS.h>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "json.h"
#include "text.h"
#include "webos_luna.h"

Uint32 platform_window_flags(void) { return SDL_WINDOW_FULLSCREEN; }

/* a notification from the system, which shows even when the app's window is gone */
void platform_fatal(const char *message) {
  char payload[400];
  snprintf(payload, sizeof payload, "{\"sourceId\":\"io.github.alexesmm.legacytv\",\"message\":\"");
  size_t n = strlen(payload);
  for (const char *c = message; *c && n < sizeof payload - 4; c++)
    if (*c != '"' && *c != '\\' && (unsigned char)*c >= 32) payload[n++] = *c;
  snprintf(payload + n, sizeof payload - n, "\"}");
  luna_call("luna://com.webos.notification/createToast", payload, NULL, NULL);
  for (int i = 0; i < 20; i++) {
    luna_poll();
    usleep(50000);
  }
}

/* a crash still says at which step it happened */
static void crashed(int sig) {
  char message[160];
  snprintf(message, sizeof message, "StremCompat closed: signal %d during %s", sig, app_stage);
  fprintf(stderr, "%s\n", message);
  platform_fatal(message);
  _exit(1);
}

void platform_init(void) {
  /* what goes wrong is kept where it can be read back (Settings > Diagnostics, or over ssh) */
  int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
  for (int i = 0; i < 5; i++) signal(signals[i], crashed);
  if (freopen("/tmp/stremcompat-native.log", "w", stderr)) setvbuf(stderr, NULL, _IOLBF, 0);
  /* Back comes to the app instead of closing it; the app decides when Back means leave */
  SDL_SetHint(SDL_HINT_WEBOS_ACCESS_POLICY_KEYS_BACK, "true");
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

void platform_poll(void) { luna_poll(); }

/* the system settings know the language of the TV's menus (localeInfo.locales.UI) */
static char language[32];
static int language_answered;

static bool language_came(LSHandle *sh, LSMessage *msg, void *ctx) {
  (void)sh;
  (void)ctx;
  const char *text = LSMessageGetPayload(msg);
  jdoc *d = text ? json_parse(text, strlen(text)) : NULL;
  const jval *ui = json_get(json_get(json_get(json_get(json_root(d), "settings"), "localeInfo"), "locales"), "UI");
  snprintf(language, sizeof language, "%s", json_str(ui, ""));
  json_free(d);
  language_answered = 1;
  return true;
}

void platform_language(char *out, int size) {
  LSMessageToken token;
  out[0] = 0;
  if (!luna_call("luna://com.webos.settingsservice/getSystemSettings", "{\"keys\":[\"localeInfo\"]}", language_came, &token))
    return;
  /* asked once at the start; the answer comes in a moment */
  for (int i = 0; i < 40 && !language_answered; i++) {
    luna_poll();
    SDL_Delay(25);
  }
  luna_cancel(token);
  snprintf(out, (size_t)size, "%s", language);
}

/* the service starts the local server (service/launch.js); the call is what wakes it */
void platform_start_server(void) {
  luna_call("luna://io.github.alexesmm.legacytv.server/start", "{}", NULL, NULL);
  luna_poll();
}

/* the service hands the downloaded package to the system installer (service/launch.js) */
static void (*install_done)(const char *error);

static bool install_answered(LSHandle *sh, LSMessage *msg, void *ctx) {
  (void)sh;
  (void)ctx;
  const char *text = LSMessageGetPayload(msg);
  jdoc *d = text ? json_parse(text, strlen(text)) : NULL;
  int ok = json_bool(json_get(json_root(d), "returnValue"), 0);
  if (install_done) install_done(ok ? "" : json_str(json_get(json_root(d), "error"), "refused"));
  json_free(d);
  return true;
}

void platform_install_update(void (*done)(const char *error)) {
  LSMessageToken token;
  install_done = done;
  if (!luna_call("luna://io.github.alexesmm.legacytv.server/install", "{}", install_answered, &token)) done(luna_error());
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
