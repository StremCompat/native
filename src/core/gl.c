#include "gl.h"

#define SC_GL_DEFINE(ret, name, args) sc_gl_##name##_fn gl##name;
SC_GL_FUNCS(SC_GL_DEFINE)
#undef SC_GL_DEFINE

int sc_gl_load(void *(*lookup)(const char *name)) {
  int missing = 0;
#define SC_GL_LOOKUP(ret, name, args)                                \
  gl##name = (sc_gl_##name##_fn)lookup("gl" #name);                \
  if (!gl##name) missing++;
  SC_GL_FUNCS(SC_GL_LOOKUP)
#undef SC_GL_LOOKUP
  return missing;
}
