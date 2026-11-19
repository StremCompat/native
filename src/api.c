#include "api.h"

#include <stdlib.h>
#include <string.h>

#include "net.h"
#include "util.h"

typedef struct {
  api_done done;
  void *user;
} call;

/* worker thread: the body into a JSON tree */
static void parse(net_reply *r, void *user) {
  (void)user;
  if (r->status == 200 && r->body) r->result = json_parse(r->body, r->len);
}

static void answered(net_reply *r, void *user) {
  call *c = user;
  jdoc *d = r->result;
  c->done(d ? json_root(d) : NULL, c->user);
  json_free(d);
  free(c);
}

static void get(const char *path, int lane, api_done done, void *user) {
  call *c = malloc(sizeof *c);
  c->done = done;
  c->user = user;
  net_request(path, NULL, lane, parse, answered, c);
}

void api_get(const char *path, api_done done, void *user) { get(path, NET_HIGH, done, user); }

void api_get_slow(const char *path, api_done done, void *user) { get(path, NET_SLOW, done, user); }

void api_post(const char *path, const char *json) { net_request(path, json, NET_HIGH, NULL, NULL, NULL); }

/* ---------- texts: two tables (the language, then English) in one lookup by hash ---------- */
#define TEXT_SLOTS 4096

typedef struct {
  const char *key, *text;
} text_slot;

static jdoc *texts;
static text_slot table[TEXT_SLOTS];

static uint32_t hash_of(const char *s) {
  uint32_t h = 2166136261u;
  while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
  return h;
}

static text_slot *slot_for(const char *key) {
  uint32_t h = hash_of(key);
  for (int i = 0; i < TEXT_SLOTS; i++) {
    text_slot *s = &table[(h + (uint32_t)i) % TEXT_SLOTS];
    if (!s->key || !strcmp(s->key, key)) return s;
  }
  return NULL;
}

/* keys already there keep their text: the language goes in first, English fills the gaps */
static void add_all(const jval *obj) {
  for (int i = 0; i < json_len(obj); i++) {
    const jval *v = json_at(obj, i);
    text_slot *s = v->type == J_STR ? slot_for(v->key) : NULL;
    if (s && !s->key) {
      s->key = v->key;
      s->text = v->u.str;
    }
  }
}

int api_load_strings(void) {
  net_reply r;
  net_fetch("/app/strings", NULL, &r);
  jdoc *d = r.status == 200 && r.body ? json_parse(r.body, r.len) : NULL;
  free(r.body);
  if (!d) return -1;
  json_free(texts);
  memset(table, 0, sizeof table);
  texts = d;
  add_all(json_get(json_root(d), "lang"));
  add_all(json_get(json_root(d), "fallback"));
  return 0;
}

const char *tx(const char *key) {
  text_slot *s = slot_for(key);
  return s && s->key ? s->text : key;
}

const char *txp(const char *key, const char *a0) {
  /* a few answers in turn, as txf: a line made of two (title and hint) read both before one is reused */
  static char ring[4][512];
  static int next;
  char *out = ring[next++ % 4];
  const jval *root = json_root(texts);
  const char *text = json_str(json_get(json_get(json_get(root, "lang"), "player"), key), NULL);
  if (!text) text = json_str(json_get(json_get(json_get(root, "fallback"), "player"), key), key);
  size_t n = 0;
  for (const char *p = text; *p && n < sizeof ring[0] - 1; p++) {
    if (p[0] == '{' && p[1] == '0' && p[2] == '}' && a0) {
      for (const char *a = a0; *a && n < sizeof ring[0] - 1; a++) out[n++] = *a;
      p += 2;
    } else {
      out[n++] = *p;
    }
  }
  out[n] = 0;
  return out;
}

const char *txf(const char *key, const char *a0, const char *a1) {
  static char ring[4][512];
  static int next;
  char *out = ring[next++ % 4];
  size_t n = 0;
  for (const char *p = tx(key); *p && n < sizeof ring[0] - 1; p++) {
    const char *arg = p[0] == '{' && (p[1] == '0' || p[1] == '1') && p[2] == '}' ? (p[1] == '0' ? a0 : a1) : NULL;
    if (!arg) {
      out[n++] = *p;
      continue;
    }
    for (; *arg && n < sizeof ring[0] - 1; arg++) out[n++] = *arg;
    p += 2;
  }
  out[n] = 0;
  return out;
}
