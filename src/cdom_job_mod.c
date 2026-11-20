/* cdom_job_mod: what a job module exports besides run (cdom_job_mod.h): the room the worker puts the input in.
 * Link it with src/cdom.c built -DCDOM_HEAP_ONLY and src/mini.c. */
#include "cdom_job_mod.h"

/* A block of n bytes of the job's heap, or 0 when it has no room: the worker answers NO_ROOM then. */
__attribute__((export_name("job_room"))) void *job_room(uint32_t n) { return cd_alloc_try(n ? n : 1); }
