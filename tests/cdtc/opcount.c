/* Op counts of the templated rows app (bench/cdom-tpl, -DTPL) against the hand-written one
 * (bench/cdom-keyed/src/app.c): both run natively into the C sink, which counts the ops
 * every action costs. tests/cdtc/run.sh builds both, requires identical output, and
 * requires the 1000-row create to be 11 ops a row and a label patch to be 6. */
#include "cdom.h"
#include "cdom_html.h"
#include <stdio.h>

extern uint32_t cd_seed;
static uint64_t mark;
static void report(const char *what) {
  cd_flush();
  printf("%s %llu\n", what, (unsigned long long)(cd_html_ops() - mark));
  mark = cd_html_ops();
}

#ifdef TPL
#include "rows.h"
/* what cd_on_event does: the handler, then the render */
static void act(void (*h)(Rows *, const cd_event_rec *)) {
  cd_event_rec e = {CD_EV_CLICK, 0, 0, 0, 0, 0, 0, 0, 0};
  h(&S, &e);
  page_render(&S);
}
#define RUN act(run)
#define RUNLOTS act(runlots)
#define ADD act(add)
#define UPDATE act(update)
#define CLEAR act(clear)
#define SWAP act(swaprows)
static void cell(uint32_t row, int c) {
  cd_event_rec e = {CD_EV_CLICK, 0, 0, 0, 0, 0, 0, c, 1};
  for (uint32_t i = 0; i < S.rows_n; i++)
    if (S.rows[i].id == row) { row_click(&S, &S.rows[i], &e); break; }
  page_render(&S);
}
#else
static cd_id by_tag(uint32_t t) {
  for (cd_id id = 1; id < (1u << 20); id++)
    if (cd_tag(id) == t) return id;
  return 0;
}
static void click(uint32_t tag, int path, int depth) {
  cd_event_rec e = {CD_EV_CLICK, (int32_t)by_tag(tag), 0, 0, 0, 0, 0, path, depth};
  cd_on_event(&e);
}
#define RUN click(1, 0, 0)
#define RUNLOTS click(2, 0, 0)
#define ADD click(3, 0, 0)
#define UPDATE click(4, 0, 0)
#define CLEAR click(5, 0, 0)
#define SWAP click(6, 0, 0)
static void cell(uint32_t row, int c) { click(0x80000000u | row, c, 1); }
#endif

int main(void) {
  cd_html_reset();
  cd_seed = 42;
  cd_main();
  report("start");
  RUN; report("run1000");
  UPDATE; report("update10th");
  cell(5, 1); report("select");
  cell(7, 1); report("reselect");
  SWAP; report("swap");
  cell(3, 2); report("remove");
  ADD; report("append1000");
  RUN; report("replace1000");
  RUNLOTS; report("run10000");
  CLEAR; report("clear");
  RUN; report("run_after_clear");
  return 0;
}
