#include "json.h"

#include <stdlib.h>
#include <string.h>

/* Two passes over the text: the first counts values and string bytes, so the second can lay
   the whole tree and every decoded string into a single allocation. While filling, the members
   of an array or object are skipped over once first, to know how many places to set aside. */

struct jdoc {
  jval *root;
};

const jval json_null = {J_NULL, 0, NULL, {0}};

typedef enum { COUNT, SKIP, FILL } pmode;

typedef struct {
  const char *p, *end;
  pmode mode;
  jval *vals; /* next free value (FILL) */
  char *strs; /* next free string byte (FILL) */
  size_t nvals, nstr;
  int depth;
} parser;

#define MAX_DEPTH 64

static void skip_ws(parser *ps) {
  while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')) ps->p++;
}

static int hex4(const char *s, unsigned *out) {
  unsigned v = 0;
  for (int i = 0; i < 4; i++) {
    char c = s[i];
    v <<= 4;
    if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
    else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
    else return -1;
  }
  *out = v;
  return 0;
}

static int put_utf8(char *out, unsigned cp) {
  unsigned char b[4];
  int n;
  if (cp < 0x80) {
    b[0] = (unsigned char)cp;
    n = 1;
  } else if (cp < 0x800) {
    b[0] = (unsigned char)(0xC0 | (cp >> 6));
    b[1] = (unsigned char)(0x80 | (cp & 0x3F));
    n = 2;
  } else if (cp < 0x10000) {
    b[0] = (unsigned char)(0xE0 | (cp >> 12));
    b[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    b[2] = (unsigned char)(0x80 | (cp & 0x3F));
    n = 3;
  } else {
    b[0] = (unsigned char)(0xF0 | (cp >> 18));
    b[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    b[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    b[3] = (unsigned char)(0x80 | (cp & 0x3F));
    n = 4;
  }
  if (out) memcpy(out, b, (size_t)n);
  return n;
}

/* the string at ps->p (on its quote), decoded into out unless out is NULL; its decoded length */
static int read_string(parser *ps, char *out) {
  int n = 0;
  ps->p++;
  while (ps->p < ps->end) {
    char c = *ps->p++;
    if (c == '"') {
      if (out) out[n] = 0;
      return n;
    }
    if ((unsigned char)c < 0x20) return -1;
    if (c != '\\') {
      if (out) out[n] = c;
      n++;
      continue;
    }
    if (ps->p >= ps->end) return -1;
    c = *ps->p++;
    unsigned cp;
    switch (c) {
    case '"':
    case '\\':
    case '/': cp = (unsigned char)c; break;
    case 'b': cp = '\b'; break;
    case 'f': cp = '\f'; break;
    case 'n': cp = '\n'; break;
    case 'r': cp = '\r'; break;
    case 't': cp = '\t'; break;
    case 'u':
      if (ps->end - ps->p < 4 || hex4(ps->p, &cp)) return -1;
      ps->p += 4;
      /* a pair of surrogates makes one character beyond U+FFFF */
      if (cp >= 0xD800 && cp < 0xDC00 && ps->end - ps->p >= 6 && ps->p[0] == '\\' && ps->p[1] == 'u') {
        unsigned lo;
        if (!hex4(ps->p + 2, &lo) && lo >= 0xDC00 && lo < 0xE000) {
          cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          ps->p += 6;
        }
      }
      if (cp >= 0xD800 && cp < 0xE000) cp = 0xFFFD;
      break;
    default: return -1;
    }
    n += put_utf8(out ? out + n : NULL, cp);
  }
  return -1;
}

/* a string value or key: counted, skipped or copied out depending on the pass */
static const char *take_string(parser *ps, int *len) {
  char *out = ps->mode == FILL ? ps->strs : NULL;
  int n = read_string(ps, out);
  if (n < 0) return NULL;
  if (ps->mode == COUNT) ps->nstr += (size_t)n + 1;
  if (out) ps->strs += n + 1;
  *len = n;
  return out ? out : "";
}

static int read_value(parser *ps, jval *v);

/* the members after the opening bracket, filled into items when given; their count, or -1 */
static int members(parser *ps, jval *items, int is_obj) {
  char close = is_obj ? '}' : ']';
  int i = 0;
  skip_ws(ps);
  if (ps->p < ps->end && *ps->p == close) {
    ps->p++;
    return 0;
  }
  for (;;) {
    const char *key = NULL;
    skip_ws(ps);
    if (is_obj) {
      int n;
      if (ps->p >= ps->end || *ps->p != '"' || !(key = take_string(ps, &n))) return -1;
      skip_ws(ps);
      if (ps->p >= ps->end || *ps->p != ':') return -1;
      ps->p++;
    }
    if (read_value(ps, items ? &items[i] : NULL)) return -1;
    if (items) items[i].key = key;
    i++;
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == ',') {
      ps->p++;
      continue;
    }
    if (ps->p < ps->end && *ps->p == close) {
      ps->p++;
      return i;
    }
    return -1;
  }
}

static int read_container(parser *ps, jval *v, int is_obj) {
  if (++ps->depth > MAX_DEPTH) return -1;
  ps->p++;
  v->type = is_obj ? J_OBJ : J_ARR;
  if (ps->mode == FILL) {
    const char *start = ps->p;
    ps->mode = SKIP;
    int n = members(ps, NULL, is_obj);
    ps->mode = FILL;
    if (n < 0) return -1;
    ps->p = start;
    v->len = n;
    v->u.items = ps->vals;
    ps->vals += n;
    if (members(ps, v->u.items, is_obj) != n) return -1;
  } else if (members(ps, NULL, is_obj) < 0) {
    return -1;
  }
  ps->depth--;
  return 0;
}

/* -?digits(.digits)?([eE][+-]?digits)? */
static int read_number(parser *ps, double *out) {
  const char *s = ps->p;
  if (s < ps->end && *s == '-') s++;
  if (s >= ps->end || *s < '0' || *s > '9') return -1;
  while (s < ps->end && ((*s >= '0' && *s <= '9') || *s == '.' || *s == 'e' || *s == 'E' ||
                         ((*s == '+' || *s == '-') && (s[-1] == 'e' || s[-1] == 'E'))))
    s++;
  char buf[64], *endp;
  size_t n = (size_t)(s - ps->p);
  if (n >= sizeof buf) return -1;
  memcpy(buf, ps->p, n);
  buf[n] = 0;
  *out = strtod(buf, &endp);
  if ((size_t)(endp - buf) != n) return -1;
  ps->p = s;
  return 0;
}

static int literal(parser *ps, const char *word) {
  size_t n = strlen(word);
  if ((size_t)(ps->end - ps->p) < n || memcmp(ps->p, word, n)) return -1;
  ps->p += n;
  return 0;
}

static int read_value(parser *ps, jval *v) {
  jval scratch;
  skip_ws(ps);
  if (ps->p >= ps->end) return -1;
  if (ps->mode == COUNT) ps->nvals++;
  if (!v) v = &scratch;
  char c = *ps->p;
  switch (c) {
  case '{':
  case '[': return read_container(ps, v, c == '{');
  case '"': {
    int n;
    const char *s = take_string(ps, &n);
    if (!s) return -1;
    v->type = J_STR;
    v->len = n;
    v->u.str = s;
    return 0;
  }
  case 't': v->type = J_TRUE; return literal(ps, "true");
  case 'f': v->type = J_FALSE; return literal(ps, "false");
  case 'n': v->type = J_NULL; return literal(ps, "null");
  default: v->type = J_NUM; return read_number(ps, &v->u.num);
  }
}

jdoc *json_parse(const char *text, size_t len) {
  parser ps = {text, text + len, COUNT, NULL, NULL, 0, 0, 0};
  if (read_value(&ps, NULL)) return NULL;
  skip_ws(&ps);
  if (ps.p != ps.end) return NULL;
  jdoc *d = malloc(sizeof(jdoc) + ps.nvals * sizeof(jval) + ps.nstr);
  if (!d) return NULL;
  jval *vals = (jval *)(d + 1);
  parser fill = {text, text + len, FILL, vals + 1, (char *)(vals + ps.nvals), 0, 0, 0};
  d->root = vals;
  vals[0].key = NULL;
  if (read_value(&fill, vals)) {
    free(d);
    return NULL;
  }
  return d;
}

const jval *json_root(const jdoc *d) { return d ? d->root : &json_null; }
void json_free(jdoc *d) { free(d); }

const jval *json_get(const jval *obj, const char *key) {
  if (!obj || obj->type != J_OBJ) return &json_null;
  for (int i = 0; i < obj->len; i++)
    if (!strcmp(obj->u.items[i].key, key)) return &obj->u.items[i];
  return &json_null;
}

const jval *json_at(const jval *arr, int i) {
  if (!arr || (arr->type != J_ARR && arr->type != J_OBJ) || i < 0 || i >= arr->len) return &json_null;
  return &arr->u.items[i];
}

int json_len(const jval *v) { return v && (v->type == J_ARR || v->type == J_OBJ) ? v->len : 0; }

const char *json_str(const jval *v, const char *def) { return v && v->type == J_STR ? v->u.str : def; }

double json_num(const jval *v, double def) { return v && v->type == J_NUM ? v->u.num : def; }

int json_bool(const jval *v, int def) {
  if (!v) return def;
  if (v->type == J_TRUE) return 1;
  if (v->type == J_FALSE) return 0;
  return def;
}

void sb_add_json_str(sbuf *b, const char *s) {
  sb_add(b, "\"");
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (*p == '"' || *p == '\\') {
      char esc[2] = {'\\', (char)*p};
      sb_addn(b, esc, 2);
    } else if (*p < 0x20) {
      sb_addf(b, "\\u%04x", *p);
    } else {
      sb_addn(b, (const char *)p, 1);
    }
  }
  sb_add(b, "\"");
}
