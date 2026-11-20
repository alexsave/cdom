/* The page of docs/job-worker.md's measurements (tools/shrink.mjs): M0's meter check (touch, grow_only), M1's cost today
 * (local: the job's work in the app's own memory, then freed) and M3's cd_job (job: the same work in a worker). */
#include "../src/cdom.h"
#include "../src/cdom_job.h"
#include "shrink_work.h"
#define EXPORT(n) __attribute__((export_name(n)))

void cd_main(void) { }
static uint8_t *kept;
/* M0: mb megabytes written and kept; and memory grown by mb megabytes and never written. */
EXPORT("touch") void touch(uint32_t mb) {
  kept = cd_alloc(mb << 20);
  __builtin_memset(kept, 1, mb << 20);
}
EXPORT("grow_only") void grow_only(uint32_t mb) { __builtin_wasm_memory_grow(0, mb * 16u); }
EXPORT("release") void release(void) { cd_free(kept); kept = 0; }
/* The wall time of the last local or job call, in milliseconds as cd_now counts them: from the call to the answer. */
static double wall0, wall;
static uint32_t answered, answer_status, answer_len;
EXPORT("wall") double wall_ms(void) { return wall; }
EXPORT("answered") uint32_t answered_n(void) { return answered; }
EXPORT("status") uint32_t status_of(void) { return answer_status; }
EXPORT("len") uint32_t len_of(void) { return answer_len; }
/* "nothing": the work in the app's memory, on the main thread, then freed and 64 KB answered as the job would. */
EXPORT("local") void local(uint32_t mb, uint32_t passes) {
  wall0 = cd_now();
  uint8_t *m = cd_alloc(mb << 20);
  uint32_t sum = shrink_work(m, mb << 20, passes, 1);
  uint8_t *out = cd_alloc(65536);
  __builtin_memset(out, (int)sum, 65536);
  cd_free(out);
  cd_free(m);
  wall = cd_now() - wall0;
  answer_status = 0, answer_len = 65536;
  answered++;
}
static void land(void *, uint32_t status, const uint8_t *, uint32_t len) {
  wall = cd_now() - wall0;
  answer_status = status, answer_len = len;
  answered++;
}
/* B-worker: the same work in a throwaway worker. */
EXPORT("job") void job(uint32_t mb, uint32_t passes) {
  uint32_t args[4] = {mb, passes, 0, 1};
  wall0 = cd_now();
  cd_job_cb("shrink_job.wasm", 0, 0, args, 16, land, 0);
}
