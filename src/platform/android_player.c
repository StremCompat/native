/* Android TV: ExoPlayer plays the address on a surface under the app's window (NativePlayer.java
   in the Android project). The app gives its orders and reads what it reports through JNI; the
   Java side keeps the news (state, position, tracks) where these calls read them. */
#include "player.h"

#include <SDL.h>
#include <jni.h>
#include <stdio.h>
#include <string.h>

#define CLASS "io/github/alexesmm/stremcompat/NativePlayer"

static player_state status = PLAYER_IDLE;
static int buffering;
static double position, duration;
static char error[128];

static JNIEnv *env(jclass *cls) {
  JNIEnv *e = (JNIEnv *)SDL_AndroidGetJNIEnv();
  *cls = (*e)->FindClass(e, CLASS);
  return e;
}

/* a static method of NativePlayer by name and signature; NULL when the build lacks it */
static jmethodID method(JNIEnv *e, jclass cls, const char *name, const char *sig) {
  jmethodID m = (*e)->GetStaticMethodID(e, cls, name, sig);
  if (!m) (*e)->ExceptionClear(e);
  return m;
}

static void call_void(const char *name) {
  jclass cls;
  JNIEnv *e = env(&cls);
  jmethodID m = method(e, cls, name, "()V");
  if (m) (*e)->CallStaticVoidMethod(e, cls, m);
  (*e)->DeleteLocalRef(e, cls);
}

static long long call_long(JNIEnv *e, jclass cls, const char *name) {
  jmethodID m = method(e, cls, name, "()J");
  return m ? (*e)->CallStaticLongMethod(e, cls, m) : 0;
}

/* a string from Java into out; "" when there is none */
static void call_string(const char *name, char *out, size_t size) {
  jclass cls;
  JNIEnv *e = env(&cls);
  jmethodID m = method(e, cls, name, "()Ljava/lang/String;");
  jstring s = m ? (jstring)(*e)->CallStaticObjectMethod(e, cls, m) : NULL;
  const char *text = s ? (*e)->GetStringUTFChars(e, s, NULL) : NULL;
  snprintf(out, size, "%s", text ? text : "");
  if (text) (*e)->ReleaseStringUTFChars(e, s, text);
  if (s) (*e)->DeleteLocalRef(e, s);
  (*e)->DeleteLocalRef(e, cls);
}

int player_open(const char *url, double start_ms) {
  jclass cls;
  JNIEnv *e = env(&cls);
  jmethodID m = method(e, cls, "open", "(Ljava/lang/String;J)V");
  jstring u = (*e)->NewStringUTF(e, url);
  if (m) (*e)->CallStaticVoidMethod(e, cls, m, u, (jlong)start_ms);
  (*e)->DeleteLocalRef(e, u);
  (*e)->DeleteLocalRef(e, cls);
  status = PLAYER_LOADING;
  position = start_ms;
  duration = 0;
  error[0] = 0;
  return m ? 0 : -1;
}

void player_close(void) {
  call_void("stop");
  status = PLAYER_IDLE;
}

void player_play(void) { call_void("play"); }
void player_pause(void) { call_void("pause"); }

void player_seek(double ms) {
  jclass cls;
  JNIEnv *e = env(&cls);
  jmethodID m = method(e, cls, "seek", "(J)V");
  if (m) (*e)->CallStaticVoidMethod(e, cls, m, (jlong)ms);
  (*e)->DeleteLocalRef(e, cls);
  position = ms;
}

/* the Java side's state: 0 idle, 1 loading, 2 playing, 3 paused, 4 ended, 5 failed, as player_state */
void player_poll(void) {
  if (status == PLAYER_IDLE) return;
  jclass cls;
  JNIEnv *e = env(&cls);
  status = (player_state)call_long(e, cls, "state");
  buffering = (int)call_long(e, cls, "buffering");
  position = (double)call_long(e, cls, "position");
  duration = (double)call_long(e, cls, "duration");
  (*e)->DeleteLocalRef(e, cls);
  if (status == PLAYER_FAILED && !error[0]) call_string("error", error, sizeof error);
}

player_state player_status(void) { return status; }
int player_buffering(void) { return buffering; }
double player_position(void) { return position; }
double player_duration(void) { return duration; }
const char *player_error(void) { return error; }

/* the audio tracks as lines of "language<TAB>name" */
int player_audio_tracks(player_track *out, int max) {
  char list[2048];
  call_string("audioTracks", list, sizeof list);
  int n = 0;
  for (char *line = list; *line && n < max; n++) {
    char *end = strchr(line, '\n'), *tab = strchr(line, '\t');
    if (end) *end = 0;
    if (tab) *tab = 0;
    snprintf(out[n].language, sizeof out[n].language, "%s", line);
    snprintf(out[n].name, sizeof out[n].name, "%s", tab ? tab + 1 : line);
    if (!end) {
      n++;
      break;
    }
    line = end + 1;
  }
  return n;
}

int player_audio_current(void) {
  jclass cls;
  JNIEnv *e = env(&cls);
  int at = (int)call_long(e, cls, "audioCurrent");
  (*e)->DeleteLocalRef(e, cls);
  return at;
}

void player_select_audio(int index) {
  jclass cls;
  JNIEnv *e = env(&cls);
  jmethodID m = method(e, cls, "selectAudio", "(I)V");
  if (m) (*e)->CallStaticVoidMethod(e, cls, m, (jint)index);
  (*e)->DeleteLocalRef(e, cls);
}

/* the system does not say it */
int player_audio_failed(void) { return 0; }
int player_hdr(void) { return 0; }

int player_set_speed(double rate) {
  jclass cls;
  JNIEnv *e = env(&cls);
  jmethodID m = method(e, cls, "speed", "(F)V");
  if (m) (*e)->CallStaticVoidMethod(e, cls, m, (jfloat)rate);
  (*e)->DeleteLocalRef(e, cls);
  return m != NULL;
}
