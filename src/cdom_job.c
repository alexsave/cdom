/* cdom_job: the job import and the exports that answer it (cdom_job.h). Separate from cdom.c so an app that
 * runs no jobs does not carry them. */
#include "cdom.h"
#include "cdom_job.h"
#include "cdom_cb.h"

#define IMPORT(n) __attribute__((import_module("cdom"), import_name(n)))
#define EXPORT(n) __attribute__((export_name(n)))

/* file 0: (a, b) the input's pointer and length in C memory; file 1: the node id and the file's index. */
IMPORT("job_run") void j_job_run(uint32_t token, const char *name, uint32_t name_len, const void *args, uint32_t args_len,
                                 uint32_t a, uint32_t b, uint32_t file);

void cd_job_cb(const char *name, const void *in, uint32_t in_len, const void *args, uint32_t args_len,
               cd_job_fn fn, void *ctx) {
  j_job_run(cd_cb_new((cd_cb_fn)fn, ctx), name, (uint32_t)__builtin_strlen(name), args, args_len, (uint32_t)(uintptr_t)in, in_len, 0);
}
void cd_job_file_cb(const char *name, cd_id input, uint32_t index, const void *args, uint32_t args_len,
                    cd_job_fn fn, void *ctx) {
  j_job_run(cd_cb_new((cd_cb_fn)fn, ctx), name, (uint32_t)__builtin_strlen(name), args, args_len, input, index, 1);
}

/* Room for an answer's bytes: a block of their size, or 0 when the heap has none (the shim then answers
 * NO_ROOM). The export that answers frees it. */
EXPORT("cd_job_room") void *cd_job_room(uint32_t n) { return cd_alloc_try(n ? n : 1); }
/* The callback that asked hears the answer; then p (a block of cd_job_room's, or 0) is freed, so nothing
 * holds the bytes past it. */
EXPORT("cd_job_got") void cd_job_got(uint32_t token, uint32_t status, const uint8_t *p, uint32_t len) {
  cd_cb_fn fn;
  void *ctx;
  if (cd_cb_take(token, &fn, &ctx)) ((cd_job_fn)fn)(ctx, status, p, len);
  cd_flush();
  cd_free((void *)p);
}
