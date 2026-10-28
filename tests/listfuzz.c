/* Keyed diff fuzz: random list transitions through the real shim; the page
 * checks DOM order and that every surviving key kept its node. */
#include "../src/cdom.h"

__attribute__((import_module("env"), import_name("check"))) void check(const uint32_t *keys, uint32_t n, uint32_t round);

static uint32_t rng = 12345;
static uint32_t rnd(uint32_t m) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng % m; }

static uint32_t keys[64], nkeys;
static const uint32_t *cur = keys; /* the key array create() reads */
static cd_list list;
static cd_str S_LI;

static cd_id create(void *ctx, uint32_t i, uint32_t *data) {
  (void)ctx;
  cd_id id = cd_id_new();
  cd_open(S_LI); cd_name(id); cd_text_num(cur[i]); cd_close();
  *data = 0;
  return id;
}
static void patch(void *ctx, uint32_t i, cd_item *it) { (void)ctx; (void)i; (void)it; }
static void destroy(void *ctx, cd_item *it) { (void)ctx; (void)it; }
static const cd_list_fns FNS = {0, create, patch, destroy};

static void next_keys(void) {
  uint32_t mode = rnd(6), used[64] = {0};
  uint32_t prev[64], np = nkeys;
  for (uint32_t i = 0; i < np; i++) prev[i] = keys[i];
  if (mode == 0) { /* fully random */
    nkeys = rnd(41);
    for (uint32_t i = 0; i < nkeys; i++) { uint32_t k; do k = rnd(64); while (used[k]); used[k] = 1; keys[i] = k; }
  } else { /* small edits of the previous list: swaps, moves, inserts, deletes */
    nkeys = np;
    for (uint32_t i = 0; i < nkeys; i++) used[keys[i]] = 1;
    for (uint32_t e = 1 + rnd(3); e-- > 0;) {
      uint32_t op = rnd(4);
      if (op == 0 && nkeys > 1) { uint32_t a = rnd(nkeys), b = rnd(nkeys), t = keys[a]; keys[a] = keys[b]; keys[b] = t; }
      else if (op == 1 && nkeys > 1) { uint32_t a = rnd(nkeys), b = rnd(nkeys), t = keys[a];
        if (a < b) for (uint32_t i = a; i < b; i++) keys[i] = keys[i + 1]; else for (uint32_t i = a; i > b; i--) keys[i] = keys[i - 1];
        keys[b] = t; }
      else if (op == 2 && nkeys < 40) { uint32_t k; do k = rnd(64); while (used[k]); used[k] = 1;
        uint32_t at = rnd(nkeys + 1); for (uint32_t i = nkeys; i > at; i--) keys[i] = keys[i - 1]; keys[at] = k; nkeys++; }
      else if (op == 3 && nkeys) { uint32_t at = rnd(nkeys); used[keys[at]] = 0;
        for (uint32_t i = at; i + 1 < nkeys; i++) keys[i] = keys[i + 1]; nkeys--; }
    }
  }
}

void cd_on_event(const cd_event_rec *e) { (void)e; }

void cd_main(void) {
  S_LI = cd_internz("li");
  cd_at(CD_MOUNT, 0);
  cd_open(cd_internz("ul"));
  list.parent = cd_id_new();
  list.owns_parent = 1;
  cd_name(list.parent);
  cd_close();
}

__attribute__((export_name("run_fuzz"))) void run_fuzz(uint32_t seed, uint32_t rounds) {
  rng = seed | 1;
  for (uint32_t round = 0; round < rounds; round++) {
    next_keys();
    cd_list_update(&list, keys, nkeys, &FNS);
    cd_flush();
    check(keys, nkeys, round);
  }
}

/* Strings: ASCII and not, in a tiny batch (char loop) and a large one (one
 * TextDecoder call sliced by UTF-16 length). The page reads them back. */
static const char *STRS[] = {"plain", "h\xc3\xa9llo", "\xe6\x97\xa5\xe6\x9c\xac", "\xf0\x9f\x98\x80x", ""};
__attribute__((export_name("run_strings"))) void run_strings(uint32_t big) {
  cd_at(CD_MOUNT, 0);
  cd_open(cd_internz("p"));
  for (uint32_t r = 0; r < (big ? 20 : 1); r++)
    for (uint32_t i = 0; i < 5; i++) {
      cd_open(cd_internz("b"));
      char buf[32]; uint32_t n = 0;
      for (const char *c = STRS[i]; *c; c++) buf[n++] = *c;
      buf[n++] = (char)('a' + r % 26);
      cd_text(cd_intern(buf, n));
      cd_close();
    }
  cd_close();
  cd_flush();
}

