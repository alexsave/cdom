/* cd_job_cb and cd_job_file_cb (cdom_job.h), driven by tools/job.mjs through tests/job.html. Every answer
 * lands in a record the page reads. The job is tests/shrink_job.c (kind 0: shrink_job.wasm; kind 1:
 * shrink_job_cap.wasm, built with the heap capped at 100 pages, 6.4 MB, for NO_ROOM). */
#include "../src/cdom.h"
#include "../src/cdom_job.h"
#define EXPORT(n) __attribute__((export_name(n)))

static cd_id pick;
void cd_main(void) {
    cd_open("input");
    cd_attr("type", "file");
    cd_attr("id", "pick");
    cd_attr("multiple", "");
    cd_name_el(pick = cd_id_new());
    cd_close();
}
EXPORT("pick") cd_id pick_id(void) { return pick; }

typedef struct {
    int32_t status;
    uint32_t calls, len, in_len, hash, mb, seed, sum, tail;
} record;
#define RECORDS 16
static record rec[RECORDS];
EXPORT("rec") uint32_t rec_at(uint32_t k, uint32_t f) {
    const uint32_t *w = (const uint32_t *)&rec[k];
    return w[f];
}
static void land(void *ctx, uint32_t status, const uint8_t *p, uint32_t len) {
    record *r = ctx;
    r->status = (int32_t)status;
    r->calls++;
    r->len = len;
    if (status != CD_JOB_OK) return;
    const uint32_t *w = (const uint32_t *)p;
    r->in_len = w[0], r->hash = w[1], r->mb = w[2], r->seed = w[3], r->sum = w[4];
    r->tail = len == 65536;
    for (uint32_t i = 20; i < len; i++) r->tail &= p[i] == (uint8_t)(i * 7 + r->seed);
}
EXPORT("reset") void reset(void) {
    for (uint32_t k = 0; k < RECORDS; k++) rec[k] = (record){.status = -1};
}
static const char *const NAME[2] = {"shrink_job.wasm", "shrink_job_cap.wasm"};
/* The input and the args are freed (and overwritten) right after the call: the job must not read them later. */
EXPORT("ask") void ask(uint32_t k, uint32_t kind, uint32_t mb, uint32_t passes, uint32_t mode, uint32_t seed, uint32_t in_len) {
    uint8_t *in = cd_alloc(in_len + 1);
    for (uint32_t i = 0; i < in_len; i++) in[i] = (uint8_t)(i * 31 + 3);
    uint32_t *args = cd_alloc(16);
    args[0] = mb, args[1] = passes, args[2] = mode, args[3] = seed;
    cd_job_cb(NAME[kind], in, in_len, args, 16, land, &rec[k]);
    __builtin_memset(in, 0xEE, in_len);
    __builtin_memset(args, 0xEE, 16);
    cd_free(in);
    cd_free(args);
}
EXPORT("ask_file") void ask_file(uint32_t k, uint32_t kind, uint32_t index, uint32_t mb, uint32_t seed) {
    uint32_t args[4] = {mb, 0, 0, seed};
    cd_job_file_cb(NAME[kind], pick, index, args, 16, land, &rec[k]);
}
EXPORT("pages") uint32_t pages(void) { return (uint32_t)__builtin_wasm_memory_size(0); }
