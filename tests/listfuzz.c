/* Keyed diff fuzz: random list transitions through the real shim; the page
 * checks DOM order and that every surviving key kept its node. */
#include "../src/cdom.h"

__attribute__((import_module("env"), import_name("check"))) void check(const uint32_t *keys, uint32_t n, uint32_t round);

static uint32_t rng = 12345;
static uint32_t rnd(uint32_t m) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng % m; }

static uint32_t keys[64], nkeys;
static cd_list list;
static cd_str S_LI;

static cd_id create(void *ctx, uint32_t i, uint32_t *data) {
  (void)ctx;
  cd_id id = cd_id_new();
  cd_open(S_LI); cd_name(id); cd_text_num(keys[i]); cd_close();
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
