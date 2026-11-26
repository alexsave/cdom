/* The synthetic job of docs/job-worker.md (and the cases of tools/job.mjs): touch N MB of its own memory and
 * answer 64 KB. A job module (src/cdom_job_mod.h), built as cdom.mk builds one (-DCDOM_HEAP_ONLY).
 * args: four little-endian u32: mb (megabytes to touch), passes (extra passes over them, which burn time), mode
 * (1: trap), seed. The answer is 65,536 bytes: in_len, an FNV-1a hash of the input, mb, seed, and (when passes is 0) the
 * sum of the touched bytes, each a u32 at offsets 0, 4, 8, 12, 16; then byte i is (i * 7 + seed) & 255 from 20 on. */
#include "../src/cdom_job_mod.h"
#include "shrink_work.h"
#include <string.h>

CD_JOB_ENTRY uint32_t run(const uint8_t *in, uint32_t in_len, const uint8_t *args, uint32_t args_len) {
    uint32_t a[4] = {0, 0, 0, 0}; /* mb, passes, mode, seed */
    memcpy(a, args, args_len < sizeof a ? args_len : sizeof a);
    if (a[2] == 1) __builtin_trap();
    uint32_t n = a[0] << 20;
    uint8_t *m = cd_alloc_try(n);
    uint8_t *out = m ? cd_alloc_try(65536) : 0;
    if (!out) return CD_JOB_RUN_NO_ROOM;
    uint32_t sum = shrink_work(m, n, a[1], a[3]);
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < in_len; i++) h = (h ^ in[i]) * 16777619u;
    uint32_t head[5] = {in_len, h, a[0], a[3], sum};
    memcpy(out, head, sizeof head);
    for (uint32_t i = 20; i < 65536; i++) out[i] = (uint8_t)(i * 7 + a[3]);
    cd_job_answer(out, 65536);
    memset(out, 0xEE, 65536);   /* the answer was read in the call: the block may be reused at once (cdom_job_mod.h) */
    cd_free(out);
    return CD_JOB_RUN_OK;
}
