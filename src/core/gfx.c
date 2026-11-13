#include "gfx.h"
#include "gl.h"

#include <stdio.h>
#include <string.h>

/* One vertex: position, texture position, colour, and the rectangle it belongs to (size,
   position inside it, corner radius), so the fragment shader can round the corners. */
typedef struct {
  float x, y;
  float u, v;
  float r, g, b, a;
  float lx, ly;   /* position inside the rectangle */
  float hw, hh;   /* half size of the rectangle */
  float radius;
  float mode;     /* 0 colour, 1 picture, 2 glyph, 3 ring */
  float ring;     /* ring width */
} vertex;

#define MAX_VERTS 6144
#define STACK 16

static vertex verts[MAX_VERTS];
static int nverts;
static unsigned int prog, vbo, white;
static unsigned int bound_tex;
static int u_scale, u_tex;
static int surf_w, surf_h;
static float scale_x, scale_y;
static int calls, last_calls;

static float opacity[STACK];
static int nopacity;
static float offx[STACK], offy[STACK];
static int noffset;
typedef struct { float x, y, w, h; } box;
static box clips[STACK];
static int nclips;

static const char *VS =
    "attribute vec2 a_pos; attribute vec2 a_uv; attribute vec4 a_color; attribute vec2 a_local;"
    "attribute vec2 a_half; attribute vec3 a_shape;"
    "uniform vec2 u_scale;"
    "varying vec2 v_uv; varying vec4 v_color; varying vec2 v_local; varying vec2 v_half; varying vec3 v_shape;"
    "void main(){ v_uv=a_uv; v_color=a_color; v_local=a_local; v_half=a_half; v_shape=a_shape;"
    " gl_Position=vec4(a_pos.x*u_scale.x-1.0, 1.0-a_pos.y*u_scale.y, 0.0, 1.0); }";

/* the rounded box distance gives soft, antialiased corners without extra geometry */
static const char *FS =
    "precision mediump float;"
    "uniform sampler2D u_tex;"
    "varying vec2 v_uv; varying vec4 v_color; varying vec2 v_local; varying vec2 v_half; varying vec3 v_shape;"
    "float box(vec2 p, vec2 h, float r){ vec2 q=abs(p)-h+vec2(r); return length(max(q,0.0))+min(max(q.x,q.y),0.0)-r; }"
    "void main(){"
    " vec4 c=v_color; float mode=v_shape.y;"
    " if(mode>0.5 && mode<1.5){ c=texture2D(u_tex,v_uv)*v_color; }"
    " else if(mode>1.5 && mode<2.5){ c=vec4(v_color.rgb, v_color.a*texture2D(u_tex,v_uv).a); }"
    " if(v_shape.x>0.0 || mode>2.5){"
    "  float d=box(v_local, v_half, v_shape.x);"
    "  float inside=clamp(0.5-d,0.0,1.0);"
    "  if(mode>2.5){ inside*=clamp(d+v_shape.z+0.5,0.0,1.0); }"
    "  c.a*=inside;"
    " }"
    " gl_FragColor=c; }";

static unsigned int shader(unsigned int type, const char *src) {
  unsigned int s = glCreateShader(type);
  int ok = 0;
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetShaderInfoLog(s, sizeof log, NULL, log);
    fprintf(stderr, "shader: %s\n", log);
  }
  return s;
}

int gfx_init(void) {
  int ok = 0;
  unsigned char px[4] = {255, 255, 255, 255};
  prog = glCreateProgram();
  glAttachShader(prog, shader(GL_VERTEX_SHADER, VS));
  glAttachShader(prog, shader(GL_FRAGMENT_SHADER, FS));
  glBindAttribLocation(prog, 0, "a_pos");
  glBindAttribLocation(prog, 1, "a_uv");
  glBindAttribLocation(prog, 2, "a_color");
  glBindAttribLocation(prog, 3, "a_local");
  glBindAttribLocation(prog, 4, "a_half");
  glBindAttribLocation(prog, 5, "a_shape");
  glLinkProgram(prog);
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetProgramInfoLog(prog, sizeof log, NULL, log);
    fprintf(stderr, "program: %s\n", log);
    return -1;
  }
  u_scale = glGetUniformLocation(prog, "u_scale");
  u_tex = glGetUniformLocation(prog, "u_tex");
  glGenBuffers(1, &vbo);
  glGenTextures(1, &white);
  glBindTexture(GL_TEXTURE_2D, white);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  return 0;
}

