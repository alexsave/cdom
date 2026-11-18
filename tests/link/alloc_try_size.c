/* The non-trapping heap path costs only an app that calls the _try forms: built with -DTRY it calls
 * cd_alloc_try, without it cd_alloc (run.sh bounds the gap). */
#include "../../src/cdom.h"
static cd_id t; static void *buf;
/* The size is read at run time, so neither build knows it: both carry the small path and the large one. */
static volatile uint32_t size = 1u << 20;
static void click(const cd_event_rec *, void *) {
#ifdef TRY
  void *p = cd_alloc_try(size); if (!p) { cd_set_text_node(t, "too big"); return; } buf = p;
#else
  buf = cd_alloc(size);
#endif
  cd_set_text_node_num(t, (uint32_t)(uintptr_t)buf); }
void cd_main(void) { cd_id b = cd_open_id("button"); cd_text_num(0); cd_name_text(t = cd_id_new()); cd_close(); cd_on(b, CD_EV_CLICK, click, nullptr); }
