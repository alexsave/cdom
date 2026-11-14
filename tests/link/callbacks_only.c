/* An app on the callbacks-only profile (src/cdom_callbacks_only.h): cd_on and the _cb forms compile.
 * With -DUSE=<name>, the same file uses one poisoned call and must not compile (run.sh). */
#include "../../src/cdom.h"
#include "../../src/cdom_net.h"
#include "../../src/cdom_store.h"
#include "../../src/cdom_callbacks_only.h"

static void on_click(const cd_event_rec *e, void *ctx) { (void)e; (void)ctx; }
static void on_timer(void *ctx) { (void)ctx; }
static void on_http(void *ctx, uint32_t status, const uint8_t *p, uint32_t len) { (void)ctx; (void)status; (void)p; (void)len; }
static void on_got(void *ctx, const uint8_t *p, int32_t len) { (void)ctx; (void)p; (void)len; }
void cd_main(void) {
  cd_id b = cd_open_id(CD_LIT("button"));
  cd_close();
  cd_on(b, CD_EV_CLICK, on_click, 0);
  cd_timer_cb(10, on_timer, 0);
  cd_http_cb(on_http, 0, "GET", 3, "/", 1, 0, 0, 0, 0, 0);
  cd_store_get_cb(on_got, 0, "k", 1, CD_STORE_DEADLINE_MS);
#ifdef USE_LISTEN
  cd_listen(b, CD_EV_CLICK);
#endif
#ifdef USE_HTTP
  cd_http(1, "GET", 3, "/", 1, 0, 0, 0, 0, 0);
#endif
#ifdef USE_TIMER
  cd_timer_start(10, 1);
#endif
#ifdef USE_STORE
  cd_store_get(1, "k", 1, CD_STORE_DEADLINE_MS);
#endif
}