static void flush(void) {
  if (!nverts) return;
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nverts * sizeof(vertex)), verts, GL_STREAM_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, 0, sizeof(vertex), (void *)offsetof(vertex, x));
  glVertexAttribPointer(1, 2, GL_FLOAT, 0, sizeof(vertex), (void *)offsetof(vertex, u));
  glVertexAttribPointer(2, 4, GL_FLOAT, 0, sizeof(vertex), (void *)offsetof(vertex, r));
  glVertexAttribPointer(3, 2, GL_FLOAT, 0, sizeof(vertex), (void *)offsetof(vertex, lx));
  glVertexAttribPointer(4, 2, GL_FLOAT, 0, sizeof(vertex), (void *)offsetof(vertex, hw));
  glVertexAttribPointer(5, 3, GL_FLOAT, 0, sizeof(vertex), (void *)offsetof(vertex, radius));
  glDrawArrays(GL_TRIANGLES, 0, nverts);
  nverts = 0;
  calls++;
}

static void use_texture(unsigned int id) {
  if (id == bound_tex) return;
  flush();
  glBindTexture(GL_TEXTURE_2D, id);
  bound_tex = id;
}

static void apply_clip(void) {
  flush();
  if (!nclips) {
    glDisable(GL_SCISSOR_TEST);
    return;
  }
  box c = clips[nclips - 1];
  int x = (int)(c.x * surf_w / SC_DESIGN_W), w = (int)(c.w * surf_w / SC_DESIGN_W + 0.999f);
  int h = (int)(c.h * surf_h / SC_DESIGN_H + 0.999f), y = surf_h - (int)(c.y * surf_h / SC_DESIGN_H) - h;
  glEnable(GL_SCISSOR_TEST);
  glScissor(x, y, w < 0 ? 0 : w, h < 0 ? 0 : h);
}

void gfx_begin(int w, int h, uint32_t clear) {
  surf_w = w;
  surf_h = h;
  scale_x = 2.0f / SC_DESIGN_W;
  scale_y = 2.0f / SC_DESIGN_H;
  last_calls = calls;
  calls = 0;
  nopacity = 0;
  noffset = 0;
  nclips = 0;
  glViewport(0, 0, w, h);
  glDisable(GL_SCISSOR_TEST);
  glClearColor(((clear >> 24) & 255) / 255.0f, ((clear >> 16) & 255) / 255.0f, ((clear >> 8) & 255) / 255.0f,
               (clear & 255) / 255.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_BLEND);
  /* colour mixes by the source's alpha; the surface's own alpha adds up as one layer over
     another, so an opaque screen stays opaque (the TV composes it over the video plane) */
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(prog);
  glUniform2f(u_scale, scale_x, scale_y);
  glUniform1i(u_tex, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, white);
  bound_tex = white;
  for (int i = 0; i < 6; i++) glEnableVertexAttribArray((unsigned)i);
}

void gfx_end(void) { flush(); }

int gfx_draw_calls(void) { return last_calls; }

static float current_opacity(void) {
  float o = 1.0f;
  for (int i = 0; i < nopacity; i++) o *= opacity[i];
  return o;
}

static void unpack(uint32_t c, float o, float *out) {
  out[0] = ((c >> 24) & 255) / 255.0f;
  out[1] = ((c >> 16) & 255) / 255.0f;
  out[2] = ((c >> 8) & 255) / 255.0f;
  out[3] = (c & 255) / 255.0f * o;
}

/* a quad with per-corner colours: c[0] top left, c[1] top right, c[2] bottom right, c[3] bottom left */
static void quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, const float c[4][4],
                 float radius, float mode, float ring) {
  if (w <= 0 || h <= 0) return;
  if (nverts + 6 > MAX_VERTS) flush();
  if (noffset) {
    x += offx[noffset - 1];
    y += offy[noffset - 1];
  }
  float hw = w / 2, hh = h / 2;
  float xs[4] = {x, x + w, x + w, x}, ys[4] = {y, y, y + h, y + h};
  float us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
  float lxs[4] = {-hw, hw, hw, -hw}, lys[4] = {-hh, -hh, hh, hh};
  static const int order[6] = {0, 1, 2, 0, 2, 3};
  if (radius > hw) radius = hw;
  if (radius > hh) radius = hh;
  for (int i = 0; i < 6; i++) {
    int k = order[i];
    vertex *v = &verts[nverts++];
    v->x = xs[k];
    v->y = ys[k];
    v->u = us[k];
    v->v = vs[k];
    v->r = c[k][0];
    v->g = c[k][1];
    v->b = c[k][2];
    v->a = c[k][3];
    v->lx = lxs[k];
    v->ly = lys[k];
    v->hw = hw;
    v->hh = hh;
    v->radius = radius;
    v->mode = mode;
    v->ring = ring;
  }
}

static void solid(float x, float y, float w, float h, float radius, uint32_t a, uint32_t b, int vertical, float mode,
                  float ring) {
  float o = current_opacity(), ca[4], cb[4], c[4][4];
  if (o <= 0.001f) return;
  use_texture(white);
  unpack(a, o, ca);
  unpack(b, o, cb);
  for (int i = 0; i < 4; i++) {
    int second = vertical ? (i >= 2) : (i == 1 || i == 2);
    memcpy(c[i], second ? cb : ca, sizeof ca);
  }
  quad(x, y, w, h, 0, 0, 1, 1, c, radius, mode, ring);
}

