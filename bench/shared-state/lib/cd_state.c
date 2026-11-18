#include "cd_state.h"

#define ROUNDS 16
static cd_state *asked_first, *asked_last;
static cd_watcher **queue;
static uint32_t nqueue, queue_cap, round_no = 1, in_pass, runs;
[[maybe_unused]] static cd_watcher *running; /* the view running now, for the check build */
static cd_watcher *recent; /* the view cd_watch made last. Prototype only: cdom would look among the building region's own views,
                            * so the same (fn, ctx) watched from two regions, one right after the other, is two views there and one here. */

static void pass(void *);
void cd_changed(cd_state *s) {
  s->ver++;
  if (s->asked) return;
  s->asked = 1;
  s->next_asked = nullptr;
  if (asked_last) asked_last->next_asked = s; else asked_first = s;
  asked_last = s;
  if (!in_pass) cd_dirty(pass, nullptr); /* in cdom: the flush runs the pass itself */
}

/* One round: the states changed so far give their views, each once, in the order the states changed and
 * then the order the views began to watch; then the views run. A view that changes a state is another round. */
static void pass(void *) {
  in_pass = 1;
  for (uint32_t r = 0; asked_first; r++) {
    if (r == ROUNDS) __builtin_trap(); /* views that keep changing what they watch */
    round_no++;
    nqueue = 0;
    for (cd_state *s = asked_first, *n; s; s = n) {
      n = s->next_asked;
      s->asked = 0;
      for (cd_watcher *w = s->first; w; w = w->next) {
        cd_watcher *v = w->view;
        if (v->round == round_no) continue;
        v->round = round_no;
        if (nqueue == queue_cap) {
          queue_cap = queue_cap ? queue_cap * 2 : 64;
          queue = cd_realloc(queue, queue_cap * sizeof *queue);
        }
        queue[nqueue++] = v;
        v->slot = nqueue;
      }
    }
    asked_first = asked_last = nullptr;
    for (uint32_t i = 0; i < nqueue; i++) {
      cd_watcher *v = queue[i];
      if (!v) continue; /* its region went earlier in this round */
      v->slot = 0;
      running = v;
      runs++;
      v->fn(v->ctx);
    }
    running = nullptr;
  }
  in_pass = 0;
}

void cd_watch_at(cd_watcher *w, cd_state *s, cd_render_fn fn, void *ctx, cd_watcher *view) {
  *w = (cd_watcher){.of = s, .prev = s->last, .view = view ? view : w, .fn = fn, .ctx = ctx};
  if (s->last) s->last->next = w; else s->first = w;
  s->last = w;
}
void cd_unwatch(cd_watcher *w) {
  cd_state *s = w->of;
  if (!s) return;
  if (w->prev) w->prev->next = w->next; else s->first = w->next;
  if (w->next) w->next->prev = w->prev; else s->last = w->prev;
  if (w->slot) queue[w->slot - 1] = nullptr; /* a run still pending is dropped */
  if (recent == w) recent = nullptr;
  w->of = nullptr;
}
static void region_gone(void *w) { cd_unwatch(w); cd_free(w); }
void cd_watch(cd_state *s, cd_render_fn fn, void *ctx) {
  cd_watcher *w = cd_alloc(sizeof *w);
  cd_watcher *view = recent && recent->fn == fn && recent->ctx == ctx ? recent : nullptr;
  cd_watch_at(w, s, fn, ctx, view);
  if (!view) recent = w;
  cd_on_free(nullptr, region_gone, w); /* in cdom: the region's own list of views, no cleanup entry */
}
uint32_t cd_state_runs(void) { return runs; }

#ifdef CD_STATE_CHECK
void cd_read(const cd_state *s) {
  if (!running) return;
  for (const cd_watcher *w = s->first; w; w = w->next) if (w->view == running) return;
  __builtin_trap(); /* a view read a state it does not watch */
}
#endif
