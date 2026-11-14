#include "webos_luna.h"

#include <glib.h>
#include <stdio.h>

#define APP_ID "io.github.alexesmm.legacytv"

static LSHandle *bus;
static char error[256];

const char *luna_error(void) { return error; }

int luna_ready(void) {
  if (bus) return 1;
  LSError err;
  LSErrorInit(&err);
  /* the app's own name first; an unnamed handle on systems that refuse it */
  if (!LSRegister(APP_ID, &bus, &err)) {
    LSErrorFree(&err);
    LSErrorInit(&err);
    if (!LSRegister(NULL, &bus, &err)) {
      snprintf(error, sizeof error, "Luna: %s", err.message ? err.message : "no bus");
      LSErrorFree(&err);
      bus = NULL;
      return 0;
    }
  }
  if (!LSGmainContextAttach(bus, g_main_context_default(), &err)) {
    snprintf(error, sizeof error, "Luna: %s", err.message ? err.message : "no main loop");
    LSErrorFree(&err);
    return 0;
  }
  return 1;
}

int luna_call(const char *uri, const char *payload, LSFilterFunc reply, LSMessageToken *token) {
  LSError err;
  if (!luna_ready()) return 0;
  LSErrorInit(&err);
  int ok = reply ? LSCall(bus, uri, payload, reply, NULL, token, &err)
                 : LSCallOneReply(bus, uri, payload, NULL, NULL, NULL, &err);
  if (!ok) {
    snprintf(error, sizeof error, "%s: %s", uri, err.message ? err.message : "refused");
    LSErrorFree(&err);
  }
  return ok;
}

void luna_cancel(LSMessageToken token) {
  LSError err;
  LSErrorInit(&err);
  if (bus && token && !LSCallCancel(bus, token, &err)) LSErrorFree(&err);
}

void luna_poll(void) {
  while (g_main_context_iteration(NULL, FALSE)) {
  }
}
