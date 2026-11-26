/* The work of the synthetic job (docs/job-worker.md), one copy, used by the job module (tests/shrink_job.c) and by the app
 * that runs it in its own memory for the "nothing" arm (tests/shrink.c): touch n bytes, then `passes` passes of dependent
 * writes over them, which burn time. Returns the sum of the touched bytes when passes is 0, else 0. */
#include <stdint.h>
static inline uint32_t shrink_work(uint8_t *m, uint32_t n, uint32_t passes, uint32_t seed) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        m[i] = (uint8_t)(i * 131 + seed);
        sum += m[i];
    }
    for (uint32_t p = 0; p < passes; p++) {
        uint8_t carry = (uint8_t)p;
        for (uint32_t i = 0; i < n; i++) m[i] = carry = (uint8_t)(m[i] + carry * 5 + 1);
        sum += m[n / 2]; /* keeps the passes from being dropped */
    }
    return passes ? 0 : sum;
}
