/* LG webOS 3 and newer. */
#include "platform.h"

#include <SDL_webOS.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
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

/* the service starts the local server (service/launch.js); the call is what wakes it. A refusal
   is told on screen, with what the service saw, as the web launcher did. */
static char service_note[200], direct_note[200];

const char *platform_server_status(void) {
  static char both[420];
  snprintf(both, sizeof both, "%s%s%s", service_note, direct_note[0] ? " \xC2\xB7 " : "", direct_note);
  return both;
}

static bool start_answered(LSHandle *sh, LSMessage *msg, void *ctx) {
  (void)sh;
  (void)ctx;
  const char *text = LSMessageGetPayload(msg);
  jdoc *d = text ? json_parse(text, strlen(text)) : NULL;
  const jval *root = json_root(d);
  if (json_bool(json_get(root, "ready"), 0)) snprintf(service_note, sizeof service_note, "service ready");
  else
    snprintf(service_note, sizeof service_note, "service %s %s %s", json_str(json_get(root, "errorCode"), ""),
             json_str(json_get(root, "errorText"), json_str(json_get(root, "reason"), "not ready")), json_str(json_get(root, "log"), ""));
  fprintf(stderr, "start: %s\n", text ? text : "");
  json_free(d);
  return true;
}

/* The service did not answer: the server is started here, as the service does it (launch.js
   startRuntime): the TV's Node running local-server.js from the service's folder, in a session of
   its own so it outlives this process. What it prints goes to /tmp/stremcompat-server.log. */
void platform_run_server(void) {
  static const char *nodes[] = {"/usr/bin/node", "/usr/bin/nodejs", "/usr/local/bin/node"};
  const char *node = NULL;
  for (size_t i = 0; i < sizeof nodes / sizeof nodes[0] && !node; i++)
    if (access(nodes[i], X_OK) == 0) node = nodes[i];
  char dir[600], script[700];
  char *base = SDL_GetBasePath(); /* .../usr/palm/applications/<app id>/ */
  snprintf(dir, sizeof dir, "%s../../services/io.github.alexesmm.legacytv.server", base ? base : "");
  SDL_free(base);
  snprintf(script, sizeof script, "%s/local-server.js", dir);
  if (!node) {
    snprintf(direct_note, sizeof direct_note, "direct: no node");
    return;
  }
  if (access(script, R_OK) != 0) {
    snprintf(direct_note, sizeof direct_note, "direct: no server at %.150s", dir);
    return;
  }
  pid_t pid = fork();
  if (pid == 0) {
    setsid();
    int log = open("/tmp/stremcompat-server.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log >= 0) {
      dup2(log, 1);
      dup2(log, 2);
    }
    if (chdir(dir) == 0) {
      if (!getenv("HOME")) setenv("HOME", "/home/root", 1);
      execl(node, node, "--max_semi_space_size=2", "local-server.js", (char *)NULL);
    }
    _exit(127);
  }
  snprintf(direct_note, sizeof direct_note, pid > 0 ? "direct: started %s" : "direct: fork failed", node);
  fprintf(stderr, "%s\n", direct_note);
}

void platform_start_server(void) {
  LSMessageToken token;
  snprintf(service_note, sizeof service_note, "service asked, no answer");
  if (!luna_call("luna://io.github.alexesmm.legacytv.server/start", "{}", start_answered, &token))
    snprintf(service_note, sizeof service_note, "service call refused: %s", luna_error());
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
