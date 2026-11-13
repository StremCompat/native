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

void api_get(const char *path, api_done done, void *user) {
  call *c = malloc(sizeof *c);
  c->done = done;
  c->user = user;
  net_request(path, NULL, NET_HIGH, parse, answered, c);
}

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
