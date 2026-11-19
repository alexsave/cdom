/* cdom_job_mod: the job module's side of cd_job (cdom_job.h). A job module is a wasm module of its own:
 * its source, src/cdom.c compiled with -DCDOM_HEAP_ONLY (cdom's heap: cd_alloc, cd_alloc_try, malloc),
 * src/mini.c (the program's libc) and src/cdom_job_mod.c. It has no DOM and no cdom imports. cdom.mk builds
 * it beside the app's (APP_JOBS).
 * It defines `run`, the one function the worker calls, and answers by calling cd_job_answer. */
#ifndef CDOM_JOB_MOD_H
#define CDOM_JOB_MOD_H
#include <stdint.h>
#include "cdom.h"

#define CD_JOB_RUN_OK 0      /* run returns this when it finished (its output is whatever it passed to cd_job_answer) */
#define CD_JOB_RUN_NO_ROOM 1 /* run returns this when cd_alloc_try answered 0: the app hears CD_JOB_NO_ROOM; any other non-zero is CD_JOB_FAILED */

/* The job's one entry, exported as "run". in and args are blocks of the job's own heap, filled before the
 * call (in is in_len bytes, as given to cd_job_cb, or the file's bytes for cd_job_file_cb; args likewise)
 * and not freed by the worker: the memory goes with the instance. */
#define CD_JOB_ENTRY __attribute__((export_name("run")))
CD_JOB_ENTRY uint32_t run(const uint8_t *in, uint32_t in_len, const uint8_t *args, uint32_t args_len);

/* Hands the worker the output: p and len are read during the call, so the block may be freed or reused
 * after it. The last call before run returns counts; none at all is an empty output. */
__attribute__((import_module("cdjob"), import_name("answer"))) void cd_job_answer(const void *p, uint32_t len);

#endif
