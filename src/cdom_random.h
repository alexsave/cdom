/* cdom_random: bytes from the browser's cryptographic generator (crypto.getRandomValues), written
 * straight into C memory. Link src/cdom_random.c to use it. Synchronous: the bytes are there when
 * the call returns, so no answer comes back. */
#ifndef CDOM_RANDOM_H
#define CDOM_RANDOM_H
#include <stdint.h>

/* Fill len bytes at buf with random bytes fit for secrets (a key, a nonce, a session id). The
 * browser hands out at most CD_RANDOM_CHUNK bytes per call, so C asks chunk by chunk and any len
 * works. CD_RANDOM_OK when every byte was written; CD_RANDOM_UNAVAILABLE when the page has no
 * crypto.getRandomValues (a very old browser), and then nothing is written: an app that needs the
 * bytes secret checks, and does not fall back on Math.random. len 0 writes nothing and is OK. */
#define CD_RANDOM_UNAVAILABLE 0
#define CD_RANDOM_OK 1
#define CD_RANDOM_CHUNK 65536
uint32_t cd_random_bytes(void *buf, uint32_t len);
#endif
