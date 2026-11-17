/* Samsung Tizen: the TV's AVPlay plays the address on the video plane under the page, which is
   see-through where the app leaves the picture's place clear. AVPlay's news (position, end,
   errors) is kept in a small JavaScript object that these calls read once per frame. */
#include "player.h"

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the states as player_state: 0 idle, 1 loading, 2 playing, 3 paused, 4 ended, 5 failed */
EM_JS(void, js_open, (const char *url, double start), {
  var p = (Module.scPlayer = {state: 1, buffering: 0, position: start, error: ""});
  var av = webapis.avplay;
  try {
    av.stop();
    av.close();
  } catch (e) {}
  function fail(e) {
    p.state = 5;
    p.error = String((e && (e.name || e.message)) || e || "error");
  }
  try {
    av.open(UTF8ToString(url));
    av.setDisplayRect(0, 0, 1920, 1080);
    av.setListener({
      onbufferingstart: function() { p.buffering = 1; },
      onbufferingcomplete: function() { p.buffering = 0; },
      oncurrentplaytime: function(ms) { p.position = ms; },
      onstreamcompleted: function() { p.state = 4; },
      onerror: fail
    });
    av.prepareAsync(function() {
      if (start > 0) av.seekTo(start);
      av.play();
      p.state = 2;
    }, fail);
  } catch (e) {
    fail(e);
  }
});

EM_JS(int, js_order, (int what, double value), {
  var av = webapis.avplay, p = Module.scPlayer;
  if (!p) return 1;
  try {
    if (what == 0) {
      av.stop();
      av.close();
      Module.scPlayer = null;
    } else if (what == 1) {
      av.play();
      p.state = 2;
    } else if (what == 2) {
      av.pause();
      p.state = 3;
    } else if (what == 3) {
      p.position = value;
      av.seekTo(value);
    } else if (what == 4) {
      av.setSpeed(value);
    } else if (what == 5) {
      av.setSelectTrack("AUDIO", value);
      p.audio = value;
    }
  } catch (e) {
    return 1;
  }
  return 0;
});

/* 0 state, 1 buffering, 2 position, 3 duration, 4 the chosen audio track */
EM_JS(double, js_read, (int what), {
  var p = Module.scPlayer;
  if (!p) return 0;
  if (what == 0) return p.state;
  if (what == 1) return p.buffering;
  if (what == 2) return p.position;
  if (what == 3) {
    try {
      return webapis.avplay.getDuration() || 0;
    } catch (e) {
      return 0;
    }
  }
  return p.audio || 0;
});

/* the error, or the audio tracks as lines of "index<TAB>language<TAB>name" */
EM_JS(void, js_text, (int what, char *out, int size), {
  var p = Module.scPlayer, text = "";
  if (p && what == 0) text = p.error;
  if (p && what == 1) {
    try {
      webapis.avplay.getTotalTrackInfo().forEach(function(t) {
        if (t.type != "AUDIO") return;
        var info = {};
        try {
          info = JSON.parse(t.extra_info);
        } catch (e) {}
        text += t.index + "\t" + (info.language || "") + "\t" + (info.fourCC || "") + (info.channels > 2 ? " " + info.channels + "ch" : "") + "\n";
      });
    } catch (e) {}
  }
  stringToUTF8(text, out, size);
});

static int track_index[PLAYER_MAX_TRACKS];

int player_open(const char *url, double start_ms) {
  js_open(url, start_ms);
  return 0;
}

void player_close(void) { js_order(0, 0); }
void player_play(void) { js_order(1, 0); }
void player_pause(void) { js_order(2, 0); }
void player_seek(double ms) { js_order(3, ms); }
void player_poll(void) {}

player_state player_status(void) { return (player_state)js_read(0); }
int player_buffering(void) { return (int)js_read(1); }
double player_position(void) { return js_read(2); }
double player_duration(void) { return js_read(3); }

const char *player_error(void) {
  static char error[128];
  js_text(0, error, sizeof error);
  return error;
}

int player_audio_tracks(player_track *out, int max) {
  char list[2048];
  js_text(1, list, sizeof list);
  int n = 0;
  for (char *line = list; *line && n < max && n < PLAYER_MAX_TRACKS; n++) {
    char *end = strchr(line, '\n');
    if (end) *end = 0;
    char *language = strchr(line, '\t'), *name = language ? strchr(language + 1, '\t') : NULL;
    if (language) *language++ = 0;
    if (name) *name++ = 0;
    track_index[n] = atoi(line);
    snprintf(out[n].language, sizeof out[n].language, "%s", language ? language : "");
    snprintf(out[n].name, sizeof out[n].name, "%s", name ? name : "");
    if (!end) {
      n++;
      break;
    }
    line = end + 1;
  }
  return n;
}

/* AVPlay numbers the tracks among all the file's; the menu by their place among the audio ones */
int player_audio_current(void) {
  int index = (int)js_read(4);
  for (int i = 0; i < PLAYER_MAX_TRACKS; i++)
    if (track_index[i] == index) return i;
  return 0;
}

void player_select_audio(int index) {
  if (index >= 0 && index < PLAYER_MAX_TRACKS) js_order(5, track_index[index]);
}

/* AVPlay takes whole speeds only on many models; a refusal leaves it at normal */
/* the system does not say it */
int player_audio_failed(void) { return 0; }
int player_hdr(void) { return 0; }

int player_set_speed(double rate) { return js_order(4, rate) == 0; }
