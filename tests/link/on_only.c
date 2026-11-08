/* An app that uses cd_on alone and defines no cd_on_event: it links against cdom's weak default, an
 * event with a handler reaches it with its ctx, and one without goes to the default. */
#include "../../src/cdom.h"

static uint32_t heard;
static cd_id node;
static void on_click(const cd_event_rec *e, void *ctx) { (void)e; ++*(uint32_t *)ctx; }
void cd_main(void) {
  node = cd_id_new();
  cd_on(node, CD_EV_CLICK, on_click, &heard);
}
__attribute__((export_name("node"))) cd_id node_id(void) { return node; }
__attribute__((export_name("heard"))) uint32_t heard_n(void) { return heard; }
