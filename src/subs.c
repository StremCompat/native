#include "subs.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "util.h"

#define MAX_CHOICES 64
#define POLL_MS 2000.0

typedef struct {
  double start, end; /* seconds */
  char *text;
} cue;

static char *type, *id, *vid, *ref, *video, *want;
static sub_choice choices[MAX_CHOICES];
static int nchoices, current = -1, gen, decided;
static cue *cues;
static int ncues, cap_cues, reading_done, polling;
static double last_poll, delay;

static char *copy(const char *s) { return s ? str_dup(s) : NULL; }

static void free_cues(void) {
  for (int i = 0; i < ncues; i++) free(cues[i].text);
  free(cues);
  cues = NULL;
  ncues = cap_cues = 0;
}

static void add_cues(const jval *list) {
  for (int i = 0; i < json_len(list); i++) {
    const jval *c = json_at(list, i);
    if (ncues == cap_cues) {
      cap_cues = cap_cues ? cap_cues * 2 : 256;
      cues = realloc(cues, (size_t)cap_cues * sizeof *cues);
    }
    cues[ncues].start = json_num(json_get(c, "s"), 0);
    cues[ncues].end = json_num(json_get(c, "e"), 0);
    cues[ncues].text = str_dup(json_str(json_get(c, "t"), ""));
    ncues++;
  }
}

/* two languages are the same when their first two letters are (por / pt, eng / en) */
static int same_language(const char *a, const char *b) {
  return a && b && *a && *b && !strncmp(a, b, 2);
}

/* the first subtitle in the wanted language, addons before the file's, once both lists came */
static void decide(void) {
  if (decided || !want || !*want) return;
  for (int i = 0; i < nchoices; i++)
    if (same_language(choices[i].lang, want)) {
      decided = 1;
      subs_choose(i);
      return;
    }
}

static void add_choice(const char *name, const char *lang, const char *url, int embedded, int stream) {
  if (nchoices >= MAX_CHOICES) return;
  sub_choice *c = &choices[nchoices++];
  c->name = copy(name);
  c->lang = copy(lang);
  c->url = copy(url);
  c->embedded = embedded;
  c->stream = stream;
}

static void addon_list(const jval *answer, void *user) {
  const jval *list = json_get(answer, "subtitles");
  if ((intptr_t)user != gen) return;
  for (int i = 0; i < json_len(list); i++) {
    const jval *s = json_at(list, i);
    char name[160];
    snprintf(name, sizeof name, "%s  \xC2\xB7  %s", json_str(json_get(s, "name"), ""), json_str(json_get(s, "src"), ""));
    add_choice(name, json_str(json_get(s, "lang"), ""), json_str(json_get(s, "u"), ""), 0, 0);
  }
  decide();
}

static void file_list(const jval *answer, void *user) {
  const jval *list = json_get(answer, "tracks");
  if ((intptr_t)user != gen) return;
  for (int i = 0; i < json_len(list); i++) {
    const jval *t = json_at(list, i);
    add_choice(json_str(json_get(t, "name"), ""), json_str(json_get(t, "lang"), ""), NULL, 1,
               (int)json_num(json_get(t, "n"), 0));
  }
  decide();
}

void subs_reset(const char *t, const char *title_id, const char *video_id, const char *stream_ref,
                const char *video_url, const char *wanted) {
  for (int i = 0; i < nchoices; i++) {
    free(choices[i].name);
    free(choices[i].lang);
    free(choices[i].url);
  }
  nchoices = decided = 0;
  current = -1;
  free_cues();
  char **keep[] = {&type, &id, &vid, &ref, &video, &want};
  const char *values[] = {t, title_id, video_id, stream_ref, video_url, wanted};
  for (int i = 0; i < 6; i++) {
    free(*keep[i]);
    *keep[i] = copy(values[i] ? values[i] : "");
  }
  gen++;
  sbuf p = {0};
  sb_add(&p, "/app/subtitles?type=");
  sb_add_url(&p, type);
  sb_add(&p, "&vid=");
  sb_add_url(&p, vid);
  sb_add(&p, "&ref=");
  sb_add_url(&p, ref);
  api_get(p.s, addon_list, (void *)(intptr_t)gen);
  sb_free(&p);
  sb_add(&p, "/subtitle-tracks?u=");
  sb_add_url(&p, video);
  api_get(p.s, file_list, (void *)(intptr_t)gen);
  sb_free(&p);
}

int subs_count(void) { return nchoices; }
const sub_choice *subs_at(int i) { return i >= 0 && i < nchoices ? &choices[i] : NULL; }
int subs_current(void) { return current; }

/* the server's lines for the chosen subtitle; a file's keep coming as it reads on */
static void cues_came(const jval *answer, void *user) {
  if ((intptr_t)user != gen) return;
  polling = 0;
  add_cues(json_get(answer, "cues"));
  reading_done = json_bool(json_get(answer, "done"), 1) || json_bool(json_get(answer, "gone"), 0);
}

/* the address that names a subtitle stream of the file to the server */
static void file_query(sbuf *p, const char *path, double seconds) {
  const sub_choice *c = &choices[current];
  sb_add(p, path);
  sb_add(p, "?u=");
  sb_add_url(p, video);
  sb_addf(p, "&n=%d&t=%.0f&since=%d&id=", c->stream, seconds, ncues);
  sb_add_url(p, id);
  sb_add(p, "&vid=");
  sb_add_url(p, vid);
}

void subs_choose(int i) {
  gen++;
  free_cues();
  current = i >= 0 && i < nchoices ? i : -1;
  reading_done = 1;
  polling = 0;
  if (current < 0) return;
  decided = 1;
  sbuf p = {0};
  if (choices[current].embedded) {
    file_query(&p, "/subtitle-start", 0);
    reading_done = 0;
  } else {
    sb_add(&p, "/sub?u=");
    sb_add_url(&p, choices[current].url);
  }
  polling = 1;
  api_get(p.s, cues_came, (void *)(intptr_t)gen);
  sb_free(&p);
}

void subs_tick(double position_ms) {
  double now = now_ms();
  if (current < 0 || reading_done || polling || now - last_poll < POLL_MS) return;
  last_poll = now;
  polling = 1;
  sbuf p = {0};
  file_query(&p, "/subtitle-cues", position_ms / 1000);
  api_get(p.s, cues_came, (void *)(intptr_t)gen);
  sb_free(&p);
}

void subs_set_delay(double seconds) { delay = seconds; }
double subs_delay(void) { return delay; }

const char *subs_text(double position_ms) {
  double t = position_ms / 1000 - delay;
  for (int i = 0; i < ncues; i++)
    if (t >= cues[i].start && t < cues[i].end) return cues[i].text;
  return NULL;
}
