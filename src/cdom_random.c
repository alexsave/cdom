/* cdom_random: the random bytes import, asked for chunk by chunk (cdom_random.h). Separate from
 * cdom.c so an app without it does not carry it. */
#include "cdom.h"
#include "cdom_random.h"

#define IMPORT(n) __attribute__((import_module("cdom"), import_name(n)))

IMPORT("random") uint32_t j_random(void *p, uint32_t len);

/* getRandomValues throws past 65536 bytes; the chunking is C's, and the shim fills one view. */
uint32_t cd_random_bytes(void *buf, uint32_t len) {
  uint8_t *p = buf;
  while (len) {
    uint32_t n = len < CD_RANDOM_CHUNK ? len : CD_RANDOM_CHUNK;
    if (!j_random(p, n)) return CD_RANDOM_UNAVAILABLE;
    p += n;
    len -= n;
  }
  return CD_RANDOM_OK;
}
