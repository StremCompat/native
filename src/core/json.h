/* JSON from the local server: parsed once into a tree that lives in one block of memory, read
   with lookups that never fail (a missing key reads as null, and null reads as the default). */
#ifndef SC_JSON_H
#define SC_JSON_H

#include <stddef.h>

#include "util.h"

typedef enum { J_NULL, J_FALSE, J_TRUE, J_NUM, J_STR, J_ARR, J_OBJ } jtype;

typedef struct jval jval;
struct jval {
  jtype type;
  int len;          /* items of an array or object, bytes of a string */
  const char *key;  /* when this value sits in an object */
  union {
    double num;
    const char *str;
    jval *items;
  } u;
};

typedef struct jdoc jdoc;

/* NULL when the text is not JSON */
jdoc *json_parse(const char *text, size_t len);
const jval *json_root(const jdoc *d);
void json_free(jdoc *d);

/* the shared null, returned for anything missing */
extern const jval json_null;

const jval *json_get(const jval *obj, const char *key);
const jval *json_at(const jval *arr, int i);
int json_len(const jval *v);
const char *json_str(const jval *v, const char *def);
double json_num(const jval *v, double def);
int json_bool(const jval *v, int def);

/* a string value quoted and escaped, for requests */
void sb_add_json_str(sbuf *b, const char *s);

#endif
