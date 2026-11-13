/* OpenGL ES 2 entry points, looked up through SDL on every platform, so the same code runs
   with the TV's GLES2, Android's, or a desktop driver's ES 2 profile. */
#ifndef SC_GL_H
#define SC_GL_H

#include <stddef.h>
#include <stdint.h>

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef char GLchar;
typedef unsigned char GLubyte;
typedef ptrdiff_t GLsizeiptr;

#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ONE 1
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_ALPHA 0x1906
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_LUMINANCE 0x1909
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5

#define SC_GL_FUNCS(X)                                                                                    \
  X(void, ActiveTexture, (GLenum t))                                                                      \
  X(void, AttachShader, (GLuint p, GLuint s))                                                             \
  X(void, BindAttribLocation, (GLuint p, GLuint i, const GLchar *n))                                      \
  X(void, BindBuffer, (GLenum t, GLuint b))                                                               \
  X(void, BindFramebuffer, (GLenum t, GLuint f))                                                          \
  X(void, BindTexture, (GLenum t, GLuint x))                                                              \
  X(void, BlendFunc, (GLenum s, GLenum d))                                                                \
  X(void, BlendFuncSeparate, (GLenum sc, GLenum dc, GLenum sa, GLenum da))                                \
  X(void, BufferData, (GLenum t, GLsizeiptr n, const void *d, GLenum u))                                  \
  X(GLenum, CheckFramebufferStatus, (GLenum t))                                                           \
  X(void, Clear, (GLbitfield m))                                                                          \
  X(void, ClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))                                       \
  X(void, CompileShader, (GLuint s))                                                                      \
  X(GLuint, CreateProgram, (void))                                                                        \
  X(GLuint, CreateShader, (GLenum t))                                                                     \
  X(void, DeleteBuffers, (GLsizei n, const GLuint *b))                                                    \
  X(void, DeleteFramebuffers, (GLsizei n, const GLuint *f))                                               \
  X(void, DeleteTextures, (GLsizei n, const GLuint *t))                                                   \
  X(void, Disable, (GLenum c))                                                                            \
  X(void, DrawArrays, (GLenum m, GLint f, GLsizei n))                                                     \
  X(void, Enable, (GLenum c))                                                                             \
  X(void, EnableVertexAttribArray, (GLuint i))                                                            \
  X(void, Finish, (void))                                                                                 \
  X(void, FramebufferTexture2D, (GLenum t, GLenum a, GLenum tt, GLuint x, GLint l))                       \
  X(void, GenBuffers, (GLsizei n, GLuint *b))                                                             \
  X(void, GenFramebuffers, (GLsizei n, GLuint *f))                                                        \
  X(void, GenTextures, (GLsizei n, GLuint *t))                                                            \
  X(void, GetProgramInfoLog, (GLuint p, GLsizei n, GLsizei *l, GLchar *s))                                \
  X(void, GetProgramiv, (GLuint p, GLenum e, GLint *v))                                                   \
  X(void, GetShaderInfoLog, (GLuint s, GLsizei n, GLsizei *l, GLchar *x))                                 \
  X(void, GetShaderiv, (GLuint s, GLenum e, GLint *v))                                                    \
  X(GLint, GetUniformLocation, (GLuint p, const GLchar *n))                                               \
  X(void, LinkProgram, (GLuint p))                                                                        \
  X(void, PixelStorei, (GLenum e, GLint v))                                                               \
  X(void, ReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, void *d))              \
  X(void, Scissor, (GLint x, GLint y, GLsizei w, GLsizei h))                                              \
  X(void, ShaderSource, (GLuint s, GLsizei n, const GLchar *const *src, const GLint *len))                \
  X(void, TexImage2D, (GLenum t, GLint l, GLint i, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum y, const void *d)) \
  X(void, TexParameteri, (GLenum t, GLenum e, GLint v))                                                   \
  X(void, TexSubImage2D, (GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum y2, const void *d)) \
  X(void, Uniform1i, (GLint l, GLint v))                                                                  \
  X(void, Uniform2f, (GLint l, GLfloat a, GLfloat b))                                                     \
  X(void, Uniform4f, (GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d))                               \
  X(void, UseProgram, (GLuint p))                                                                         \
  X(void, VertexAttribPointer, (GLuint i, GLint n, GLenum t, GLboolean z, GLsizei s, const void *p))     \
  X(void, Viewport, (GLint x, GLint y, GLsizei w, GLsizei h))

#ifdef _WIN32
#define SC_GLAPI __stdcall
#else
#define SC_GLAPI
#endif

#define SC_GL_DECLARE(ret, name, args) typedef ret(SC_GLAPI *sc_gl_##name##_fn) args; extern sc_gl_##name##_fn gl##name;
SC_GL_FUNCS(SC_GL_DECLARE)
#undef SC_GL_DECLARE

/* 0 when every entry point was found; otherwise the number missing */
int sc_gl_load(void *(*lookup)(const char *name));

#endif
