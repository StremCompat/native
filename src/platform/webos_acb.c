/* The TV's ACB library ties a media-service video to the main video plane on webOS 3 and 4, where
   SDL has no exported windows. It is the same path the TV's own browser takes. Every call goes to the
   TV's services over the bus and returns before they act on it, so the steps are spaced out on a
   thread of their own: sent back to back, the later ones arrive before the earlier are done and the
   picture stays black with the sound playing. The library is opened at run time and each function is
   optional (webOS 3.4 lacks the audio one; webOS 5 has no library at all), so one binary runs on every
   system. */
#include "webos_acb.h"

#include <SDL.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "util.h"
#include "webos_luna.h"

typedef void (*acb_callback)(long id, long task, long event, long app_state, long play_state, const char *reply);

static struct {
  long (*create)(void);
  int (*initialize)(long id, long player_type, const char *app_id, acb_callback cb);
  int (*set_sink_type)(long id, long sink);
  int (*set_media_id)(long id, const char *media_id);
  int (*set_state)(long id, long app_state, long play_state, long *task);
  int (*connect_dass)(long id, long sink, long *task);
  int (*set_video_data)(long id, const char *json, long *task);
  int (*set_audio_data)(long id, const char *json, long *task);
  int (*set_display_window)(long id, long x, long y, long w, long h, int full_screen, long *task);
  int (*finalize)(long id);
  int (*destroy)(long id);
} api;

enum { APP_FOREGROUND = 1, PLAY_LOADED = 1, PLAY_PLAYING = 2, PLAY_PAUSED = 3, PLAYER_URI = 0, SINK_MAIN = 0 };

static int looked, present;
static long handle;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int session, bound, want_paused;
static char media[128];
static struct {
  int width, height;
  double fps, bit_rate;
  char hdr[24];
} picture = {1920, 1080, 24, 0, "none"};

/* the library's events: it has to have somewhere to send them */
static void events(long id, long task, long event, long app_state, long play_state, const char *reply) {
  (void)id, (void)task, (void)event, (void)app_state, (void)play_state, (void)reply;
}

int acb_available(void) {
  if (looked) return present;
  looked = 1;
  void *lib = dlopen("libAcbAPI.so.1", RTLD_NOW | RTLD_GLOBAL);
  if (!lib) lib = dlopen("libAcbAPI.so", RTLD_NOW | RTLD_GLOBAL);
  if (!lib) return 0;
#define FIND(field, name) *(void **)&api.field = dlsym(lib, name)
  FIND(create, "AcbAPI_create");
  FIND(initialize, "AcbAPI_initialize");
  FIND(set_sink_type, "AcbAPI_setSinkType");
  FIND(set_media_id, "AcbAPI_setMediaId");
  FIND(set_state, "AcbAPI_setState");
  FIND(connect_dass, "AcbAPI_connectDass");
  FIND(set_video_data, "AcbAPI_setMediaVideoData");
  FIND(set_audio_data, "AcbAPI_setMediaAudioData");
  FIND(set_display_window, "AcbAPI_setDisplayWindow");
  FIND(finalize, "AcbAPI_finalize");
  FIND(destroy, "AcbAPI_destroy");
#undef FIND
  /* what the bind cannot do without */
  present = api.create && api.initialize && api.set_media_id && api.set_state && api.set_display_window;
  return present;
}

void acb_video_info(int width, int height, double fps, double bit_rate, const char *hdr_type) {
  pthread_mutex_lock(&lock);
  if (width > 0) picture.width = width;
  if (height > 0) picture.height = height;
  if (fps > 0) picture.fps = fps;
  if (bit_rate > 0) picture.bit_rate = bit_rate;
  if (hdr_type && *hdr_type) snprintf(picture.hdr, sizeof picture.hdr, "%s", hdr_type);
  pthread_mutex_unlock(&lock);
}

/* the system's major version: webOS before 5 names the colour blocks differently */
static int webos_major(void) {
  static int major = -1;
  if (major >= 0) return major;
  major = 4;
  char buf[512] = {0};
  FILE *f = fopen("/var/run/nyx/os_info.json", "r");
  if (f) {
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);
    const char *at = strstr(buf, "\"webos_release\"");
    if (at && (at = strchr(at + 15, '"'))) major = atoi(at + 1);
    return major;
  }
  f = fopen("/etc/starfish-release", "r");
  if (f) {
    if (fgets(buf, sizeof buf, f)) {
      const char *at = strstr(buf, "release ");
      if (at) major = atoi(at + 8);
    }
    fclose(f);
  }
  return major;
}

/* the plane's size: on ACB the rectangle is in the TV's display space, which some 2018 sets keep at
   1280x720 whatever the app's surface is */
static void plane_size(int *w, int *h) {
  SDL_DisplayMode mode;
  *w = 1920, *h = 1080;
  if (SDL_GetDesktopDisplayMode(0, &mode) == 0 && mode.w > 0 && mode.h > 0) *w = mode.w, *h = mode.h;
}

