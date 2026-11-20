/* The image job of tools/image.mjs (the chunked-read design's test 13) and of bench/chunked: a job module that links
 * the image module (tests/image/wasm.mk, APP_JOB_imgjob_MODULES := image). Its args are the box, two little-endian
 * 16-bit sides, and a fifth byte: 1 to answer, on OK, the job's memory at the decode's end too (the pixels, then 12
 * bytes: its pages, w and h; the copy that makes that block is not in the pages). It answers the pixels (w x h x 4
 * bytes) on OK, else the image status as 1 byte (no image is 1 byte long); NO_ROOM is the job's.
 * run: the whole file, which the worker read into the job's memory (cd_job_file_cb), resized; the pieces_* entries: the
 * file fed a piece at a time (cd_job_pieces_cb), the design's few lines (section 5). */
#include "cdom.h"
#include "cdom_image.h"
#include "cdom_job_mod.h"

static uint32_t rd16(const uint8_t *a) { return (uint32_t)a[0] | (uint32_t)a[1] << 8; }
static uint32_t report;
static uint32_t answer(uint32_t st, uint8_t *rgba, uint32_t w, uint32_t h) {
  if (st == CD_IMAGE_NO_ROOM) return CD_JOB_RUN_NO_ROOM;
  uint32_t mem[3] = {(uint32_t)__builtin_wasm_memory_size(0), w, h}, n = w * h * 4;
  uint8_t s = (uint8_t)st, *both = st == CD_IMAGE_OK && report ? cd_alloc_try(n + 12) : 0;
  if (both) { __builtin_memcpy(both, rgba, n); __builtin_memcpy(both + n, mem, 12); cd_job_answer(both, n + 12); cd_free(both); }
  else if (st == CD_IMAGE_OK && report) return CD_JOB_RUN_NO_ROOM;
  else if (st == CD_IMAGE_OK) cd_job_answer(rgba, n);   /* the app's callback gets the pixels */
  else cd_job_answer(&s, 1);
  cd_free(rgba);
  return CD_JOB_RUN_OK;
}

CD_JOB_ENTRY uint32_t run(const uint8_t *in, uint32_t in_len, const uint8_t *args, uint32_t args_len) {
  uint8_t *rgba;
  uint32_t w, h;
  if (args_len < 4) return 2;
  report = args_len > 4 && args[4] == 1;
  return answer(cd_image_resize(in, in_len, rd16(args), rd16(args + 2), &rgba, &w, &h), rgba, w, h);
}

static cd_image_dec *d;   /* the job's own state: the instance is used once */
static uint32_t refused;  /* cd_image_begin's status when it refused (BAD_BOX): answered at the first put */
uint32_t pieces_begin(uint32_t size, const uint8_t *args, uint32_t args_len) {
  if (args_len < 4) return 2;
  report = args_len > 4 && args[4] == 1;
  refused = cd_image_begin(size, rd16(args), rd16(args + 2), &d);
  return refused == CD_IMAGE_NO_ROOM ? CD_JOB_RUN_NO_ROOM : 0;
}
uint8_t *pieces_room(void) { uint32_t m; return d ? cd_image_room(d, &m) : 0; }
uint32_t pieces_max(void) { uint32_t m = 0; if (d) cd_image_room(d, &m); return m; }
uint32_t pieces_put(uint32_t n) {
  if (!d) return answer(refused, 0, 0, 0);
  uint32_t st = cd_image_put(d, n);
  if (st == CD_IMAGE_MORE) return CD_JOB_PIECES_MORE;
  uint8_t *rgba;
  uint32_t w, h;
  cd_image_take(d, &rgba, &w, &h);
  return answer(st, rgba, w, h);   /* the job's memory goes with the worker: no cd_image_end needed, but no harm */
}
