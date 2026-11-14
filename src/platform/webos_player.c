/* webOS: the system's media service (com.webos.media, the one the browser's <video> uses)
   plays the address and draws the picture on the video plane under the app's window. The app
   talks to it over the Luna bus: load, subscribe, play, pause, seek, unload. What it reports
   (position, length, tracks, the end, errors) comes back through the subscription.

   The picture goes to the app's exported video window when the TV's SDL has them (webOS 4 and
   newer); otherwise to the app's first window, as the older systems expect. */
#include "player.h"

#include <SDL.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "util.h"
#include "webos_luna.h"

#define MEDIA "luna://com.webos.media/"

#define LOAD_LIMIT_MS 30000.0

static char media_id[128], window_id[128], error[256];
static char last_event[48]; /* the last news from the media service, for a report when it stalls */
static double loading_since;
static player_state status = PLAYER_IDLE;
static int buffering, ntracks, audio_at;
static double position, duration, start_at;
static player_track tracks[PLAYER_MAX_TRACKS];
static LSMessageToken subscription;

/* ---------- the bus ---------- */
static int call(const char *method, const char *payload, LSFilterFunc reply, LSMessageToken *token) {
  char uri[128];
  snprintf(uri, sizeof uri, MEDIA "%s", method);
  if (luna_call(uri, payload, reply, token)) return 1;
  snprintf(error, sizeof error, "%s", luna_error());
  return 0;
}

/* a call that names the media and nothing else: play, pause, unload */
static void simple(const char *method) {
  char payload[200];
  if (!media_id[0]) return;
  snprintf(payload, sizeof payload, "{\"mediaId\":\"%s\"}", media_id);
  call(method, payload, NULL, NULL);
}

/* ---------- what the media service reports ---------- */
static void read_tracks(const jval *program) {
  const jval *list = json_get(program, "audioTrackInfo");
  ntracks = 0;
  for (int i = 0; i < json_len(list) && ntracks < PLAYER_MAX_TRACKS; i++) {
    const jval *t = json_at(list, i);
    player_track *out = &tracks[ntracks++];
    snprintf(out->language, sizeof out->language, "%s", json_str(json_get(t, "language"), ""));
    const char *codec = json_str(json_get(t, "codec"), "");
    int channels = (int)json_num(json_get(t, "channels"), 0);
    snprintf(out->name, sizeof out->name, "%s%s%s", codec, channels > 2 ? " " : "",
             channels >= 6 ? (channels >= 8 ? "7.1" : "5.1") : "");
  }
}

static bool on_event(LSHandle *sh, LSMessage *msg, void *ctx) {
  (void)sh;
  (void)ctx;
  const char *text = LSMessageGetPayload(msg);
  jdoc *d = text ? json_parse(text, strlen(text)) : NULL;
  const jval *e = json_root(d);
  if (e && e->type == J_OBJ && json_len(e) > 0) {
    const jval *first = json_at(e, 0);
    if (first && first->key && strcmp(first->key, "returnValue") && strcmp(first->key, "currentTime"))
      snprintf(last_event, sizeof last_event, "%s", first->key);
  }
  if (json_get(e, "currentTime")->type == J_NUM) position = json_num(json_get(e, "currentTime"), position);
  const jval *source = json_get(e, "sourceInfo");
  if (source->type == J_OBJ) {
    const jval *program = json_at(json_get(source, "programInfo"), 0);
    duration = json_num(json_get(source, "duration"), json_num(json_get(program, "duration"), duration));
    read_tracks(program);
  }
  if (json_get(e, "loadCompleted")->type != J_NULL) {
    if (start_at > 0) {
      char payload[200];
      snprintf(payload, sizeof payload, "{\"mediaId\":\"%s\",\"position\":%.0f}", media_id, start_at);
      call("seek", payload, NULL, NULL);
    }
    simple("play");
  }
  if (json_get(e, "playing")->type != J_NULL) status = PLAYER_PLAYING;
  if (json_get(e, "paused")->type != J_NULL) status = PLAYER_PAUSED;
  if (json_get(e, "bufferingStart")->type != J_NULL) buffering = 1;
  if (json_get(e, "bufferingEnd")->type != J_NULL) buffering = 0;
  if (json_get(e, "endOfStream")->type != J_NULL) status = PLAYER_ENDED;
  const jval *failure = json_get(e, "error");
  if (failure->type == J_OBJ) {
    status = PLAYER_FAILED;
    snprintf(error, sizeof error, "%d %s", (int)json_num(json_get(failure, "errorCode"), 0),
             json_str(json_get(failure, "errorText"), ""));
  }
  json_free(d);
  return true;
}

