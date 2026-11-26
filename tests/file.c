/* cd_file_read (cdom_file.h), driven by tools/file.mjs through tests/file.html: the harness
 * picks files from disk into the input (CDP DOM.setFileInputFiles) and asks through these exports; every
 * answer lands in a record the page reads. Built twice: file.wasm, and file_cap.wasm with the heap capped
 * (-DCDOM_HEAP_PAGES_MAX=200, 12.5 MB) for NO_ROOM. */
#include "../src/cdom.h"
#include "../src/cdom_file.h"
#define EXPORT(n) __attribute__((export_name(n)))

static cd_id pick, plain;
void cd_main(void) {
    cd_open("input");
    cd_attr("type", "file");
    cd_attr("id", "pick");
    cd_attr("multiple", "");
    cd_name_el(pick = cd_id_new());
    cd_close();
    cd_open("div"); /* a node with no files list */
    cd_name_el(plain = cd_id_new());
    cd_close();
}
EXPORT("pick") cd_id pick_id(void) { return pick; }
EXPORT("plain") cd_id plain_id(void) { return plain; }
EXPORT("size_of") double size_of(uint32_t i) {
    char path[24] = "files.", digits[10]; /* no stdio.h: tests/build.sh builds this without libc headers too */
    uint32_t n = 6, d = 0;
    do digits[d++] = (char)('0' + i % 10); while (i /= 10);
    while (d) path[n++] = digits[--d];
    for (const char *t = ".size"; *t; t++) path[n++] = *t;
    return cd_get_num(pick, cd_tmp(path, n));
}
EXPORT("pages") uint32_t pages(void) { return (uint32_t)__builtin_wasm_memory_size(0); }
/* 20 MB of the app's own, kept: after two 20 MB answers it must fit in their room. */
EXPORT("take20") void take20(void) { __builtin_memset(cd_alloc(20000000), 1, 20000000); }

/* One record per answer slot k: what came, how often, and a copy of up to 64 KB of the bytes (copied in
 * the callback: the bytes are valid only until it returns). pattern: every byte was data.bin's, i * 131 + 7. */
typedef struct {
    int32_t status;
    uint32_t calls, len, pattern;
    uint8_t *keep;
} record;
#define RECORDS 32
static record rec[RECORDS];
/* Each answer finds no walk pending (the export before it flushed) and leaves one (cd_sel then cd_up), for the
 * export to flush: stale counts answers that found one. */
static uint32_t stale;
EXPORT("stale") uint32_t stale_n(void) { return stale + (cd_pending() != 0); }
static void land(uint32_t k, uint32_t status, const uint8_t *p, uint32_t len) {
    stale += cd_pending() != 0;
    cd_sel(plain);
    cd_up();
    record *r = &rec[k];
    r->status = (int32_t)status;
    r->calls++;
    r->len = len;
    r->pattern = p != 0;
    for (uint32_t i = 0; p && i < len; i++) r->pattern &= p[i] == (uint8_t)(i * 131 + 7);
    cd_free(r->keep);
    r->keep = 0;
    if (p && len <= 65536) {
        r->keep = cd_alloc(len);
        __builtin_memcpy(r->keep, p, len);
    }
}
EXPORT("rec") int32_t rec_field(uint32_t k, uint32_t f) {
    const record *r = &rec[k];
    uint32_t v[] = {(uint32_t)r->status, r->calls, r->len, r->pattern, (uint32_t)(uintptr_t)r->keep};
    return (int32_t)v[f];
}
EXPORT("rec_clear") void rec_clear(uint32_t k) { rec[k].status = -1; }

static void read_answer(void *ctx, uint32_t status, const uint8_t *p, uint32_t len) {
    land((uint32_t)(uintptr_t)ctx, status, p, len);
}
/* The _cb forms, answered into record k. */
EXPORT("read") void read_cb(cd_id input, uint32_t index, uint32_t max_bytes, uint32_t k) {
    rec[k].status = -1;
    cd_file_read_cb(input, index, max_bytes, read_answer, (void *)(uintptr_t)k);
}
/* The token forms: the token is the record, through the hooks. */
EXPORT("read_tok") void read_tok(uint32_t index, uint32_t max_bytes, uint32_t k) {
    rec[k].status = -1;
    cd_file_read(pick, index, max_bytes, k);
}
void cd_on_file(uint32_t token, uint32_t status, const uint8_t *p, uint32_t len) { land(token, status, p, len); }
/* What must trap: a token in cdom's range. */
EXPORT("bad") void bad(void) { cd_file_read(pick, 0, 10, CD_TOKEN_CDOM | 3); }

/* A request slot (cdom.h): two reads of one slot, the second begun before the first answered;
 * only the current one lands, in record 30 + its index, and the dropped ones are counted. */
