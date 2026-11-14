/* The update from inside the app: the server finds a newer version and downloads and checks
   it (/app/update); the system installs it and the app opens again (platform_install_update). */
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "nav.h"
#include "net.h"
#include "platform.h"
#include "screens.h"
#include "ui.h"
#include "util.h"

#define POLL_MS 2000

static int busy;

static void found(const jval *answer, void *user) {
  (void)user;
  const char *version = json_str(json_get(answer, "version"), "");
  if (*version) nav_offer_update(version);
}

void update_check(void) { api_get("/app/update", found, NULL); }

static void installed(const char *error) {
  busy = 0;
  if (error && *error) ui_toast(txf("updateFailed", error, NULL));
}

static double next_poll; /* while downloading: when to ask again, 0 otherwise */

static void state_came(const jval *answer, void *user) {
  (void)user;
  const char *state = json_str(json_get(answer, "state"), "");
  if (!strcmp(state, "ready")) {
    ui_toast(tx("updateInstalling"));
    platform_install_update(installed);
  } else if (!strcmp(state, "failed")) {
    busy = 0;
    ui_toast(txf("updateFailed", json_str(json_get(answer, "error"), ""), NULL));
  } else {
    next_poll = now_ms() + POLL_MS;
  }
}

void update_poll(void) {
  if (!next_poll || now_ms() < next_poll) return;
  next_poll = 0;
  api_get("/app/update", state_came, NULL);
}

static void started(net_reply *r, void *user) {
  (void)r;
  (void)user;
  api_get("/app/update", state_came, NULL);
}

void update_start(void) {
  if (busy) return;
  busy = 1;
  ui_toast(tx("updateDownloading"));
  net_request("/app/update", "{}", NET_HIGH, NULL, started, NULL);
}
