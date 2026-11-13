/* Video, played by the TV's own media system under the interface (the interface leaves the
   picture's place clear). One implementation per system: webos_player.c, desktop_player.c. */
#ifndef SC_PLAYER_H
#define SC_PLAYER_H

typedef enum { PLAYER_IDLE, PLAYER_LOADING, PLAYER_PLAYING, PLAYER_PAUSED, PLAYER_ENDED, PLAYER_FAILED } player_state;

typedef struct {
  char language[16], name[64];
} player_track;

#define PLAYER_MAX_TRACKS 16

/* starts the video at this address from start_ms; 0 when the system took it */
int player_open(const char *url, double start_ms);
void player_close(void);
void player_play(void);
void player_pause(void);
void player_seek(double ms);
/* once per frame: the system's news (position, end, errors) */
void player_poll(void);

player_state player_status(void);
int player_buffering(void);
double player_position(void);
double player_duration(void);
/* what went wrong, for the screen, when the status is PLAYER_FAILED */
const char *player_error(void);

int player_audio_tracks(player_track *out, int max);
int player_audio_current(void);
void player_select_audio(int index);

#endif
