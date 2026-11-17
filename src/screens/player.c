/* The player: the video plays on the TV's video plane under a clear window, and this screen
   draws its controls over it as the web player does (player.css): the title, the bar with the
   position, the buttons, the seek steps that add up, short messages, and the cover while it
   loads. What is watched goes to the server every few seconds (/app/progress). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "img.h"
#include "player.h"
#include "screens.h"
#include "subs.h"
#include "text.h"
#include "ui.h"
#include "util.h"

#define BAR_SHOW_MS 5000.0
#define SEEK_COMMIT_MS 1200.0
#define REPORT_EVERY_MS 10000.0
#define BTN 86.0f
#define BTN_BIG 102.0f

/* the buttons, left to right; some are left out (episodes for films, next without one) */
enum { B_MEDIA, B_EPISODES, B_SOURCES, B_REWIND, B_PLAY, B_FORWARD, B_NEXT, B_SETTINGS, B_COUNT };
enum { MODE_VIDEO, MODE_SEEK, MODE_BUTTONS };

typedef struct {
  char *url, *title, *season_line, *logo, *background, *poster, *type, *id, *vid, *next;
} play_info;

static play_info info;
static int gen, ready, mode, button = B_PLAY, seek_step = 10, resuming;
static int live; /* a channel: no length, no seeking, nothing kept */
/* where the video last was while it played: the system can stop the player on its own (Android
   in the background), after which it knows neither */
static double seen_at, seen_len;

/* Settings > Subtitles: their size, colour, outline and background strength */
static float sub_size = 38, sub_background;
static uint32_t sub_color = 0xFFFFFFFF, sub_outline = 0x000000FF;

/* episodes started by the countdown with nobody touching the remote; at 3 it asks first */
#define AUTO_NEXT_LIMIT 3
static int auto_next_count;

/* the menu over the buttons: what it lists, and the episodes and sources it got for that */
enum { VIEW_MEDIA, VIEW_AUDIO, VIEW_SUBS, VIEW_EPISODES, VIEW_SOURCES, VIEW_SETTINGS, VIEW_SPEED, VIEW_DELAY, VIEW_CHAPTERS };

/* the settings button: speed, subtitle delay, and the chapters when the file has some */
static const double speeds[] = {0.75, 1, 1.25, 1.5};
static double speed = 1;
#define MAX_CHAPTERS 80
static double chapter_at[MAX_CHAPTERS];
static char chapter_name[MAX_CHAPTERS][64];
static int nchapters;
static menu list;
static int view, list_left;
static char *episode_ids[MENU_MAX];

/* the intro and recap to skip (seconds), the next episode by itself, and its countdown */
#define MAX_SKIPS 8
#define COUNTDOWN_MS 10000.0
static struct {
  double start, end;
  int recap;
} skips[MAX_SKIPS];
static int nskips, binge;
static double countdown_at; /* when the countdown to the next episode began, 0 when none */
static char *want_type, *want_id, *want_vid; /* what was asked, to fall back on the sources */
static double clock_now, bar_until, seek_target = -1, seek_base = -1, seek_at, last_report;

static char *copy(const jval *v) {
  const char *s = json_str(v, NULL);
  return s && *s ? str_dup(s) : NULL;
}

static void clear_info(void) {
  char **f[] = {&info.url, &info.title, &info.season_line, &info.logo, &info.background,
                &info.poster, &info.type, &info.id, &info.vid, &info.next};
  for (size_t i = 0; i < sizeof f / sizeof f[0]; i++) {
    free(*f[i]);
    *f[i] = NULL;
  }
}


static int shown(int b) {
  if (b == B_EPISODES) return info.type && !strcmp(info.type, "series");
  if (b == B_NEXT) return info.next != NULL;
  return 1;
}

