/* An app that replaces malloc and nothing else: cdom's free would get its pointers. It must trap. */
#include <stddef.h>
#include <stdint.h>
static uint8_t arena[1 << 20];
static size_t used;
void *malloc(size_t n) { void *p = arena + used; used += (n + 15) & ~(size_t)15; return p; }
