/* JSON parser checks: values, escapes, nesting, and text that must be refused. */
#include <stdio.h>
#include <string.h>

#include "json.h"

static int failures;

#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #cond);   \
      failures++;                                                          \
    }                                                                      \
  } while (0)

static jdoc *parse(const char *s) { return json_parse(s, strlen(s)); }

static void values(void) {
  jdoc *d = parse(" {\"a\": 1.5, \"b\": [true, false, null, -2e3], \"c\": {\"d\": \"x\"}, \"e\": [] , \"f\": {}} ");
  CHECK(d);
  const jval *r = json_root(d);
  CHECK(json_num(json_get(r, "a"), 0) == 1.5);
  CHECK(json_len(json_get(r, "b")) == 4);
  CHECK(json_bool(json_at(json_get(r, "b"), 0), 0) == 1);
  CHECK(json_bool(json_at(json_get(r, "b"), 1), 1) == 0);
  CHECK(json_at(json_get(r, "b"), 2)->type == J_NULL);
  CHECK(json_num(json_at(json_get(r, "b"), 3), 0) == -2000);
  CHECK(!strcmp(json_str(json_get(json_get(r, "c"), "d"), ""), "x"));
  CHECK(json_len(json_get(r, "e")) == 0 && json_get(r, "e")->type == J_ARR);
  CHECK(json_len(json_get(r, "f")) == 0 && json_get(r, "f")->type == J_OBJ);
  /* missing things read as the default, at any depth */
  CHECK(json_num(json_get(json_get(r, "zz"), "y"), 7) == 7);
  CHECK(!strcmp(json_str(json_at(json_get(r, "a"), 3), "def"), "def"));
  json_free(d);
}

static void strings(void) {
  /* the JSON text: ["a\"b\\c\/d\n", "é中", "😀", "plain é"] */
  jdoc *d = parse("[\"a\\\"b\\\\c\\/d\\n\", \"\\u00e9\\u4e2d\", \"\\ud83d\\ude00\", \"plain \xc3\xa9\"]");
  CHECK(d);
  const jval *r = json_root(d);
  CHECK(!strcmp(json_str(json_at(r, 0), ""), "a\"b\\c/d\n"));
  CHECK(!strcmp(json_str(json_at(r, 1), ""), "\xc3\xa9\xe4\xb8\xad"));
  CHECK(!strcmp(json_str(json_at(r, 2), ""), "\xf0\x9f\x98\x80"));
  CHECK(json_at(r, 3)->len == 8);
  json_free(d);
}

static void nesting(void) {
  jdoc *d = parse("{\"rows\":[{\"id\":\"tt1\",\"tags\":[\"x\",\"y\"]},{\"id\":\"tt2\",\"tags\":[]}],\"n\":2}");
  CHECK(d);
  const jval *rows = json_get(json_root(d), "rows");
  CHECK(json_len(rows) == 2);
  CHECK(!strcmp(json_str(json_get(json_at(rows, 1), "id"), ""), "tt2"));
  CHECK(!strcmp(json_str(json_at(json_get(json_at(rows, 0), "tags"), 1), ""), "y"));
  CHECK(json_num(json_get(json_root(d), "n"), 0) == 2);
  json_free(d);
}

static void refused(void) {
  static const char *bad[] = {"", "{", "[1,]", "{\"a\"}", "{\"a\":1,}", "[1 2]", "\"open", "tru", "01x", "[\"\x01\"]",
                              "{} x", "-", "[1.2.3]"};
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    jdoc *d = parse(bad[i]);
    if (d) fprintf(stderr, "accepted: %s\n", bad[i]);
    CHECK(!d);
    json_free(d);
  }
  char deep[200];
  memset(deep, '[', 100);
  memset(deep + 100, ']', 100);
  CHECK(!json_parse(deep, sizeof deep));
}

static void writing(void) {
  sbuf b = {0};
  sb_add_json_str(&b, "say \"hi\"\\\n");
  CHECK(!strcmp(b.s, "\"say \\\"hi\\\"\\\\\\u000a\""));
  jdoc *d = json_parse(b.s, b.len);
  CHECK(d && !strcmp(json_str(json_root(d), ""), "say \"hi\"\\\n"));
  json_free(d);
  sb_free(&b);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  values();
  strings();
  nesting();
  refused();
  writing();
  if (failures) fprintf(stderr, "%d failed\n", failures);
  else printf("json: all passed\n");
  return failures != 0;
}