static void format_time(char *out, size_t size, double ms) {
  int s = ms > 0 ? (int)(ms / 1000) : 0;
  if (s >= 3600) snprintf(out, size, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
  else snprintf(out, size, "%d:%02d", s / 60, s % 60);
}

/* ---------- what is reported to the server ---------- */
static void report(const char *event) {
  double at = player_position(), d = player_duration();
  if (d <= 0) at = seen_at, d = seen_len;
  if (!info.id || !info.type || d <= 0 || live) return;
  sbuf b = {0};
  sb_add(&b, "{\"type\":");
  sb_add_json_str(&b, info.type);
  sb_add(&b, ",\"id\":");
  sb_add_json_str(&b, info.id);
  sb_add(&b, ",\"vid\":");
  sb_add_json_str(&b, info.vid ? info.vid : info.id);
  sb_add(&b, ",\"name\":");
  sb_add_json_str(&b, info.title ? info.title : "");
  sb_add(&b, ",\"poster\":");
  sb_add_json_str(&b, info.poster ? info.poster : "");
  sb_add(&b, ",\"background\":");
  sb_add_json_str(&b, info.background ? info.background : "");
  sb_add(&b, ",\"logo\":");
  sb_add_json_str(&b, info.logo ? info.logo : "");
  if (info.next) {
    sb_add(&b, ",\"next\":");
    sb_add_json_str(&b, info.next);
  }
  sb_addf(&b, ",\"time\":%.0f,\"duration\":%.0f,\"event\":", at, d);
  sb_add_json_str(&b, event);
  sb_add(&b, "}");
  api_post("/app/progress", b.s);
  sb_free(&b);
  last_report = clock_now;
}

/* "#rrggbb" as a colour */
static uint32_t hex_color(const char *hex) {
  unsigned v = 0xFFFFFF;
  if (hex && hex[0] == '#') sscanf(hex + 1, "%6x", &v);
  return SC_RGBA((v >> 16) & 255, (v >> 8) & 255, v & 255, 255);
}

/* the file's chapters, read by the server (FFprobe) */
static void probe_came(const jval *answer, void *user) {
  if ((intptr_t)user != gen) return;
  const jval *marks = json_get(answer, "chapters");
  nchapters = 0;
  for (int i = 0; i < json_len(marks) && nchapters < MAX_CHAPTERS; i++, nchapters++) {
    chapter_at[nchapters] = json_num(json_get(json_at(marks, i), "t"), 0);
    snprintf(chapter_name[nchapters], sizeof chapter_name[0], "%s", json_str(json_get(json_at(marks, i), "name"), ""));
  }
}

/* ---------- starting ---------- */
static void skips_came(const jval *answer, void *user) {
  const jval *list = json_get(answer, "segments");
  if ((intptr_t)user != gen) return;
  nskips = 0;
  for (int i = 0; i < json_len(list) && nskips < MAX_SKIPS; i++) {
    const char *kind = json_str(json_get(json_at(list, i), "kind"), "");
    if (strcmp(kind, "intro") && strcmp(kind, "recap")) continue;
    skips[nskips].start = json_num(json_get(json_at(list, i), "start"), 0);
    skips[nskips].end = json_num(json_get(json_at(list, i), "end"), 0);
    skips[nskips].recap = !strcmp(kind, "recap");
    nskips++;
  }
}

/* the intro or recap being played now, or -1 */
static int skip_now(void) {
  double t = player_position() / 1000;
  for (int i = 0; i < nskips; i++)
    if (t >= skips[i].start && t < skips[i].end - 1) return i;
  return -1;
}

static void started(const jval *answer, void *user) {
  const jval *in = json_get(answer, "info");
  if ((intptr_t)user != gen) return;
  if (in->type != J_OBJ || !json_str(json_get(in, "URL"), NULL)) {
    if (resuming) {
      /* the last source is gone from the server: choose again */
      app_back();
      sources_open(want_type, want_id, want_vid, NULL);
    } else {
      ui_toast(txp("sourceFailed", NULL));
    }
    return;
  }
  clear_info();
  info.url = copy(json_get(in, "URL"));
  info.title = copy(json_get(in, "TITLE"));
  info.logo = copy(json_get(in, "LOGO"));
  info.background = copy(json_get(in, "BG"));
  info.poster = copy(json_get(in, "POSTER"));
  info.type = copy(json_get(in, "TYPE"));
  info.id = copy(json_get(in, "ID"));
  info.vid = copy(json_get(in, "VID"));
  info.next = copy(json_get(in, "NEXTVID"));
  const char *season = json_str(json_get(in, "SEASON"), ""), *episode = json_str(json_get(in, "EPISODE"), "");
  if (*season && *episode) {
    /* Season 1 · Episode 2  ·  Its title */
    sbuf se = {0};
    sb_addf(&se, "%s \xC2\xB7 ", txf("season", season, NULL));
    sb_add(&se, txf("episode", episode, NULL));
    const char *ep_title = json_str(json_get(in, "EPTITLE"), "");
    if (*ep_title) sb_addf(&se, "  \xC2\xB7  %s", ep_title);
    info.season_line = se.s;
  }
  seek_step = (int)json_num(json_get(answer, "seekStep"), 10);
  const jval *style = json_get(answer, "subtitleStyle");
  sub_size = (float)json_num(json_get(style, "size"), 38);
  sub_color = hex_color(json_str(json_get(style, "color"), "#ffffff"));
  sub_outline = hex_color(json_str(json_get(style, "outline"), "#000000"));
  sub_background = (float)json_num(json_get(style, "background"), 0);
  binge = json_bool(json_get(answer, "binge"), 1);
  if (info.vid) {
    sbuf p = {0};
    sb_add(&p, "/skips?vid=");
    sb_add_url(&p, info.vid);
    api_get(p.s, skips_came, (void *)(intptr_t)gen);
    sb_free(&p);
  }
  subs_reset(info.type, info.id, info.vid, json_str(json_get(answer, "ref"), ""), info.url,
             json_str(json_get(answer, "subtitleLanguage"), ""));
  double resume = json_num(json_get(answer, "resume"), 0);
  seen_at = seen_len = 0;
  player_open(info.url, resume);
  speed = 1;
  nchapters = 0;
  subs_set_delay(0);
  sbuf probe = {0};
  sb_add(&probe, "/probe?u=");
  sb_add_url(&probe, info.url);
  api_get(probe.s, probe_came, (void *)(intptr_t)gen);
  sb_free(&probe);
  if (resume > 0) {
    char at[32];
    format_time(at, sizeof at, resume);
    char text[200];
    snprintf(text, sizeof text, "%s%s", txp("resuming", NULL), at);
    ui_toast(text);
  }
  ready = 1;
}

void player_play_address(const char *url, const char *title, int is_live) {
  gen++;
  clear_info();
  info.url = str_dup(url);
  info.title = title ? str_dup(title) : NULL;
  live = is_live;
  mode = MODE_VIDEO;
  button = B_PLAY;
  seek_target = seek_base = -1;
  nskips = 0;
  countdown_at = 0;
  seen_at = seen_len = 0;
  player_open(info.url, 0);
  ready = 1;
  app_push(&player_screen);
}

/* a trailer: only the picture and the bar, nothing kept of it */
static void trailer_came(const jval *answer, void *user) {
  if ((intptr_t)user != gen) return;
  const char *url = json_str(json_get(answer, "url"), NULL);
  if (!url) ui_toast(json_str(json_get(answer, "error"), tx("trailerUnavailable")));
  else player_play_address(url, json_str(json_get(answer, "name"), NULL), 0);
}

void player_play_trailer(const char *type, const char *id) {
  gen++;
  ui_toast(tx("loadingTrailer"));
  sbuf p = {0};
  sb_add(&p, "/app/trailer?type=");
  sb_add_url(&p, type);
  sb_add(&p, "&id=");
  sb_add_url(&p, id);
  api_get(p.s, trailer_came, (void *)(intptr_t)gen);
  sb_free(&p);
}

/* opens the player on a source of a search (seq), or with seq < 0 on the last one played */
void player_start(const char *type, const char *id, const char *vid, int seq) {
  gen++;
  ready = 0;
  clear_info();
  free(want_type);
  free(want_id);
  free(want_vid);
  want_type = str_dup(type);
  want_id = str_dup(id);
  want_vid = str_dup(vid);
  resuming = seq < 0;
  /* a channel of an addon plays live: no progress and no seeking, like an M3U one */
  live = !strcmp(type, "tv") || !strcmp(type, "channel");
  mode = MODE_VIDEO;
  button = B_PLAY;
  seek_target = seek_base = -1;
  bar_until = 0;
  nskips = 0;
  countdown_at = 0;
  sbuf p = {0};
  sb_add(&p, "/app/play?type=");
  sb_add_url(&p, type);
  sb_add(&p, "&id=");
  sb_add_url(&p, id);
  sb_add(&p, "&vid=");
  sb_add_url(&p, vid);
  if (seq >= 0) sb_addf(&p, "&seq=%d", seq);
  else sb_add(&p, "&resume=1");
  api_get(p.s, started, (void *)(intptr_t)gen);
  sb_free(&p);
  app_push(&player_screen);
}

/* on to the next episode: what was watched moves on, then its sources (the matching one plays) */
static void go_next(void) {
  char *type = str_dup(info.type), *title = str_dup(info.id), *episode = str_dup(info.next);
  report("next");
  player_close();
  gen++;
  countdown_at = 0;
  app_back();
  sources_open_next(type, title, episode);
  free(type), free(title), free(episode);
}

static void leave(void) {
  list.open = 0;
  report("leave");
  player_close();
  gen++;
  app_back();
}

/* ---------- seeking: presses add up from the first one and are done when they stop ---------- */
static void commit_seek(void) {
  if (seek_target < 0) return;
  /* what played meanwhile is added back, so the jump is exactly the presses */
  double target = seek_target + (player_position() - seek_base);
  if (target < 0) target = 0;
  if (target > player_duration()) target = player_duration();
  player_seek(target);
  report("seek");
  seek_target = seek_base = -1;
}

static void seek_by(double seconds) {
  if (live || player_duration() <= 0) return;
  if (seek_target < 0) {
    seek_base = player_position();
    seek_target = seek_base;
  }
  seek_target += seconds * 1000;
  if (seek_target < 0) seek_target = 0;
  if (seek_target > player_duration()) seek_target = player_duration();
  seek_at = clock_now;
}

static void toggle(void) {
  if (player_status() == PLAYER_PLAYING) {
    player_pause();
    report("pause");
  } else {
    player_play();
  }
}

static void move_button(int dir) {
  for (int b = button + dir; b >= 0 && b < B_COUNT; b += dir)
    if (shown(b)) {
      button = b;
      return;
    }
}

/* ---------- menus ---------- */
/* rows that are not a choice: the "off" of the subtitles, and the wait while a list comes */
#define ROW_WAIT -100
#define ROW_OFF -101

/* a track's language by name, as the media service gives only its code */
static const char *language_name(const char *code) {
  static const char *names[][2] = {{"en", "English"}, {"pt", "Português"},  {"es", "Español"}, {"fr", "Français"},
                                   {"de", "Deutsch"}, {"it", "Italiano"},   {"ja", "日本語"},  {"ko", "한국어"},
                                   {"zh", "中文"},    {"ru", "Русский"},    {"hi", "हिन्दी"},   {"nl", "Nederlands"},
                                   {"pl", "Polski"},  {"tr", "Türkçe"},     {"sv", "Svenska"}, {"ar", "العربية"}};
  static const char *three[][2] = {{"eng", "en"}, {"por", "pt"}, {"spa", "es"}, {"fre", "fr"}, {"fra", "fr"},
                                   {"ger", "de"}, {"deu", "de"}, {"ita", "it"}, {"jpn", "ja"}, {"kor", "ko"},
                                   {"chi", "zh"}, {"zho", "zh"}, {"rus", "ru"}, {"hin", "hi"}, {"dut", "nl"},
                                   {"nld", "nl"}, {"pol", "pl"}, {"tur", "tr"}, {"swe", "sv"}, {"ara", "ar"}};
  char two[3] = {0};
  for (size_t i = 0; i < sizeof three / sizeof three[0]; i++)
    if (!strcmp(code, three[i][0])) code = three[i][1];
  memcpy(two, code, strlen(code) < 2 ? strlen(code) : 2);
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
    if (!strcmp(two, names[i][0]) && strlen(code) <= 3) return names[i][1];
  return code;
}

static const char *audio_name(void) {
  static player_track t[PLAYER_MAX_TRACKS];
  int n = player_audio_tracks(t, PLAYER_MAX_TRACKS), at = player_audio_current();
  return at < n && t[at].language[0] ? language_name(t[at].language) : txp("defaultAudio", NULL);
}

static void open_view(int which) {
  view = which;
  if (which == VIEW_MEDIA) {
    char row[200];
    menu_begin(&list, txp("audioSubs", NULL));
    snprintf(row, sizeof row, "%s: %s \xE2\x80\xBA", txp("audio", NULL), audio_name());
    menu_add(&list, row, 0, VIEW_AUDIO);
    const sub_choice *s = subs_at(subs_current());
    snprintf(row, sizeof row, "%s: %s \xE2\x80\xBA", txp("subtitles", NULL), s ? s->name : txp("off", NULL));
    menu_add(&list, row, 0, VIEW_SUBS);
  } else if (which == VIEW_AUDIO) {
    player_track t[PLAYER_MAX_TRACKS];
    int n = player_audio_tracks(t, PLAYER_MAX_TRACKS);
    menu_begin(&list, txp("audio", NULL));
    for (int i = 0; i < n; i++) {
      char row[640];
      snprintf(row, sizeof row, "%.200s%s%.60s", t[i].language[0] ? language_name(t[i].language) : txp("defaultAudio", NULL),
               t[i].name[0] ? "  \xC2\xB7  " : "", t[i].name);
      menu_add(&list, row, i == player_audio_current(), i);
    }
    if (!n) menu_add(&list, txp("defaultAudio", NULL), 1, 0);
  } else if (which == VIEW_SETTINGS) {
    char row[160];
    menu_begin(&list, tx("navSettings"));
    snprintf(row, sizeof row, "%s: %gx \xE2\x80\xBA", txp("speed", NULL), speed);
    menu_add(&list, row, 0, VIEW_SPEED);
    snprintf(row, sizeof row, "%s: %+.1fs \xE2\x80\xBA", txp("delay", NULL), subs_delay());
    menu_add(&list, row, 0, VIEW_DELAY);
    if (nchapters) {
      snprintf(row, sizeof row, "%s \xE2\x80\xBA", txp("chapters", NULL));
      menu_add(&list, row, 0, VIEW_CHAPTERS);
    }
  } else if (which == VIEW_SPEED) {
    menu_begin(&list, txp("speed", NULL));
    for (int i = 0; i < 4; i++) {
      char row[32];
      snprintf(row, sizeof row, "%gx", speeds[i]);
      menu_add(&list, row, speeds[i] == speed, i);
    }
  } else if (which == VIEW_DELAY) {
    menu_begin(&list, txp("delay", NULL));
    for (int i = 0; i <= 12; i++) {
      double d = (i - 6) * 0.5;
      char row[32];
      snprintf(row, sizeof row, "%+.1fs", d);
      menu_add(&list, row, d == subs_delay(), i);
    }
  } else if (which == VIEW_CHAPTERS) {
    menu_begin(&list, txp("chapters", NULL));
    for (int i = 0; i < nchapters; i++) {
      char row[160], at[32];
      format_time(at, sizeof at, chapter_at[i] * 1000);
      snprintf(row, sizeof row, "%s%s%.60s", at, chapter_name[i][0] ? "  \xC2\xB7  " : "", chapter_name[i]);
      menu_add(&list, row, 0, i);
    }
  } else if (which == VIEW_SUBS) {
    menu_begin(&list, txp("subtitles", NULL));
    menu_add(&list, txp("off", NULL), subs_current() < 0, ROW_OFF);
    for (int i = 0; i < subs_count(); i++) menu_add(&list, subs_at(i)->name, i == subs_current(), i);
  }
  list.open = 1;
}

/* the episodes of this season, from the title page */
static void episodes_came(const jval *answer, void *user) {
  if ((intptr_t)user != gen || !list.open || view != VIEW_EPISODES) return;
  const jval *seasons = json_get(json_get(json_get(answer, "title"), "series"), "seasons");
  menu_begin(&list, txp("episodes", NULL));
  for (int i = 0; i < MENU_MAX; i++) free(episode_ids[i]), episode_ids[i] = NULL;
  for (int s = 0; s < json_len(seasons); s++) {
    const jval *eps = json_get(json_at(seasons, s), "episodes");
    int here = 0;
    for (int e = 0; e < json_len(eps); e++)
      if (info.vid && !strcmp(json_str(json_get(json_at(eps, e), "id"), ""), info.vid)) here = 1;
    for (int e = 0; here && e < json_len(eps) && list.n < MENU_MAX; e++) {
      const char *eid = json_str(json_get(json_at(eps, e), "id"), "");
      episode_ids[list.n] = str_dup(eid);
      menu_add(&list, json_str(json_get(json_at(eps, e), "title"), ""), info.vid && !strcmp(eid, info.vid), list.n);
    }
  }
}

/* the sources of this video, as the sources panel lists them */
static int source_seqs[MENU_MAX];
static void sources_came(const jval *answer, void *user) {
  if ((intptr_t)user != gen || !list.open || view != VIEW_SOURCES) return;
  const jval *items = json_get(answer, "items");
  menu_begin(&list, tx("sources"));
  for (int i = 0; i < json_len(items) && i < MENU_MAX; i++) {
    const jval *it = json_at(items, i);
    char row[200];
    snprintf(row, sizeof row, "%s  \xC2\xB7  %s", json_str(json_get(it, "heading"), ""), json_str(json_get(it, "text"), ""));
    source_seqs[i] = (int)json_num(json_get(it, "seq"), 0);
    menu_add(&list, row, 0, i);
  }
}

static void ask_list(int which, const char *path, api_done done) {
  view = which;
  menu_begin(&list, which == VIEW_EPISODES ? txp("episodes", NULL) : tx("sources"));
  menu_add(&list, txp("readingTracks", NULL), 0, ROW_WAIT);
  sbuf p = {0};
  sb_add(&p, path);
  sb_add(&p, "type=");
  sb_add_url(&p, info.type);
  if (which == VIEW_EPISODES) {
    sb_add(&p, "&id=");
    sb_add_url(&p, info.id);
  } else {
    sb_add(&p, "&vid=");
    sb_add_url(&p, info.vid ? info.vid : info.id);
  }
  api_get(p.s, done, (void *)(intptr_t)gen);
  sb_free(&p);
}

static void choose(int id) {
  if (id == ROW_WAIT || id == ROW_OFF) {
    if (id == ROW_OFF) subs_choose(-1);
    list.open = 0;
    return;
  }
  if (view == VIEW_MEDIA || view == VIEW_SETTINGS) {
    open_view(id);
    return;
  }
  if (view == VIEW_SPEED) {
    if (player_set_speed(speeds[id])) speed = speeds[id];
    else ui_toast(txp("speedRefused", NULL));
  } else if (view == VIEW_DELAY) {
    subs_set_delay((id - 6) * 0.5);
  } else if (view == VIEW_CHAPTERS) {
    player_seek(chapter_at[id] * 1000);
    report("seek");
  }
  if (view == VIEW_AUDIO) player_select_audio(id);
  else if (view == VIEW_SUBS) subs_choose(id);
  else if (view == VIEW_EPISODES && episode_ids[id] && strcmp(episode_ids[id], info.vid ? info.vid : "")) {
    char *type = str_dup(info.type), *title = str_dup(info.id), *episode = str_dup(episode_ids[id]);
    leave();
    sources_open(type, title, episode, NULL);
    free(type), free(title), free(episode);
    return;
  } else if (view == VIEW_SOURCES) {
    char *type = str_dup(info.type), *title = str_dup(info.id), *video = str_dup(info.vid ? info.vid : info.id);
    report("leave");
    player_close();
    app_back();
    player_start(type, title, video, source_seqs[id]);
    free(type), free(title), free(video);
    return;
  }
  list.open = 0;
}

static void press_button(void) {
  if (button == B_PLAY) toggle();
  else if (button == B_REWIND) seek_by(-seek_step);
  else if (button == B_FORWARD) seek_by(seek_step);
  else if (button == B_MEDIA) list_left = 1, open_view(VIEW_MEDIA);
  else if (button == B_SETTINGS) list_left = 0, open_view(VIEW_SETTINGS);
  else if (button == B_EPISODES) list_left = 1, ask_list(VIEW_EPISODES, "/app/title?", episodes_came);
  else if (button == B_SOURCES) list_left = 1, ask_list(VIEW_SOURCES, "/app/streams?", sources_came);
  else if (button == B_NEXT && info.next) go_next();
}

static void key(app_key k) {
  bar_until = clock_now + BAR_SHOW_MS;
  /* any key means someone is watching */
  if (!countdown_at) auto_next_count = 0;
  if (k == K_BACK && !list.open) {
    leave();
    return;
  }
  if (countdown_at) {
    if (k == K_OK) {
      auto_next_count = 0;
      go_next();
    }
    return;
  }
  if (player_status() == PLAYER_FAILED) {
    /* OK: another source of the same video */
    if (k == K_OK) {
      char *type = str_dup(info.type ? info.type : want_type), *title = str_dup(info.id ? info.id : want_id),
           *video = str_dup(info.vid ? info.vid : want_vid);
      player_close();
      gen++;
      app_back();
      sources_open(type, title, video, NULL);
      free(type), free(title), free(video);
    }
    return;
  }
  if (list.open) {
    int id = menu_key(&list, k);
    if (id != -1 && id != -2) choose(id);
    return;
  }
  /* the colour buttons of the remote: red subtitles, green audio, yellow skips an intro */
  if (k == K_RED) list_left = 0, open_view(VIEW_SUBS);
  else if (k == K_GREEN) list_left = 0, open_view(VIEW_AUDIO);
  else if (k == K_YELLOW) seek_by(85);
  if (k == K_RED || k == K_GREEN || k == K_YELLOW) return;
  if (k == K_PLAY) {
    toggle();
    return;
  }
  if (mode == MODE_BUTTONS) {
    if (k == K_LEFT) move_button(-1);
    else if (k == K_RIGHT) move_button(1);
    else if (k == K_UP) mode = MODE_SEEK;
    else if (k == K_DOWN) mode = MODE_VIDEO;
    else if (k == K_OK) press_button();
  } else if (mode == MODE_SEEK) {
    if (k == K_LEFT) seek_by(-seek_step);
    else if (k == K_RIGHT) seek_by(seek_step);
    else if (k == K_DOWN) mode = MODE_BUTTONS;
    else if (k == K_UP) mode = MODE_VIDEO;
    else if (k == K_OK) commit_seek();
  } else {
    if (k == K_UP) mode = MODE_BUTTONS;
    else if (k == K_DOWN) mode = MODE_SEEK;
    else if (k == K_OK && skip_now() >= 0) player_seek(skips[skip_now()].end * 1000), report("seek");
    else if (k == K_OK) toggle();
    else if (k == K_LEFT) seek_by(-seek_step);
    else if (k == K_RIGHT) seek_by(seek_step);
  }
}

/* ---------- drawing ---------- */
/* the title's logo, else its name, centred: shown while the video loads and while it waits */
static void draw_cover(float alpha) {
  const sc_tex *art = img_get(info.background, 1280, NULL);
  gfx_rect(0, 0, SC_DESIGN_W, SC_DESIGN_H, 0, SC_RGBA(10, 10, 11, (int)(255 * alpha)));
  if (art) gfx_image(art, 0, 0, SC_DESIGN_W, SC_DESIGN_H, 0, alpha * 0.5f, COVER);
  const sc_tex *logo = img_get(info.logo, 640, NULL);
  /* breathing, as the web cover does */
  float breathe = 0.8f + 0.2f * (0.5f + 0.5f * sinf((float)(clock_now / 2800.0 * 6.2832)));
  gfx_push_opacity(alpha * breathe);
  if (logo) {
    float w = 640, h = logo->h * (640.0f / logo->w);
    if (h > 400) w *= 400 / h, h = 400;
    gfx_image(logo, (SC_DESIGN_W - w) / 2, (SC_DESIGN_H - h) / 2, w, h, 0, 1, STRETCH);
  } else if (info.title && (!info.logo || !info.logo[0] || img_failed(info.logo, 640))) {
    /* the name only when there is no logo: while it comes its place stays empty */
    float tw = text_width(info.title, 58, W_BOLD);
    text_draw_fit(info.title, tw < 1600 ? (SC_DESIGN_W - tw) / 2 : 160, SC_DESIGN_H / 2 - 35, 1600, 58, W_BOLD, C_WHITE);
  }
  gfx_pop_opacity();
}

static void draw_button(int b, float x, float y, float size) {
  int focused = mode == MODE_BUTTONS && button == b;
  gfx_rect(x, y, size, size, size / 2, focused ? C_WHITE : C_TEXT(0.13f));
  uint32_t c = focused ? SC_RGBA(0, 0, 0, 255) : C_WHITE;
  static const icon_id icons[B_COUNT] = {ICON_SUBTITLES, ICON_EPISODES, ICON_SOURCES, ICON_REWIND,
                                         ICON_PLAY,      ICON_FORWARD,  ICON_PLAY,    ICON_GEAR};
  if (b == B_PLAY && player_status() == PLAYER_PLAYING) {
    gfx_rect(x + size / 2 - 14, y + size / 2 - 19, 10, 38, 2, c);
    gfx_rect(x + size / 2 + 4, y + size / 2 - 19, 10, 38, 2, c);
  } else {
    float s = b == B_PLAY ? 44 : 40;
    ui_icon(icons[b], x + (size - s) / 2 + (b == B_PLAY ? 3 : 0), y + (size - s) / 2, s, c);
  }
  static const char *labels[B_COUNT] = {"audioSubs", "episodes", "sources", NULL, NULL, NULL, NULL, "settings"};
  if (focused && labels[b]) {
    const char *label = b == B_SOURCES ? tx("sources") : b == B_SETTINGS ? tx("navSettings") : txp(labels[b], NULL);
    float lw = text_width(label, 22, W_BOLD) + 28, lx = x + size / 2 - lw / 2;
    if (lx < 16) lx = 16;
    gfx_rect(lx, y - 14 - 40, lw, 40, 8, SC_RGBA(0, 0, 0, 191));
    ui_line(label, lx + 14, y - 14 - 40, 40, 22, W_BOLD, C_WHITE);
  }
}

static void draw_bar(void) {
  /* #bar: a shade from the bottom, the title, the season line, the position and the buttons */
  gfx_gradient(0, SC_DESIGN_H - 420, SC_DESIGN_W, 420, 0, SC_RGBA(0, 0, 0, 0), SC_RGBA(0, 0, 0, 235), 1);
  float x = 52, y = SC_DESIGN_H - 40 - 104 - 20 - 26;
  double d = player_duration(), at = seek_target >= 0 ? seek_target : player_position();
  /* position and time on one line */
  char now[32], total[32], left[40], times[128];
  format_time(now, sizeof now, at);
  format_time(total, sizeof total, d);
  format_time(left, sizeof left, d - at);
  if (live) snprintf(times, sizeof times, "%s", tx("liveNow"));
  else snprintf(times, sizeof times, "%s / %s   -%s", now, total, left);
  float tw = text_width(times, 26, W_REGULAR);
  float bar_w = SC_DESIGN_W - 104 - tw - 48, frac = live ? 1 : d > 0 ? (float)clampf((float)(at / d), 0, 1) : 0;
  int seek_focus = mode == MODE_SEEK, seeking = seek_target >= 0;
  float bh = seek_focus || seeking ? 12 : 8;
  ui_line(times, x + bar_w + 48, y + 4 - 18, 36, 26, W_REGULAR, C_TEXT(0.92f));
  if (seek_focus) gfx_outline(x - 3, y + 4 - bh / 2 - 3, bar_w + 6, bh + 6, 6, 3, C_TEXT(0.34f));
  gfx_rect(x, y + 4 - bh / 2, bar_w, bh, 3, C_TEXT(0.22f));
  gfx_rect(x, y + 4 - bh / 2, bar_w * frac, bh, 3, C_WHITE);
  float knob = seek_focus ? 28 : seeking ? 26 : 18;
  gfx_rect(x + bar_w * frac - knob / 2 - 4, y + 4 - knob / 2 - 4, knob + 8, knob + 8, knob / 2 + 4, C_TEXT(0.22f));
  gfx_rect(x + bar_w * frac - knob / 2, y + 4 - knob / 2, knob, knob, knob / 2, C_WHITE);
  /* the title and the season line above the bar */
  float ty = y - 16 - 34;
  if (info.season_line) {
    text_draw_fit(info.season_line, x, ty - 4, 1400, 28, W_SEMIBOLD, C_TEXT(0.78f));
    ty -= 6 + 56;
  } else {
    ty -= 22;
  }
  if (info.title) text_draw_fit(info.title, x, ty, 1400, 46, W_EXTRABOLD, C_WHITE);
  /* the buttons: left, centre (bigger play), right */
  float row = y + 4 + 26 + 20;
  float bx = x;
  for (int b = B_MEDIA; b <= B_SOURCES; b++)
    if (shown(b)) draw_button(b, bx, row + 8, BTN), bx += BTN + 24;
  float cx = SC_DESIGN_W / 2 - (BTN + 40 + BTN_BIG / 2);
  draw_button(B_REWIND, cx, row + 8, BTN);
  draw_button(B_PLAY, cx + BTN + 40, row, BTN_BIG);
  draw_button(B_FORWARD, cx + BTN + 40 + BTN_BIG + 40, row + 8, BTN);
  float rx = SC_DESIGN_W - 52 - BTN;
  draw_button(B_SETTINGS, rx, row + 8, BTN);
  if (shown(B_NEXT)) {
    const char *label = txp("nextEpisode", NULL);
    float w = text_width(label, 27, W_BOLD) + 80, nx = rx - 24 - w;
    int f = mode == MODE_BUTTONS && button == B_NEXT;
    gfx_rect(nx, row + 8, w, BTN, BTN / 2, f ? C_WHITE : C_TEXT(0.13f));
    ui_line(label, nx + 40, row + 8, BTN, 27, W_BOLD, f ? SC_RGBA(0, 0, 0, 255) : C_WHITE);
  }
}

static void draw_seek_amount(void) {
  if (seek_target < 0) return;
  int d = (int)((seek_target - seek_base) / 1000), a = d < 0 ? -d : d;
  /* +30s, -2m, +1m 30s */
  char text[32];
  char sign = d < 0 ? '-' : '+';
  if (a < 60) snprintf(text, sizeof text, "%c%ds", sign, a);
  else if (a % 60) snprintf(text, sizeof text, "%c%dm %ds", sign, a / 60, a % 60);
  else snprintf(text, sizeof text, "%c%dm", sign, a / 60);
  float w = text_width(text, 60, W_EXTRABOLD);
  text_draw(text, (SC_DESIGN_W - w) / 2 + 2, SC_DESIGN_H * 0.32f - 35 + 3, 60, W_EXTRABOLD, SC_RGBA(0, 0, 0, 160));
  text_draw(text, (SC_DESIGN_W - w) / 2, SC_DESIGN_H * 0.32f - 35, 60, W_EXTRABOLD, C_WHITE);
}

/* the line of the subtitle (#sub): centred, near the bottom, white with a shadow */
static void draw_subtitle(void) {
  const char *text = subs_text(player_position());
  if (!text) return;
  char lines[8][512];
  int n = 0;
  float size = sub_size, max_w = SC_DESIGN_W * 0.88f;
  /* each line of the subtitle, wrapped at word breaks when it is wider than the screen (a long
     line used to end in "..." and lose its end) */
  for (const char *p = text; *p && n < 8;) {
    const char *end = strchr(p, '\n');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    char cur[512] = "";
    size_t at = 0;
    while (at < len && n < 8) {
      while (at < len && p[at] == ' ') at++;
      size_t word = at;
      while (word < len && p[word] != ' ') word++;
      if (word == at) break;
      char next[512];
      snprintf(next, sizeof next, "%s%s%.*s", cur, cur[0] ? " " : "", (int)(word - at), p + at);
      if (cur[0] && text_width(next, size, W_REGULAR) > max_w) {
        snprintf(lines[n++], sizeof lines[0], "%s", cur);
        snprintf(cur, sizeof cur, "%.*s", (int)(word - at), p + at);
      } else
        snprintf(cur, sizeof cur, "%s", next);
      at = word;
    }
    if (cur[0] && n < 8) snprintf(lines[n++], sizeof lines[0], "%s", cur);
    p += len + (end ? 1 : 0);
  }
  float lh = size * 1.35f, y = SC_DESIGN_H * 0.92f - n * lh;
  for (int i = 0; i < n; i++, y += lh) {
    float w = text_width(lines[i], size, W_REGULAR), x = w < max_w ? (SC_DESIGN_W - w) / 2 : SC_DESIGN_W * 0.06f;
    if (sub_background > 0)
      gfx_rect(x - 14, y, (w < max_w ? w : max_w) + 28, lh, 6, SC_RGBA(0, 0, 0, (int)(255 * sub_background)));
    /* the outline: the line drawn around itself in the outline colour */
    static const float around[8][2] = {{-2, 0}, {2, 0}, {0, -2}, {0, 2}, {-2, -2}, {2, 2}, {-2, 2}, {2, -2}};
    for (int o = 0; o < 8; o++) text_draw_fit(lines[i], x + around[o][0], y + around[o][1], max_w, size, W_REGULAR, sub_outline);
    text_draw_fit(lines[i], x, y, max_w, size, W_REGULAR, sub_color);
  }
}

/* paused, once the bar has gone: the title over a shade, as the web pause screen (#pv) */
static void draw_paused(void) {
  gfx_gradient(0, 0, SC_DESIGN_W * 0.58f, SC_DESIGN_H, 0, SC_RGBA(0, 0, 0, 209), SC_RGBA(0, 0, 0, 0), 0);
  gfx_gradient(0, SC_DESIGN_H * 0.65f, SC_DESIGN_W, SC_DESIGN_H * 0.35f, 0, SC_RGBA(0, 0, 0, 0), SC_RGBA(0, 0, 0, 166), 1);
  char label[64];
  utf8_upper(label, sizeof label, txp("paused", NULL));
  float x = 56, y = 76;
  text_draw(label, x, y, 23, W_REGULAR, C_TEXT(0.72f));
  y += 40;
  const sc_tex *logo = img_get(info.logo, 640, NULL);
  if (logo) {
    float w = 640, h = logo->h * (640.0f / logo->w);
    if (h > 210) w *= 210 / h, h = 210;
    gfx_image(logo, x, y, w, h, 0, 1, STRETCH);
    y += h + 12;
  } else if (info.title && (!info.logo || !info.logo[0] || img_failed(info.logo, 640))) {
    y += text_draw_wrap(info.title, x, y, SC_DESIGN_W * 0.58f, 62, W_EXTRABOLD, C_WHITE, 2, 67) + 12;
  }
  if (info.season_line) text_draw_fit(info.season_line, x, y, SC_DESIGN_W * 0.58f, 28, W_SEMIBOLD, C_TEXT(0.78f));
}

/* Skip intro / Skip recap, at the right over the bar (#skip): OK while it shows */
static void draw_skip(void) {
  int at = skip_now();
  if (at < 0) return;
  const char *label = txp(skips[at].recap ? "skipRecap" : "skipIntro", NULL);
  float w = text_width(label, 26, W_BOLD) + 60, x = SC_DESIGN_W - 52 - w, y = SC_DESIGN_H - 230 - 64;
  gfx_rect(x, y, w, 64, 12, SC_RGBA(20, 20, 22, 224));
  gfx_outline(x, y, w, 64, 12, 2, C_TEXT(0.85f));
  ui_line(label, x + 30, y, 64, 26, W_BOLD, C_WHITE);
}

/* the end of an episode: the next one in a few seconds, OK at once, Back leaves */
static void draw_countdown(double t) {
  gfx_rect(0, 0, SC_DESIGN_W, SC_DESIGN_H, 0, SC_RGBA(10, 10, 11, 235));
  char secs[16];
  snprintf(secs, sizeof secs, "%d", (int)((COUNTDOWN_MS - (t - countdown_at)) / 1000) + 1);
  int asking = auto_next_count >= AUTO_NEXT_LIMIT;
  const char *title = asking ? txp("stillWatching", NULL) : txp("playNextIn", secs);
  const char *hint = asking ? txp("keepWatching", NULL) : txp("countdownHint", NULL);
  float y = SC_DESIGN_H / 2 - 60;
  text_draw(title, (SC_DESIGN_W - text_width(title, 52, W_EXTRABOLD)) / 2, y, 52, W_EXTRABOLD, C_WHITE);
  text_draw(hint, (SC_DESIGN_W - text_width(hint, 26, W_SEMIBOLD)) / 2, y + 90, 26, W_SEMIBOLD, C_TEXT(0.78f));
}


static void draw_failure(void) {
  gfx_rect(0, 0, SC_DESIGN_W, SC_DESIGN_H, 0, SC_RGBA(10, 10, 11, 235));
  const char *title = txp("sourceFailed", NULL), *why = player_error(), *hint = txp("failHint", NULL);
  float y = SC_DESIGN_H / 2 - 90;
  text_draw(title, (SC_DESIGN_W - text_width(title, 44, W_EXTRABOLD)) / 2, y, 44, W_EXTRABOLD, C_WHITE);
  if (why && *why) text_draw(why, (SC_DESIGN_W - text_width(why, 24, W_REGULAR)) / 2, y + 74, 24, W_REGULAR, C_TEXT(0.6f));
  text_draw(hint, (SC_DESIGN_W - text_width(hint, 26, W_SEMIBOLD)) / 2, y + 130, 26, W_SEMIBOLD, C_TEXT(0.78f));
}

static void frame(double t, float dt) {
  (void)dt;
  clock_now = t;
  player_poll();
  /* the video shows through: nothing is drawn where the picture is */
  gfx_clear(SC_RGBA(0, 0, 0, 0));
  player_state st = player_status();
  if (player_audio_failed()) {
    api_post("/app/learn", "{\"what\":\"audio\"}");
    ui_toast(tx("audioNotPlayable"));
  }
  if ((st == PLAYER_PLAYING || st == PLAYER_PAUSED) && player_duration() > 0) seen_at = player_position(), seen_len = player_duration();
  /* stopped by the system while it was open (Android stops it when the app goes to the background):
     what was watched is kept, and the app goes back */
  if (ready && st == PLAYER_IDLE) {
    leave();
    return;
  }
  if (seek_target >= 0 && t - seek_at > SEEK_COMMIT_MS) commit_seek();
  if (st == PLAYER_PLAYING && t - last_report > REPORT_EVERY_MS) report("tick");
  if (st == PLAYER_ENDED && binge && info.next) {
    if (!countdown_at) countdown_at = t;
    if (t - countdown_at >= COUNTDOWN_MS && auto_next_count < AUTO_NEXT_LIMIT) {
      auto_next_count++;
      go_next();
      return;
    }
    draw_countdown(t);
    return;
  }
  if (st == PLAYER_ENDED) {
    leave();
    return;
  }
  if (st == PLAYER_FAILED) {
    draw_failure();
    return;
  }
  /* a video whose length the system has not said yet shows anyway once it plays (it used to stay
     covered for good when the length never came) */
  if (!ready || st == PLAYER_LOADING || (!live && player_duration() <= 0 && st != PLAYER_PLAYING && st != PLAYER_PAUSED)) {
    draw_cover(1);
  } else {
    if (player_buffering()) gfx_rect(0, 0, SC_DESIGN_W, SC_DESIGN_H, 0, SC_RGBA(10, 10, 11, 128));
    subs_tick(player_position());
    draw_subtitle();
    if (st == PLAYER_PAUSED && t >= bar_until && !list.open) draw_paused();
    else if (t < bar_until || st == PLAYER_PAUSED || seek_target >= 0 || list.open) draw_bar();
    draw_seek_amount();
    draw_skip();
    menu_draw(&list, list_left ? 52 : SC_DESIGN_W - 52 - 384, SC_DESIGN_H - 190);
  }
}

static void enter(void) {}

const screen player_screen = {enter, key, frame, NULL, 0};
