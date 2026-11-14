/* net.h for the WebAssembly build (Samsung): the browser's XMLHttpRequest instead of sockets
   and threads. Answers come back to the page's event loop and wait in a list until net_poll
   hands them to the screens, as the threaded version does; the work step runs there too. */
#include "net.h"

#include <emscripten.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

typedef struct job {
  struct job *next;
  net_work_fn work;
  net_done_fn done;
  void *user;
  net_reply reply;
} job;

static job *finished, **finished_tail = &finished;
static int pending, port;

/* the body comes as a binary string (x-user-defined), copied into the C heap with a zero byte */
EM_JS(void, js_request, (int port, const char *path, const char *body, int sync, void *job), {
  var x = new XMLHttpRequest();
  var url = "http://127.0.0.1:" + port + UTF8ToString(path);
  function answer() {
    var text = x.responseText || "", n = text.length, at = _malloc(n + 1);
    for (var i = 0; i < n; i++) HEAPU8[at + i] = text.charCodeAt(i) & 255;
    HEAPU8[at + n] = 0;
    _web_net_answer(job, x.status, at, n);
  }
  x.open(body ? "POST" : "GET", url, !sync);
  /* a plain POST, which needs no CORS preflight; the server reads the JSON whatever its type */
  x.overrideMimeType("text/plain; charset=x-user-defined");
  if (!sync) x.onloadend = answer;
  try {
    x.send(body ? UTF8ToString(body) : null);
  } catch (e) {}
  if (sync) answer();
});

EMSCRIPTEN_KEEPALIVE void web_net_answer(job *j, int status, char *body, int len) {
  j->reply.status = status;
  j->reply.body = body;
  j->reply.len = (size_t)len;
  if (!j->done) return; /* net_fetch's own reply, read where it waits */
  j->next = NULL;
  *finished_tail = j;
  finished_tail = &j->next;
}

int net_init(int server_port, int workers) {
  (void)workers;
  port = server_port;
  return 0;
}

void net_shutdown(void) {}

void net_request(const char *path, const char *body, int priority, net_work_fn work, net_done_fn done, void *user) {
  (void)priority;
  job *j = calloc(1, sizeof *j);
  j->work = work;
  j->done = done;
  j->user = user;
  pending++;
  js_request(port, path, body, 0, j);
}

void net_fetch(const char *path, const char *body, net_reply *r) {
  job j = {0};
  js_request(port, path, body, 1, &j);
  *r = j.reply;
  r->result = NULL;
}

void net_poll(void) {
  job *list = finished;
  finished = NULL;
  finished_tail = &finished;
  while (list) {
    job *j = list;
    list = j->next;
    if (j->work) j->work(&j->reply, j->user);
    j->done(&j->reply, j->user);
    pending--;
    free(j->reply.body);
    free(j);
  }
}

int net_pending(void) { return pending; }
