/* Windows: no picture, only a clock that runs like a two-hour video, so the player's controls
   can be built and tested on the computer. */
#include "player.h"

#include <stdio.h>
#include <string.h>

#include "util.h"

#define LENGTH_MS (2 * 3600 * 1000.0)

static player_state status = PLAYER_IDLE;
static double position, started_at;

/* the position runs while playing: kept as where it was when it last started */
static double now_position(void) {
  if (status != PLAYER_PLAYING) return position;
  double p = position + (now_ms() - started_at);
  return p < LENGTH_MS ? p : LENGTH_MS;
}

int player_open(const char *url, double start_ms) {
  (void)url;
  position = start_ms;
  started_at = now_ms();
  status = PLAYER_PLAYING;
  return 0;
}

void player_close(void) { status = PLAYER_IDLE; }

void player_play(void) {
  if (status != PLAYER_PAUSED) return;
  started_at = now_ms();
  status = PLAYER_PLAYING;
}

void player_pause(void) {
  if (status != PLAYER_PLAYING) return;
  position = now_position();
  status = PLAYER_PAUSED;
}

void player_seek(double ms) {
  position = ms < 0 ? 0 : ms > LENGTH_MS ? LENGTH_MS : ms;
  started_at = now_ms();
}

void player_poll(void) {
  if (status == PLAYER_PLAYING && now_position() >= LENGTH_MS) {
    position = LENGTH_MS;
    status = PLAYER_ENDED;
  }
}

player_state player_status(void) { return status; }
int player_buffering(void) { return 0; }
double player_position(void) { return now_position(); }
double player_duration(void) { return status == PLAYER_IDLE ? 0 : LENGTH_MS; }
const char *player_error(void) { return ""; }

int player_audio_tracks(player_track *out, int max) {
  static const char *langs[][2] = {{"eng", "English 5.1"}, {"por", "Português"}};
  int n = max < 2 ? max : 2;
  for (int i = 0; i < n; i++) {
    snprintf(out[i].language, sizeof out[i].language, "%s", langs[i][0]);
    snprintf(out[i].name, sizeof out[i].name, "%s", langs[i][1]);
  }
  return n;
}

static int audio;
int player_audio_current(void) { return audio; }
void player_select_audio(int index) { audio = index; }
