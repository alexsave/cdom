/* cdom_job: run a big piece of C in a throwaway worker. Link src/cdom_job.c to use it (APP_MODULES += job).
 * The job is a module of its own (src/cdom_job_mod.h is its side), built beside the app's (cdom.mk,
 * APP_JOBS). A call compiles that module once, starts a fresh dedicated worker from a blob: URL, hands it
 * the input, and ends the worker (worker.terminate) when it answers: the job's whole memory, whatever it
 * touched, goes back to the system at once, and none of its time is on the page's main thread.
 * The calls are explicit, with no registry: the module is named by the call. Every answer comes from the
 * event loop, never inside the call, and counts against the 65,535 callbacks pending at once.
 * A worker is a page-wide resource: two jobs at once are two workers. A job has no deadline of its own;
 * an app that wants one starts a cd_timer_cb and ignores a late answer (cd_req_current, cdom.h).
 * A page under a Content-Security-Policy needs worker-src blob:, and (bundled) connect-src data:. */
#ifndef CDOM_JOB_H
#define CDOM_JOB_H
#include <stdint.h>
#include "cdom.h"

/* The answers. p and len are what each status says; p is valid until the callback returns, and cdom frees
 * it then (copy what is kept). */
#define CD_JOB_OK 0          /* p: the job's output (what it passed to cd_job_answer; len 0 when it passed none); len: its count */
#define CD_JOB_NO_ROOM 1     /* the job's memory had no room (for its input, or run returned CD_JOB_RUN_NO_ROOM), or C memory had none for the output (len: its count); p 0 */
#define CD_JOB_FAILED 2      /* the module would not load or start, the job trapped, returned another non-zero value, or the worker failed; p 0, len 0 */
#define CD_JOB_UNSUPPORTED 3 /* this browser has no Worker and WebAssembly together (the IE tiers); p 0, len 0 */
#define CD_JOB_NO_FILE 4     /* cd_job_file_cb: nothing at that index (none picked, or the node has no files list); p 0, len 0 */

typedef void (*cd_job_fn)(void *ctx, uint32_t status, const uint8_t *p, uint32_t len);

/* Runs the job module `name` on in (in_len bytes of C memory) and args (args_len bytes of the app's own
 * layout), both read during the call. name is a bundled module's name (APP_JOBS, cdom.mk) or the module's
 * URL; the page compiles it once and keeps it. The input crosses as a transferred ArrayBuffer (one copy out
 * of C memory, none more), and the answer is fn(ctx, status, p, len). */
void cd_job_cb(const char *name, const void *in, uint32_t in_len, const void *args, uint32_t args_len,
               cd_job_fn fn, void *ctx);
/* The same, the input a picked file (index of node input's files list, as cd_file_read): the worker reads
 * it straight into the job's memory, and it never enters the app's. No size limit but the job's own room:
 * a file the job's memory cannot hold is NO_ROOM. */
void cd_job_file_cb(const char *name, cd_id input, uint32_t index, const void *args, uint32_t args_len,
                    cd_job_fn fn, void *ctx);

#endif
