/* webOS: the system's media service (com.webos.media, the one the browser's <video> uses)
   plays the address and draws the picture on the video plane under the app's window. The app
   talks to it over the Luna bus: load, notifyForeground, subscribe, play, pause, seek, unload. What
   it reports (position, length, picture, tracks, the end, errors) comes back through the
   subscription.

   Where the picture goes depends on the system:
   - webOS 5 and some 4: the TV's SDL makes an exported video window, named in the load;
   - webOS 3 and 4 without one: the load names a placeholder window and, once the video has
     loaded, the TV's ACB library ties it to the main plane (webos_acb.c);
   - neither: the app's first window, as the oldest systems expect. */
#include "player.h"

#include <SDL.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "util.h"
#include "webos_acb.h"
#include "webos_luna.h"

#define MEDIA "luna://com.webos.media/"

#define LOAD_LIMIT_MS 30000.0
#define SEEK_SETTLE_MS 350.0  /* one seek sent once the keys rest: several in a row killed the pipeline */
#define SEEK_QUIET_MS 8000.0  /* the position reports after a seek can still say the old place */
#define SEEK_TRIES 3

static char media_id[128], window_id[128], error[256], url[4096];
static char last_event[48]; /* the last news from the media service, for a report when it stalls */
static const char *path = "first window"; /* where the picture goes, for the report */
static double loading_since;
static player_state status = PLAYER_IDLE;
static int buffering, ntracks, audio_at, reloads, audio_failed, hdr;
static double position, duration, start_at;
static double reported_at; /* when the position last came, to move it on between reports */
static double last_time = -1; /* the position the service reported last */
static double seek_to = -1, seek_asked_at, seek_sent_at, seek_retry_at;
static int seek_tries;
static player_track tracks[PLAYER_MAX_TRACKS];
static LSMessageToken subscription, saver;
/* each load is numbered: the answer to one already left (closed, or another video asked) frees
   its pipeline, which would otherwise hold the TV's decoder and stop the next video */
static unsigned loads;

