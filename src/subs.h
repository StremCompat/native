/* The player's subtitles: those of the subtitle addons and those inside the file, the one in
   the preferred language chosen at first, and its lines as the server reads them. */
#ifndef SC_SUBS_H
#define SC_SUBS_H

typedef struct {
  char *name, *lang, *url; /* url: an addon's file; embedded ones are read out of the video */
  int embedded, stream;    /* the subtitle stream's number in the file */
} sub_choice;

/* a new video: its title, the stream reference and address, and the language wanted ("" none) */
void subs_reset(const char *type, const char *id, const char *vid, const char *ref, const char *video_url,
                const char *want);
int subs_count(void);
const sub_choice *subs_at(int i);
/* the one shown, or -1 for none */
int subs_current(void);
void subs_choose(int i);
/* once in a while with the position: the lines of a subtitle read from the file keep coming */
void subs_tick(double position_ms);
/* the subtitles later (positive) or earlier, in seconds */
void subs_set_delay(double seconds);
double subs_delay(void);
/* the line to show at this moment, or NULL */
const char *subs_text(double position_ms);

#endif
