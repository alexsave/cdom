/* cdom_image_file: cd_image_file_cb and cd_image_file_stop (cdom_image_file.h). The decode's state is one small
 * block; the callback slot of its token (cdom_cb.h) holds the app's fn and that block, so the token is how
 * cd_image_file_stop finds it, and once the slot is taken (by the answer or a stop) the token names nothing. */
#include "cdom.h"
#include "cdom_cb.h"
#include "cdom_file.h"
#include "cdom_image.h"
#include "cdom_image_file.h"

typedef struct {
  cd_image_dec *d;
  void *ctx;           /* the app's */
  uint32_t token;      /* this decode's: its slot holds the app's fn and this block */
  uint32_t reader;     /* cd_file_open's, or 0 */
  uint32_t timer;      /* a pending cd_timer_cb (a step that needs no bytes, or an answer known at once), or 0 */
  uint32_t status;     /* the answer a timer gives */
} job;

/* What the decode holds, freed: the timer, the reader (and its pending piece), the decoder, the block. */
static void drop(job *j) {
  if (j->timer) cd_timer_stop(j->timer);
  if (j->reader) cd_file_close(j->reader);
  cd_image_end(j->d);
  cd_free(j);
}

/* The answer: the image taken, everything freed, then fn, unless a stop took the slot first. */
static void finish(job *j, uint32_t st) {
  uint8_t *rgba = 0;
  uint32_t w = 0, h = 0;
  if (st == CD_IMAGE_OK) cd_image_take(j->d, &rgba, &w, &h);
  j->timer = 0;
  cd_cb_fn fn;
  void *v, *ctx = j->ctx;
  int live = cd_cb_take(j->token, &fn, &v);
  drop(j);
  if (live) ((cd_image_file_fn)fn)(ctx, st, rgba, w, h);
  else cd_free(rgba);
}

static void step(void *v, uint32_t st, uint32_t n);
static void again(void *v);
/* The next piece into the room the decoder names, or, when it has work that needs no bytes, that work after a yield. */
static void next(job *j) {
  uint32_t max;
  uint8_t *at = cd_image_room(j->d, &max);
  if (max) cd_file_next_cb(j->reader, at, max, step, j);
  else j->timer = cd_timer_cb(0, again, j);
}
/* A piece came. n 0 while the decoder wants bytes (it never asks past the file's size): the file ended early. */
static void step(void *v, uint32_t st, uint32_t n) {
  job *j = v;
  if (st != CD_FILE_OK || !n) { finish(j, CD_IMAGE_UNREADABLE); return; }
  uint32_t s = cd_image_put(j->d, n);
  if (s != CD_IMAGE_MORE) { finish(j, s); return; }
  next(j);
}
static void again(void *v) {
  job *j = v;
  j->timer = 0;
  uint32_t s = cd_image_put(j->d, 0);
  if (s != CD_IMAGE_MORE) { finish(j, s); return; }
  next(j);
}
static void late(void *v) {
  job *j = v;
  finish(j, j->status);
}

void cd_image_file_cb(cd_id input, uint32_t index, uint32_t max_w, uint32_t max_h, cd_image_file_fn fn, void *ctx,
                      uint32_t *id) {
  job *j = cd_alloc(sizeof *j);
  j->d = 0; j->ctx = ctx; j->reader = 0; j->timer = 0; j->status = CD_IMAGE_OK;
  *id = j->token = cd_cb_new((cd_cb_fn)fn, j);
  uint32_t size, st;
  if (cd_file_open(input, index, &j->reader, &size) != CD_FILE_OK) { j->reader = 0; st = CD_IMAGE_UNREADABLE; }
  else st = cd_image_begin(size, max_w, max_h, &j->d);
  if (st != CD_IMAGE_OK) {                        /* answered from the event loop all the same */
    j->status = st;
    j->timer = cd_timer_cb(0, late, j);
    return;
  }
  next(j);
}

void cd_image_file_stop(uint32_t id) {
  cd_cb_fn fn;
  void *v;
  if (cd_cb_take(id, &fn, &v)) drop(v);
}
