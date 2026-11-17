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
  int sync; /* net_fetch's own, read where it waits */
  net_reply reply;
} job;

static job *finished, **finished_tail = &finished;
static int pending, port;

/* The body comes as a binary string (x-user-defined), copied into the C heap with a zero byte.
   The browser opens at most six connections to one address: the slow lane (NET_SLOW) asks the
   same server as "localhost", which counts as another, so a request the server holds never keeps
   the pictures waiting. If that name does not reach the server, the lane is dropped. */
EM_JS(void, js_request, (int port, const char *path, const char *body, int sync, int slow, void *job), {
  var where = UTF8ToString(path), sent = body ? UTF8ToString(body) : null;
  function ask(lane) {
    var x = new XMLHttpRequest();
    function answer() {
      if (lane && !x.status) {
        Module.scSlowLaneOff = true;
        return ask(false);
      }
      var text = x.responseText || "", n = text.length, at = _malloc(n + 1);
      for (var i = 0; i < n; i++) HEAPU8[at + i] = text.charCodeAt(i) & 255;
      HEAPU8[at + n] = 0;
      _web_net_answer(job, x.status, at, n);
    }
    x.open(sent !== null ? "POST" : "GET", "http://" + (lane ? "localhost" : "127.0.0.1") + ":" + port + where, !sync);
    /* a plain POST, which needs no CORS preflight; the server reads the JSON whatever its type */
    x.overrideMimeType("text/plain; charset=x-user-defined");
    if (!sync) x.onloadend = answer;
    try {
      x.send(sent);
    } catch (e) {}
    if (sync) answer();
  }
  ask(slow && !sync && !Module.scSlowLaneOff);
});

EMSCRIPTEN_KEEPALIVE void web_net_answer(job *j, int status, char *body, int len) {
  j->reply.status = status;
  j->reply.body = body;
  j->reply.len = (size_t)len;
  if (j->sync) return;
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
  job *j = calloc(1, sizeof *j);
  j->work = work;
  j->done = done;
  j->user = user;
  pending++;
  js_request(port, path, body, 0, priority == NET_SLOW, j);
}

void net_fetch(const char *path, const char *body, net_reply *r) {
  job j = {.sync = 1};
  js_request(port, path, body, 1, 0, &j);
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
    if (j->done) j->done(&j->reply, j->user);
    pending--;
    free(j->reply.body);
    free(j);
  }
}

int net_pending(void) { return pending; }