static cd_req slot;
static uint32_t dropped;
static void stale_read(void *ctx, uint32_t status, const uint8_t *p, uint32_t len) {
    uint16_t i;
    if (!cd_req_current(&slot, ctx, &i)) { dropped++; return; }
    cd_req_end(&slot, status == CD_FILE_OK);
    land(i, status, p, len);
}
EXPORT("stale_reads") void stale_reads(uint32_t first, uint32_t second, uint32_t k1, uint32_t k2) {
    rec[k1].status = rec[k2].status = -1;
    cd_file_read_cb(pick, first, 1u << 30, stale_read, cd_req_begin(&slot, (uint16_t)k1));
    cd_file_read_cb(pick, second, 1u << 30, stale_read, cd_req_begin(&slot, (uint16_t)k2));
}
EXPORT("dropped") uint32_t dropped_n(void) { return dropped; }

/* ---- the reader in order (cd_file_open, cd_file_next_cb, cd_file_close), tools/file.mjs's test 10 ----
 * One reader at a time, its pieces checked as they come against the patterned file ((i * 131 + 7) & 255 at offset
 * i), into a 1 MiB room with 64 canary bytes on each side. Each answer also reads window.EPOCH, which the page moves
 * on once per task: two pieces in one task (an answer chained as a microtask) show as the same epoch. */
#include "../src/cdom_cb.h"
#define RD_ROOM (1u << 20)
#define CANARY 64u
static uint8_t *rd_mem;
static struct {
    int32_t status;             /* -1 while reading; the last status (100: stopped at the limit) */
    uint32_t reader, size, at, pieces, ends, bad, max, limit, stepped, after, same, canary;
    double epoch;
} R;
static uint8_t *rd_dst(void) { return rd_mem + CANARY; }
static int canaries_ok(void) {
    for (uint32_t i = 0; i < CANARY; i++) if (rd_mem[i] != 0xc5 || rd_mem[CANARY + RD_ROOM + i] != 0xc5) return 0;
    return 1;
}
static void rd_got(void *ctx, uint32_t status, uint32_t n);
static void rd_next(void) { cd_file_next_cb(R.reader, rd_dst(), R.max, rd_got, 0); }
static void rd_got(void *ctx, uint32_t status, uint32_t n) {
    (void)ctx;
    double e = cd_get_num(CD_WINDOW, cd_tmp("EPOCH", 5));
    if (R.pieces && e == R.epoch) R.same++;
    R.epoch = e;
    if (R.status != -1) { R.after++; return; }
    R.canary &= (uint32_t)canaries_ok();
    if (status != CD_FILE_OK || !n) {
        if (status == CD_FILE_OK) R.ends++;
        R.status = (int32_t)status;
        cd_file_close(R.reader);
        return;
    }
    if (n > R.max) R.bad++;
    const uint8_t *d = rd_dst();
    for (uint32_t i = 0; i < n; i++) R.bad += d[i] != (uint8_t)((R.at + i) * 131 + 7);
    R.at += n;
    R.pieces++;
    if (R.limit && R.at >= R.limit) { R.status = 100; cd_file_close(R.reader); return; }
    if (!R.stepped) rd_next();
}
EXPORT("rd_open") int32_t rd_open(uint32_t index) {
    if (!rd_mem) rd_mem = cd_alloc(RD_ROOM + 2 * CANARY);
    __builtin_memset(rd_mem, 0xc5, RD_ROOM + 2 * CANARY);
    __builtin_memset(&R, 0, sizeof R);
    R.status = -1; R.canary = 1;
    return (int32_t)cd_file_open(pick, index, &R.reader, &R.size);
}
EXPORT("rd_start") void rd_start(uint32_t max, uint32_t limit, uint32_t stepped) {
    R.max = max; R.limit = limit; R.stepped = stepped;
    rd_next();
}
EXPORT("rd_more") void rd_more(void) { if (R.status == -1) rd_next(); }
EXPORT("rd") uint32_t rd_field(uint32_t f) {
    uint32_t v[] = {(uint32_t)R.status, R.reader, R.size, R.at, R.pieces, R.ends, R.bad, R.after, R.same, R.canary};
    return v[f];
}
/* A second next while one is pending: traps. */
EXPORT("rd_double") void rd_double(void) { rd_next(); rd_next(); }
/* A piece pending, then the reader closed: the room is filled with 0x5a first, and must stay so (nothing written). */
EXPORT("rd_close_pending") void rd_close_pending(void) {
    __builtin_memset(rd_dst(), 0x5a, RD_ROOM);
    rd_next();
    cd_file_close(R.reader);
}
EXPORT("rd_untouched") uint32_t rd_untouched(void) {
    const uint8_t *d = rd_dst();
    for (uint32_t i = 0; i < RD_ROOM; i++) if (d[i] != 0x5a) return 0;
    return (uint32_t)canaries_ok();
}
/* The callback slot a new callback would get now (cdom_cb.h): the same before a next and after its reader was closed,
 * or the close kept the slot. */
EXPORT("slot_probe") uint32_t slot_probe(void) {
    uint32_t t = cd_cb_new(0, 0);
    cd_cb_drop(t);
    return t & 0xffff;
}
