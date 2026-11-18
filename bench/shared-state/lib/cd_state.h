/* cd_state: a PROTOTYPE of the shared-state design (docs/shared-state-design.md), written on cdom's public
 * API so that it can be measured without touching src/. In cdom itself it would use the region that is
 * building (its depth, its takedown) and the render queue directly; what differs here is said at each place.
 *
 * A cd_state is what a module keeps beside a piece of data that others show: a version, and the list of
 * the views that draw from it. The data stays the module's own plain C.
 *   static char name[256]; static uint32_t name_len;      (name.c: the data, private to the file)
 *   cd_state name_state;                                   (name.h: extern, for the views)
 *   void name_set(const char *p, uint32_t n) { memcpy(name, p, n); name_len = n; cd_changed(&name_state); }
 * A view says what it draws from, while its region builds, and draws itself once:
 *   cd_watch(&name_state, badge_view, nullptr); badge_view(nullptr);
 * cd_changed(s) moves s->ver on and asks for every view watching s: each runs once in the flush that ends
 * the entry, however many of the states it watches changed and however often. It costs the same whether s
 * has no watcher or ten thousand (the views are found in the flush, not in the setter).
 * s->ver only goes up, so the sum of the versions of everything a value is made from is a version of that
 * value: a list row's revision (docs/list-design.md), a memo's key. */
#ifndef CD_STATE_PROTO_H
#define CD_STATE_PROTO_H
#include "cdom.h"
/* cdom.h now has cd_state of its own (the implementation, in src/): the prototype's names are renamed
 * here, after cdom.h, so the prototype builds beside it and its entries measure what they measured. */
#define cd_state cdp_state
#define cd_changed cdp_changed
#define cd_watch cdp_watch
#define cd_read cdp_read

typedef struct cd_watcher cd_watcher;
typedef struct cd_state {
  uint32_t ver;                 /* moves on at every cd_changed; read it freely */
  uint32_t asked;               /* the prototype's: on the list of changed states */
  cd_watcher *first, *last;     /* the views watching, oldest first */
  struct cd_state *next_asked;
} cd_state;
struct cd_watcher {
  cd_state *of; cd_watcher *next, *prev;
  cd_watcher *view;             /* the first watcher of the same (fn, ctx): one view, however many states */
  cd_render_fn fn; void *ctx;
  uint32_t round, slot;         /* on the view: the round it was last queued in, and its place in the queue + 1 */
};

void cd_changed(cd_state *s);
/* The region building now draws fn(ctx) from s. The watch goes when the region does (a cd_on_free cleanup),
 * with any run of the view still pending. Calls with the same (fn, ctx) one after another are one view
 * that watches several states. Traps when no region builds (cd_on_free does). */
void cd_watch(cd_state *s, cd_render_fn fn, void *ctx);
/* The same with the watcher's memory the caller's (a row's struct, a static), for code that is not in a
 * region's build: w must stay where it is until cd_unwatch(w). view is 0, or the watcher already made for
 * the same (fn, ctx) on another state. */
void cd_watch_at(cd_watcher *w, cd_state *s, cd_render_fn fn, void *ctx, cd_watcher *view);
void cd_unwatch(cd_watcher *w);

/* The check build (-DCD_STATE_CHECK): a getter calls cd_read(&its_state), and a view that runs in the
 * flush and reads a state it does not watch traps there. It is an assertion over the explicit watches,
 * nothing is wired by it; without the flag it is nothing. */
#ifdef CD_STATE_CHECK
void cd_read(const cd_state *s);
#else
static inline void cd_read(const cd_state *) {}
#endif
uint32_t cd_state_runs(void);   /* views run since start, for tests */
#endif