/* ---------- the bus ---------- */
static int call(const char *method, const char *payload, LSFilterFunc reply, LSMessageToken *token) {
  char uri[128];
  snprintf(uri, sizeof uri, MEDIA "%s", method);
  if (luna_call(uri, payload, reply, NULL, token)) return 1;
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

/* ---------- the TV's screensaver: it does not count this video as one, so it is told no ---------- */
static bool on_saver(LSHandle *sh, LSMessage *msg, void *ctx) {
  (void)sh;
  (void)ctx;
  const char *text = LSMessageGetPayload(msg);
  jdoc *d = text ? json_parse(text, strlen(text)) : NULL;
  const jval *stamp = json_get(json_root(d), "timestamp");
  if (stamp->type != J_NULL && status != PLAYER_IDLE && status != PLAYER_ENDED && status != PLAYER_FAILED) {
    char payload[200];
    snprintf(payload, sizeof payload, "{\"clientName\":\"%s\",\"ack\":false,\"timestamp\":%.0f}", SC_APP_ID,
             json_num(stamp, 0));
    luna_call("luna://com.webos.service.tvpower/power/responseScreenSaverRequest", payload, NULL, NULL, NULL);
  }
  json_free(d);
  return true;
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

/* a number the service sends either as is or wrapped in an object of the same name
   ({"currentTime":{"currentTime":8580,"mediaId":...}}) */
static double number_of(const jval *e, const char *key, double fallback) {
  const jval *v = json_get(e, key);
  if (v->type == J_OBJ) v = json_get(v, key);
  return v->type == J_NUM ? json_num(v, fallback) : fallback;
}

static void send_seek(double ms) {
  char payload[200];
  if (!media_id[0]) return;
  snprintf(payload, sizeof payload, "{\"mediaId\":\"%s\",\"position\":%.0f}", media_id, ms < 0 ? 0 : ms);
  call("seek", payload, NULL, NULL);
  seek_sent_at = now_ms();
}

static void reload_here(void);

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
  /* the position, except while a seek settles: the reports can still say the old place */
  double t = number_of(e, "currentTime", -1);
  if (t >= 0 && seek_to < 0 && now_ms() - seek_sent_at > SEEK_QUIET_MS) {
    position = t;
    reported_at = now_ms();
  }
  /* the time moving is the video playing, whether or not "playing" and "bufferingEnd" came (some
     firmware leaves them out): the cover and the dimming would otherwise hide the picture */
  if (t >= 0) {
    if (last_time >= 0 && t > last_time && t - last_time < 5000) {
      buffering = 0;
      if (status == PLAYER_LOADING) status = PLAYER_PLAYING;
    }
    last_time = t;
  }
  if (json_get(e, "seekDone")->type != J_NULL) seek_sent_at = 0, seek_tries = 0;
  const jval *source = json_get(e, "sourceInfo");
  if (source->type == J_OBJ) {
    const jval *program = json_at(json_get(source, "programInfo"), 0);
    duration = json_num(json_get(source, "duration"), json_num(json_get(program, "duration"), duration));
    read_tracks(program);
  }
  double d_ms = number_of(e, "duration", -1);
  if (d_ms > 0) duration = d_ms;
  /* the picture's real size, rate and range, which the plane is told of */
  const jval *video = json_get(e, "videoInfo");
  if (video->type == J_OBJ) {
    const char *kind = json_str(json_get(video, "hdrType"), "");
    hdr = *kind && strcmp(kind, "none") && strcmp(kind, "NONE");
  }
  if (video->type == J_OBJ)
    acb_video_info((int)json_num(json_get(video, "width"), 0), (int)json_num(json_get(video, "height"), 0),
                   json_num(json_get(video, "frameRate"), 0), json_num(json_get(video, "bitRate"), 0),
                   json_str(json_get(video, "hdrType"), NULL));
  if (json_get(e, "loadCompleted")->type != J_NULL) {
    /* the place to resume from is asked only now: a seek before the load is lost */
    if (start_at > 0) send_seek(start_at);
    start_at = 0;
    simple("play");
    if (!strcmp(path, "ACB")) acb_bind(media_id, 0);
  }
  if (json_get(e, "playing")->type != J_NULL) {
    status = PLAYER_PLAYING;
    reloads = 0;
    acb_set_playing(1);
  }
  if (json_get(e, "paused")->type != J_NULL) {
    status = PLAYER_PAUSED;
    acb_set_playing(0);
  }
  if (json_get(e, "bufferingStart")->type != J_NULL) buffering = 1;
  if (json_get(e, "bufferingEnd")->type != J_NULL) buffering = 0;
  if (json_get(e, "endOfStream")->type != J_NULL) status = PLAYER_ENDED;
  const jval *failure = json_get(e, "error");
  if (failure->type == J_OBJ) {
    int code = (int)json_num(json_get(failure, "errorCode"), 0);
    const char *what = json_str(json_get(failure, "errorText"), "");
    if (code == 200) {
      /* the sound's format is not one this TV plays: the picture goes on, another track can be chosen */
      snprintf(last_event, sizeof last_event, "audio not supported");
      audio_failed = 1;
    } else if (strstr(what, "is not running") && reloads < 2) {
      /* the TV's pipeline died (it happens a while after a seek): the same video, from here */
      reloads++;
      reload_here();
    } else if ((code == 107 || code == 108 || code == 700 || strstr(what, "seek Failure")) && seek_tries < SEEK_TRIES) {
      seek_tries++;
      seek_retry_at = now_ms() + 800.0 * seek_tries;
    } else {
      status = PLAYER_FAILED;
      snprintf(error, sizeof error, "%d %s", code, what);
    }
  }
  json_free(d);
  return true;
}

static bool on_loaded(LSHandle *sh, LSMessage *msg, void *ctx) {
  (void)sh;
  const char *text = LSMessageGetPayload(msg);
  jdoc *d = text ? json_parse(text, strlen(text)) : NULL;
  const char *id = json_str(json_get(json_root(d), "mediaId"), NULL);
  if ((unsigned)(uintptr_t)ctx != loads) {
    if (id) {
      char payload[200];
      snprintf(payload, sizeof payload, "{\"mediaId\":\"%s\"}", id);
      call("unload", payload, NULL, NULL);
    }
  } else if (id && json_bool(json_get(json_root(d), "returnValue"), 0)) {
    snprintf(media_id, sizeof media_id, "%s", id);
    char payload[200];
    snprintf(payload, sizeof payload, "{\"connectionId\":\"%s\"}", media_id);
    call("notifyForeground", payload, NULL, NULL);
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
  /* looked up at run time: the SDL of webOS 3 and of some webOS 4 has no exported windows */
  void *self = dlopen(NULL, RTLD_NOW);
  create_fn create = self ? (create_fn)dlsym(self, "SDL_webOSCreateExportedWindow") : NULL;
  place_fn place = self ? (place_fn)dlsym(self, "SDL_webOSSetExportedWindow") : NULL;
  const char *id = create ? create(0 /* SDL_WEBOS_EXPORED_WINDOW_TYPE_VIDEO */) : NULL;
  if (id) {
    int w = 1920, h = 1080;
    SDL_Window *win = SDL_GL_GetCurrentWindow();
    if (win) SDL_GetWindowSize(win, &w, &h);
    /* the whole frame as the source, never none: some TVs drop the connection on a missing one */
    SDL_Rect whole = {0, 0, w, h};
    if (place) place(id, &whole, &whole);
    snprintf(window_id, sizeof window_id, "%s", id);
    path = "exported window";
  } else if (acb_available()) {
    /* a placeholder window: an empty one takes the load and never fetches the video */
    snprintf(window_id, sizeof window_id, "window_id_dummy");
    path = "ACB";
  } else {
    snprintf(window_id, sizeof window_id, "_Window_Id_1");
    path = "first window";
  }
}

/* ---------- the player ---------- */
static int load(double from_ms) {
  status = PLAYER_LOADING;
  last_time = -1;
  position = from_ms;
  reported_at = now_ms();
  duration = 0;
  start_at = from_ms;
  buffering = 1;
  seek_to = -1;
  seek_sent_at = 0;
  seek_tries = 0;
  seek_retry_at = 0; /* a retry belongs to the video that asked for it */
  loading_since = now_ms();
  snprintf(last_event, sizeof last_event, "load asked");
  /* nothing of the last video's picture is taken for this one's */
  ntracks = audio_at = hdr = 0;
  sbuf p = {0};
  sb_add(&p, "{\"uri\":");
  sb_add_json_str(&p, url);
  sb_add(&p, ",\"type\":\"media\",\"payload\":{\"option\":{\"appId\":\"" SC_APP_ID "\",\"useSeekableRanges\":true,"
             "\"bufferControl\":{\"userBufferCtrl\":false},\"windowId\":");
  sb_add_json_str(&p, window_id);
  sb_add(&p, "}}}");
  LSMessageToken token;
  int ok = luna_call(MEDIA "load", p.s, on_loaded, (void *)(uintptr_t)++loads, &token);
  if (!ok) snprintf(error, sizeof error, "%s", luna_error());
  sb_free(&p);
  if (!ok) status = PLAYER_FAILED;
  return ok ? 0 : -1;
}

static void let_go(void) {
  loads++; /* a load still unanswered is no longer wanted */
  seek_retry_at = 0;
  seek_to = -1;
  if (media_id[0]) {
    simple("unload");
    luna_cancel(subscription);
  }
  media_id[0] = 0;
  subscription = 0;
}

/* the same address again from where it was, after the TV's pipeline died */
static void reload_here(void) {
  double at = player_position();
  let_go();
  load(at);
}

int player_open(const char *address, double start_ms) {
  player_close();
  error[0] = 0;
  reloads = 0;
  if (!luna_ready()) {
    snprintf(error, sizeof error, "%s", luna_error());
    status = PLAYER_FAILED;
    return -1;
  }
  choose_window();
  snprintf(url, sizeof url, "%s", address ? address : "");
  if (!saver)
    luna_call("luna://com.webos.service.tvpower/power/registerScreenSaverRequest",
              "{\"subscribe\":true,\"clientName\":\"" SC_APP_ID "\"}", on_saver, NULL, &saver);
  return load(start_ms);
}

void player_close(void) {
  let_go();
  acb_release();
  if (saver) luna_cancel(saver);
  saver = 0;
  status = PLAYER_IDLE;
}

void player_play(void) {
  simple("play");
  if (status == PLAYER_PAUSED) status = PLAYER_PLAYING;
  reported_at = now_ms();
}

void player_pause(void) {
  simple("pause");
  if (status == PLAYER_PLAYING) status = PLAYER_PAUSED;
}

/* the place shows at once; the one seek goes out once the keys rest */
void player_seek(double ms) {
  if (!media_id[0]) return;
  seek_to = ms < 0 ? 0 : ms;
  seek_asked_at = now_ms();
  position = seek_to;
  reported_at = now_ms();
}

/* a video that never starts says so, with what the media service said last and where the picture
   was sent */
void player_poll(void) {
  luna_poll();
  if (seek_to >= 0 && now_ms() - seek_asked_at > SEEK_SETTLE_MS) {
    send_seek(seek_to);
    seek_to = -1;
  }
  if (seek_retry_at > 0 && now_ms() > seek_retry_at) {
    seek_retry_at = 0;
    send_seek(position);
  }
  if (status == PLAYER_LOADING && now_ms() - loading_since > LOAD_LIMIT_MS) {
    status = PLAYER_FAILED;
    snprintf(error, sizeof error, "no picture after 30 s (%s, %s %s, bus %s)", last_event, path, window_id,
             luna_name());
  }
}

player_state player_status(void) { return status; }
int player_buffering(void) { return buffering; }
/* between the service's reports (some TVs send one every few seconds) the clock moves it on */
double player_position(void) {
  if (status == PLAYER_PLAYING && !buffering && seek_to < 0 && reported_at > 0) {
    double ahead = now_ms() - reported_at;
    return position + (ahead < 5000 ? ahead : 5000);
  }
  return position;
}
double player_duration(void) { return duration; }
const char *player_error(void) { return error; }

int player_audio_tracks(player_track *out, int max) {
  int n = ntracks < max ? ntracks : max;
  memcpy(out, tracks, (size_t)n * sizeof *out);
  return n;
}

int player_audio_current(void) { return audio_at; }

int player_hdr(void) { return hdr; }

int player_audio_failed(void) {
  int failed = audio_failed;
  audio_failed = 0;
  return failed;
}

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
