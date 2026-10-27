/* Step 0: ns per call across the wasm/JS boundary. */
#include <stdint.h>
#define IMPORT(n) __attribute__((import_module("env"), import_name(n)))
#define EXPORT(n) __attribute__((export_name(n)))

IMPORT("nop") void nop(void);
IMPORT("set_attr") void set_attr(uint32_t node, uint32_t val);
IMPORT("set_attr_utf8") void set_attr_utf8(uint32_t node, const char *p, uint32_t len);
IMPORT("set_attr_ascii") void set_attr_ascii(uint32_t node, const char *p, uint32_t len);
IMPORT("set_text") void set_text(uint32_t node, uint32_t val);
IMPORT("apply") void apply(uint32_t n);

static const char *vals[8] = {"alpha", "bravo", "charlie", "delta", "echo", "foxtrot", "golf", "hotel"};
static const uint32_t lens[8] = {5, 5, 7, 5, 4, 7, 4, 5};
static uint32_t ops[1 << 20];

EXPORT("ops_ptr") uint32_t *ops_ptr(void) { return ops; }
EXPORT("loop_nop") void loop_nop(uint32_t n) { for (uint32_t i = 0; i < n; i++) nop(); }
EXPORT("loop_attr") void loop_attr(uint32_t n) { for (uint32_t i = 0; i < n; i++) set_attr(1, i & 7); }
EXPORT("loop_attr_utf8") void loop_attr_utf8(uint32_t n) {
  for (uint32_t i = 0; i < n; i++) set_attr_utf8(1, vals[i & 7], lens[i & 7]);
}
EXPORT("loop_attr_ascii") void loop_attr_ascii(uint32_t n) {
  for (uint32_t i = 0; i < n; i++) set_attr_ascii(1, vals[i & 7], lens[i & 7]);
}
EXPORT("loop_text") void loop_text(uint32_t n) { for (uint32_t i = 0; i < n; i++) set_text(1, i & 7); }
/* The batched shape: write (node, val) records, one call applies each 4096. */
EXPORT("batch_attr") void batch_attr(uint32_t n) {
  for (uint32_t done = 0; done < n;) {
    uint32_t c = n - done < 4096 ? n - done : 4096;
    for (uint32_t i = 0; i < c; i++) { ops[2 * i] = 1; ops[2 * i + 1] = (done + i) & 7; }
    apply(c);
    done += c;
  }
}
EXPORT("batch_empty") void batch_empty(uint32_t n) {
  for (uint32_t done = 0; done < n;) {
    uint32_t c = n - done < 4096 ? n - done : 4096;
    for (uint32_t i = 0; i < c; i++) { ops[2 * i] = 1; ops[2 * i + 1] = (done + i) & 7; }
    apply(0);
    done += c;
  }
}