static bool on_loaded(LSHandle *sh, LSMessage *msg, void *ctx) {
  (void)sh;
  (void)ctx;
  const char *text = LSMessageGetPayload(msg);
  jdoc *d = text ? json_parse(text, strlen(text)) : NULL;
  const char *id = json_str(json_get(json_root(d), "mediaId"), NULL);
  if (id && json_bool(json_get(json_root(d), "returnValue"), 0)) {
    snprintf(media_id, sizeof media_id, "%s", id);
    char payload[200];
    snprintf(payload, sizeof payload, "{\"mediaId\":\"%s\"}", media_id);
    call("subscribe", payload, on_event, &subscription);
    /* "loadCompleted" can come before the subscription does; play waits for the load anyway */
    simple("play");
    snprintf(last_event, sizeof last_event, "loaded");
  } else {
    status = PLAYER_FAILED;
    snprintf(error, sizeof error, "load: %s", json_str(json_get(json_root(d), "errorText"), text ? text : ""));
  }
  json_free(d);
  return true;
}

/* ---------- the window the picture goes to ---------- */
static void choose_window(void) {
  typedef const char *(*create_fn)(int);
  typedef SDL_bool (*place_fn)(const char *, SDL_Rect *, SDL_Rect *);
  static int tried;
  if (tried) return;
  tried = 1;
  snprintf(window_id, sizeof window_id, "_Window_Id_1");
  /* looked up at run time: the SDL of webOS 3 has no exported windows */
  void *self = dlopen(NULL, RTLD_NOW);
  create_fn create = self ? (create_fn)dlsym(self, "SDL_webOSCreateExportedWindow") : NULL;
  place_fn place = self ? (place_fn)dlsym(self, "SDL_webOSSetExportedWindow") : NULL;
  const char *id = create ? create(0 /* SDL_WEBOS_EXPORED_WINDOW_TYPE_VIDEO */) : NULL;
  if (!id) return;
  int w = 1920, h = 1080;
  SDL_Window *win = SDL_GL_GetCurrentWindow();
  if (win) SDL_GetWindowSize(win, &w, &h);
  SDL_Rect whole = {0, 0, w, h};
  if (place) place(id, &whole, &whole);
  snprintf(window_id, sizeof window_id, "%s", id);
}

/* ---------- the player ---------- */
int player_open(const char *url, double start_ms) {
  player_close();
  error[0] = 0;
  if (!luna_ready()) {
    snprintf(error, sizeof error, "%s", luna_error());
    status = PLAYER_FAILED;
    return -1;
  }
  choose_window();
  status = PLAYER_LOADING;
  position = start_ms;
  duration = 0;
  start_at = start_ms;
  buffering = 1;
  loading_since = now_ms();
  snprintf(last_event, sizeof last_event, "load asked");
  ntracks = audio_at = 0;
  sbuf p = {0};
  sb_add(&p, "{\"uri\":");
  sb_add_json_str(&p, url);
  sb_add(&p, ",\"type\":\"media\",\"payload\":{\"option\":{\"appId\":\"" SC_APP_ID "\",\"windowId\":");
  sb_add_json_str(&p, window_id);
  sb_add(&p, "}}}");
  LSMessageToken token;
  int ok = call("load", p.s, on_loaded, &token);
  sb_free(&p);
  if (!ok) status = PLAYER_FAILED;
  return ok ? 0 : -1;
}

void player_close(void) {
  if (media_id[0]) {
    simple("unload");
    luna_cancel(subscription);
  }
  media_id[0] = 0;
  subscription = 0;
  status = PLAYER_IDLE;
}

void player_play(void) {
  simple("play");
  if (status == PLAYER_PAUSED) status = PLAYER_PLAYING;
}

void player_pause(void) {
  simple("pause");
  if (status == PLAYER_PLAYING) status = PLAYER_PAUSED;
}

void player_seek(double ms) {
  char payload[200];
  if (!media_id[0]) return;
  snprintf(payload, sizeof payload, "{\"mediaId\":\"%s\",\"position\":%.0f}", media_id, ms < 0 ? 0 : ms);
  call("seek", payload, NULL, NULL);
  position = ms;
}

/* a video that never starts says so, with what the media service said last */
void player_poll(void) {
  luna_poll();
  if (status == PLAYER_LOADING && now_ms() - loading_since > LOAD_LIMIT_MS) {
    status = PLAYER_FAILED;
    snprintf(error, sizeof error, "no picture after 30 s (%s, window %s)", last_event, window_id);
  }
}

player_state player_status(void) { return status; }
int player_buffering(void) { return buffering; }
double player_position(void) { return position; }
double player_duration(void) { return duration; }
const char *player_error(void) { return error; }

int player_audio_tracks(player_track *out, int max) {
  int n = ntracks < max ? ntracks : max;
  memcpy(out, tracks, (size_t)n * sizeof *out);
  return n;
}

int player_audio_current(void) { return audio_at; }

int player_set_speed(double rate) {
  char payload[200];
  if (!media_id[0]) return 0;
  snprintf(payload, sizeof payload, "{\"mediaId\":\"%s\",\"playRate\":%.2f,\"audioOutput\":true}", media_id, rate);
  return call("setPlayRate", payload, NULL, NULL);
}

void player_select_audio(int index) {
  char payload[200];
  if (!media_id[0] || index < 0 || index >= ntracks) return;
  snprintf(payload, sizeof payload, "{\"mediaId\":\"%s\",\"type\":\"audio\",\"index\":%d}", media_id, index);
  if (call("selectTrack", payload, NULL, NULL)) audio_at = index;
}