/* Freed ids travel as runs. Two lists take ids in a random interleave, so runs of one list's
 * ids sit next to the other list's live ids; after a list is cleared, every node of the other
 * list must still be reachable by id (a run that over-reaches would drop it and the class
 * write below would throw). */
__attribute__((export_name("run_freeruns"))) void run_freeruns(uint32_t seed, uint32_t rounds) {
  static cd_list L[2];
  static uint32_t K[2][64], nk[2];
  rng = seed | 1;
  cd_str S_UL = cd_internz("ul"), S_X = cd_internz("x");
  /* A flush closes the open run: after it, op 0 is a new op whose args must not be taken for
   * the run's (first id, count). Needs a freed id y right below a live id z. */
  cd_flush();
  cd_id ids[64], y = 0, z = 0, w;
  for (uint32_t i = 0; i < 64 && !z; i++) {
    ids[i] = cd_id_new();
    for (uint32_t k = 0; k < i && !z; k++)
      if (ids[k] + 1 == ids[i]) { y = ids[k]; z = ids[i]; } else if (ids[i] + 1 == ids[k]) { y = ids[i]; z = ids[k]; }
  }
  w = cd_id_new();
  cd_at(CD_MOUNT, 0);
  cd_open(S_LI); cd_name(z); cd_close();
  cd_flush();
  cd_id_free(w);         /* op 0: a run (w, 1) */
  cd_flush();
  cd_set_class(z, S_X);  /* op 0 again: (z, S_X) */
  cd_id_free(y);         /* must be its own run, not an edit of op 0 (y names no node) */
  cd_set_class(z, S_X);
  cd_flush();
  for (int j = 0; j < 2; j++) {
    cd_at(CD_MOUNT, 0);
    cd_open(S_UL); L[j].parent = cd_id_new(); L[j].owns_parent = 1; cd_name(L[j].parent); cd_close();
  }
  for (uint32_t round = 0; round < rounds; round++) {
    /* grow both lists a few items at a time, alternating, so their ids interleave in runs */
    for (uint32_t step = rnd(8); step-- > 0;) {
      int j = (int)rnd(2);
      for (uint32_t a = 1 + rnd(4); a-- > 0 && nk[j] < 60;) { K[j][nk[j]] = round * 64 + nk[j]; nk[j]++; }
      cur = K[j];
      cd_list_update(&L[j], K[j], nk[j], &FNS);
    }
    int c = (int)rnd(2); /* clear one list: its ids are freed as runs */
    nk[c] = 0;
    cur = K[c];
    cd_list_update(&L[c], K[c], 0, &FNS);
    for (uint32_t i = 0; i < L[1 - c].n; i++) cd_set_class(L[1 - c].items[i].node, S_X);
    cd_flush();
  }
  for (int j = 0; j < 2; j++) { nk[j] = 0; cd_list_update(&L[j], K[j], 0, &FNS); }
  cd_flush();
}

/* Walks cross as one call, split every 16 steps, and must land before any op that reads the
 * node they reach: a class after a walk, a walk from the last node, a 20-step walk. The page
 * checks where each class landed. */
__attribute__((export_name("run_walks"))) void run_walks(void) {
  cd_str S_DIV = cd_internz("div"), S_I = cd_internz("i"), S_W = cd_internz("w");
  cd_id root = cd_id_new();
  cd_at(CD_MOUNT, 0);
  cd_open(S_DIV); cd_name(root);
  for (int d = 0; d < 20; d++) cd_open(S_DIV);
  cd_text(cd_internz("deep"));
  cd_up(); cd_class(cd_internz("w20"));          /* from the last node (the text) up to its div */
  for (int d = 0; d < 20; d++) cd_close();
  cd_open(S_I); cd_text(cd_internz("a")); cd_close();
  cd_open(S_I); cd_open(S_I); cd_text(cd_internz("b")); cd_close(); cd_close();
  cd_close();
  cd_sel(root); cd_down(); cd_next(); cd_next(); cd_down(); cd_class(S_W);   /* the inner i */
  cd_sel(root); for (int d = 0; d < 19; d++) cd_down(); cd_class(cd_internz("w19")); /* 19 steps: one split */
    cd_flush();
}
