/* tests/image/host_alloc.c: the image module's heap in a native build, counted. */
#ifndef HOST_ALLOC_H
#define HOST_ALLOC_H
#include <stddef.h>
#include <stdint.h>
extern size_t host_live, host_peak, host_blocks, host_calls, host_room;
extern int host_full;
/* The room from now on (SIZE_MAX: no limit), the heap no longer full, the peak and the call count from here. */
void host_reset(size_t room);
void *cd_alloc_try(uint32_t n);
void *cd_realloc_try(void *p, uint32_t n);
void cd_free(void *p);
#endif