void gfx_rect(float x, float y, float w, float h, float radius, uint32_t color) {
  solid(x, y, w, h, radius, color, color, 0, 0, 0);
}

void gfx_gradient(float x, float y, float w, float h, float radius, uint32_t a, uint32_t b, int vertical) {
  solid(x, y, w, h, radius, a, b, vertical, 0, 0);
}

void gfx_outline(float x, float y, float w, float h, float radius, float width, uint32_t color) {
  solid(x, y, w, h, radius, color, color, 0, 3, width);
}

void gfx_image(const sc_tex *t, float x, float y, float w, float h, float radius, float opacity_, int cover) {
  float o = current_opacity() * opacity_, c[4][4];
  float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
  if (!t || !t->id || o <= 0.001f) return;
  if (cover && t->w > 0 && t->h > 0) {
    float box_ratio = w / h, pic_ratio = (float)t->w / (float)t->h;
    if (pic_ratio > box_ratio) {
      float keep = box_ratio / pic_ratio;
      u0 = (1 - keep) / 2;
      u1 = 1 - u0;
    } else {
      float keep = pic_ratio / box_ratio;
      v0 = cover == COVER_TOP ? 0 : (1 - keep) / 2;
      v1 = v0 + keep;
    }
  }
  use_texture(t->id);
  for (int i = 0; i < 4; i++) c[i][0] = c[i][1] = c[i][2] = 1, c[i][3] = o;
  quad(x, y, w, h, u0, v0, u1, v1, c, radius, 1, 0);
}

void gfx_alpha_quad(const sc_tex *t, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                    uint32_t color) {
  float o = current_opacity(), cc[4], c[4][4];
  if (!t || !t->id || o <= 0.001f) return;
  use_texture(t->id);
  unpack(color, o, cc);
  for (int i = 0; i < 4; i++) memcpy(c[i], cc, sizeof cc);
  quad(x, y, w, h, u0, v0, u1, v1, c, 0, 2, 0);
}

void gfx_push_opacity(float o) {
  if (nopacity < STACK) opacity[nopacity++] = o;
}
void gfx_pop_opacity(void) {
  if (nopacity) nopacity--;
}

void gfx_push_offset(float dx, float dy) {
  float bx = noffset ? offx[noffset - 1] : 0, by = noffset ? offy[noffset - 1] : 0;
  if (noffset < STACK) {
    offx[noffset] = bx + dx;
    offy[noffset] = by + dy;
    noffset++;
  }
}
void gfx_pop_offset(void) {
  if (noffset) noffset--;
}

void gfx_push_clip(float x, float y, float w, float h) {
  if (noffset) {
    x += offx[noffset - 1];
    y += offy[noffset - 1];
  }
  if (nclips) {
    box p = clips[nclips - 1];
    float x2 = x + w, y2 = y + h;
    if (x < p.x) x = p.x;
    if (y < p.y) y = p.y;
    if (x2 > p.x + p.w) x2 = p.x + p.w;
    if (y2 > p.y + p.h) y2 = p.y + p.h;
    w = x2 - x;
    h = y2 - y;
  }
  if (nclips < STACK) clips[nclips++] = (box){x, y, w, h};
  apply_clip();
}
void gfx_pop_clip(void) {
  if (nclips) nclips--;
  apply_clip();
}

sc_tex gfx_texture(int w, int h, const unsigned char *pixels, int alpha_only) {
  sc_tex t = {0, w, h, alpha_only};
  flush();
  glGenTextures(1, &t.id);
  glBindTexture(GL_TEXTURE_2D, t.id);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, alpha_only ? GL_ALPHA : GL_RGBA, w, h, 0, alpha_only ? GL_ALPHA : GL_RGBA,
               GL_UNSIGNED_BYTE, pixels);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, bound_tex);
  return t;
}

void gfx_texture_update(sc_tex *t, int x, int y, int w, int h, const unsigned char *pixels) {
  flush();
  glBindTexture(GL_TEXTURE_2D, t->id);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, t->alpha_only ? GL_ALPHA : GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  glBindTexture(GL_TEXTURE_2D, bound_tex);
}

void gfx_texture_free(sc_tex *t) {
  if (!t->id) return;
  flush();
  if (bound_tex == t->id) bound_tex = 0;
  glDeleteTextures(1, &t->id);
  t->id = 0;
}

int gfx_read(unsigned char *out, int w, int h) {
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out);
  /* GL rows start at the bottom */
  for (int y = 0; y < h / 2; y++) {
    unsigned char *a = out + (size_t)y * w * 4, *b = out + (size_t)(h - 1 - y) * w * 4, tmp;
    for (int i = 0; i < w * 4; i++) tmp = a[i], a[i] = b[i], b[i] = tmp;
  }
  return 0;
}