/* true while this bind is still the current one: a newer video, or the close, stops it */
static int still(int mine) { return mine == session; }

static void pause_ms(int ms) { usleep((useconds_t)ms * 1000); }

static void *bind_steps(void *arg) {
  int mine = (int)(intptr_t)arg;
  char id[128], json[1200];
  long task = 0;
  pthread_mutex_lock(&lock);
  snprintf(id, sizeof id, "%s", media);
  pthread_mutex_unlock(&lock);
  if (!still(mine)) return NULL;
  api.set_media_id(handle, id);
  pause_ms(200);
  if (!still(mine)) return NULL;
  api.set_state(handle, APP_FOREGROUND, PLAY_LOADED, &task);
  pause_ms(200);
  if (!still(mine)) return NULL;
  /* without it the plane decodes and plays the sound, and nothing reaches the screen */
  if (api.connect_dass) {
    api.connect_dass(handle, SINK_MAIN, &task);
    pause_ms(200);
  }
  if (!still(mine)) return NULL;
  if (api.set_video_data) {
    /* the TV's display service wants the media named in the data ("context"), and the real
       picture; a standard-range video sends its colour blocks empty */
    const char *sei = webos_major() < 5 ? "mediaSei" : "sei", *vui = webos_major() < 5 ? "mediaVui" : "vui";
    pthread_mutex_lock(&lock);
    snprintf(json, sizeof json,
             "{\"context\":\"%s\",\"content\":\"movie\",\"video\":{\"bitRate\":%.0f,\"frameRate\":%.3f,"
             "\"hdrType\":\"%s\",\"width\":%d,\"height\":%d,\"scanType\":\"VIDEO_PROGRESSIVE\",\"path\":\"network\","
             "\"pixelAspectRatio\":{\"width\":1,\"height\":1},\"data3D\":{\"originalPattern\":\"2d\",\"currentPattern\":\"2d\","
             "\"typeLR\":\"LR\"},\"hfr\":false,\"adaptive\":false,\"rotation\":\"0\",\"specificRendering\":\"none\","
             "\"%s\":{\"displayPrimariesX0\":0,\"displayPrimariesY0\":0,\"displayPrimariesX1\":0,\"displayPrimariesY1\":0,"
             "\"displayPrimariesX2\":0,\"displayPrimariesY2\":0,\"whitePointX\":0,\"whitePointY\":0,"
             "\"minDisplayMasteringLuminance\":0,\"maxDisplayMasteringLuminance\":0,\"maxContentLightLevel\":0,"
             "\"maxPicAverageLightLevel\":0},\"%s\":{\"transferCharacteristics\":2,\"colorPrimaries\":2,"
             "\"matrixCoeffs\":2,\"videoFullRangeFlag\":false}}}",
             id, picture.bit_rate, picture.fps, picture.hdr, picture.width, picture.height, sei, vui);
    pthread_mutex_unlock(&lock);
    api.set_video_data(handle, json, &task);
  }
  if (api.set_audio_data) {
    snprintf(json, sizeof json, "{\"context\":\"%s\",\"audio\":{\"immersive\":\"none\"}}", id);
    api.set_audio_data(handle, json, &task);
  }
  pause_ms(300);
  if (!still(mine)) return NULL;
  int w, h;
  plane_size(&w, &h);
  api.set_display_window(handle, 0, 0, w, h, 1, &task);
  pause_ms(200);
  if (!still(mine)) return NULL;
  api.set_state(handle, APP_FOREGROUND, want_paused ? PLAY_PAUSED : PLAY_PLAYING, &task);
  bound = 1;
  return NULL;
}

void acb_bind(const char *media_id, int paused) {
  if (!acb_available() || !media_id || !*media_id) return;
  if (!handle) {
    handle = api.create();
    /* the events go to a real function: a null one is called on the first event */
    if (!handle || !api.initialize(handle, PLAYER_URI, getenv("APPID") ? getenv("APPID") : SC_APP_ID, events)) {
      handle = 0;
      return;
    }
    /* the main plane: the other sends the picture to the picture-in-picture plane */
    if (api.set_sink_type) api.set_sink_type(handle, SINK_MAIN);
  }
  pthread_mutex_lock(&lock);
  snprintf(media, sizeof media, "%s", media_id);
  pthread_mutex_unlock(&lock);
  want_paused = paused;
  bound = 0;
  int mine = ++session;
  pthread_t t;
  if (pthread_create(&t, NULL, bind_steps, (void *)(intptr_t)mine) == 0) pthread_detach(t);
}

void acb_set_playing(int playing) {
  want_paused = !playing;
  if (!handle || !bound) return;
  long task = 0;
  api.set_state(handle, APP_FOREGROUND, playing ? PLAY_PLAYING : PLAY_PAUSED, &task);
}

void acb_release(void) {
  ++session;
  bound = 0;
  if (!handle) return;
  if (api.finalize) api.finalize(handle);
  if (api.destroy) api.destroy(handle);
  handle = 0;
}
