/* The JS 1 transform's CPU test (docs/js1-transform-design.md): a loop of integer operations and a recursion that
 * stays a recursion, timed in C with cd_now. tests/js1cpu.html shows it on the frames tier, where its buttons run each
 * and draw the answer and the milliseconds; tools/js1.mjs runs the exports against WebAssembly, and tools/frames.mjs
 * (FRAMES_MEASURE=1) times them in the transformed file and in the IE 9 build. */
#include "../src/cdom.h"

#define EXPORT(n) __attribute__((export_name(n)))

static uint32_t buf[256];
static uint32_t depth, deepest;
static volatile uint32_t sink[64];

/* n rounds of a multiply, shifts, xors, a load and a store: the transform's arithmetic and memory, no calls. */
EXPORT("loop") uint32_t loop(uint32_t n) {
  uint32_t x = 1;
  for (uint32_t i = 0; i < n; i++) {
    x = x * 2654435761u + i;
    x ^= x >> 15;
    buf[i & 255] += x;
    x += buf[(i * 7) & 255] << 3;
  }
  return x;
}

/* A recursion d deep that the compiler cannot make a loop: each level stores after its call. depth and deepest count
 * the C frames. */
__attribute__((noinline)) static uint32_t rec(uint32_t d) {
  if (++depth > deepest) deepest = depth;
  uint32_t r = 1;
  if (d) {
    r = rec(d - 1);
    sink[d & 63] = r;
    r = r * 3 + sink[(d * 5) & 63] + d;
  }
  depth--;
  return r;
}
EXPORT("rec") uint32_t rec_export(uint32_t d) { return rec(d); }
/* reps runs of a d-deep recursion. */
EXPORT("recs") uint32_t recs(uint32_t reps, uint32_t d) {
  uint32_t x = 0;
  for (uint32_t i = 0; i < reps; i++) x += rec(d);
  return x;
}
EXPORT("deepest") uint32_t deepest_export(void) { return deepest; }

enum { LOOP, LOOPV, LOOPMS, RECS, RECSV, RECSMS, DEPTH, DEPTHF, DEPTHV, NIDS };
static cd_id ids[NIDS];
static char io[32];

static cd_id button(const char *idattr, const char *label) {
  cd_open("button");
  cd_attr("id", idattr);
  cd_text(label);
  cd_id id = cd_id_new();
  cd_name_el(id);
  cd_close();
  return id;
}
static cd_id named_num(void) {
  cd_text_num(0);
  cd_id id = cd_id_new();
  cd_name_text(id);
  return id;
}

void cd_main(void) {
  cd_open("h1"); cd_text("js1cpu"); cd_close();
  cd_open("p"); ids[LOOP] = button("loop", "loop 100000"); cd_text(" answer "); ids[LOOPV] = named_num();
  cd_text(" ms "); ids[LOOPMS] = named_num(); cd_close();
  cd_open("p"); ids[RECS] = button("recs", "1000 recursions 20 deep"); cd_text(" answer "); ids[RECSV] = named_num();
  cd_text(" ms "); ids[RECSMS] = named_num(); cd_close();
  /* A depth to try: a browser whose stack is too short for it stops with its own error. */
  cd_open("p"); cd_text("depth ");
  cd_open("input"); cd_attr("id", "depth"); cd_attr("value", "50"); ids[DEPTHF] = cd_id_new(); cd_name_el(ids[DEPTHF]); cd_close();
  ids[DEPTH] = button("deep", "recurse"); cd_text(" deepest "); ids[DEPTHV] = named_num(); cd_close();
  cd_sel(ids[LOOP]); cd_listen(ids[LOOP], CD_EV_CLICK);
  cd_sel(ids[RECS]); cd_listen(ids[RECS], CD_EV_CLICK);
  cd_sel(ids[DEPTH]); cd_listen(ids[DEPTH], CD_EV_CLICK);
}

void cd_on_event(const cd_event_rec *e) {
  if (e->type != CD_EV_CLICK) return;
  cd_id id = (cd_id)e->id;
  double t0 = cd_now();
  if (id == ids[LOOP]) {
    cd_set_text_node_num(ids[LOOPV], loop(100000));
    cd_set_text_node_num(ids[LOOPMS], (uint32_t)(cd_now() - t0));
  } else if (id == ids[RECS]) {
    cd_set_text_node_num(ids[RECSV], recs(1000, 20));
    cd_set_text_node_num(ids[RECSMS], (uint32_t)(cd_now() - t0));
  } else if (id == ids[DEPTH]) {
    uint32_t n = cd_get_str(ids[DEPTHF], "value", io, sizeof io - 1), d = 0;
    for (uint32_t i = 0; i < n && io[i] >= '0' && io[i] <= '9'; i++) d = d * 10 + (uint32_t)(io[i] - '0');
    rec(d);
    cd_set_text_node_num(ids[DEPTHV], deepest);
  }
}
