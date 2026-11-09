/* The I/O contract (src/cdom.h, top): each declaration external adapters build on, with its exact type,
 * and each code and constant with its value. A declaration edited in place fails to compile here, so a
 * breaking change has to come as a new name. run.sh compiles it under cdom's flags. */
#include <stdio.h>
#include <string.h>
#include "../../src/cdom.h"
#include "../../src/cdom_net.h"
#include "../../src/cdom_store.h"
#include "../../src/cdom_random.h"

#define SAME(f, type) _Static_assert(__builtin_types_compatible_p(__typeof__(f), type), #f " is no longer " #type)

SAME(cd_http_fn, void (*)(void *, uint32_t, const uint8_t *, uint32_t));
SAME(cd_http_cb, void (cd_http_fn, void *, const char *, uint32_t, const char *, uint32_t, const char *, uint32_t,
                       const void *, uint32_t, uint32_t));
SAME(cd_ws_fn, void (*)(void *, uint32_t, uint32_t, const uint8_t *, uint32_t));
SAME(cd_ws_open_cb, uint32_t (cd_ws_fn, void *, const char *, uint32_t));
SAME(cd_ws_send, void (uint32_t, const void *, uint32_t));
SAME(cd_ws_close, void (uint32_t));
SAME(cd_ws_buffered, uint32_t (uint32_t));
SAME(cd_page_origin, uint32_t (char *, uint32_t));
SAME(cd_timer_fn, void (*)(void *));
SAME(cd_timer_cb, uint32_t (uint32_t, cd_timer_fn, void *));
SAME(cd_timer_stop, void (uint32_t));
SAME(cd_store_fn, void (*)(void *, const uint8_t *, int32_t));
SAME(cd_store_done_fn, void (*)(void *, uint32_t));
SAME(cd_store_get_cb, void (cd_store_fn, void *, const char *, uint32_t, uint32_t));
SAME(cd_store_put_cb, void (cd_store_done_fn, void *, const char *, uint32_t, const void *, uint32_t));
SAME(cd_store_del_cb, void (cd_store_done_fn, void *, const char *, uint32_t));
SAME(cd_random_bytes, uint32_t (void *, uint32_t));
SAME(cd_epoch_ms, double (void));
SAME(cd_now, double (void));
SAME(memcpy, void *(void *restrict, const void *restrict, size_t));
SAME(memmove, void *(void *, const void *, size_t));
SAME(memset, void *(void *, int, size_t));
SAME(memcmp, int (const void *, const void *, size_t));
SAME(strlen, size_t (const char *));
SAME(snprintf, int (char *restrict, size_t, const char *restrict, ...));
SAME(vsnprintf, int (char *restrict, size_t, const char *restrict, va_list));

/* mini.c's definitions against string.h's and stdio.h's declarations: one that drifts is a conflicting type. The
 * macros go first, since a definition is a name followed by "(". */
#undef memcpy
#undef memmove
#undef memset
#include "../../src/mini.c"

_Static_assert(CD_HTTP_FAILED == 0 && CD_HTTP_TIMEOUT == 1, "the http status codes");
_Static_assert(CD_WS_OPEN == 0 && CD_WS_MSG == 1 && CD_WS_CLOSE == 2 && CD_WS_REFUSED == 3, "the socket event codes");
_Static_assert(CD_WS_REFUSED_CODE == 1006, "a refused socket's close code");
_Static_assert(CD_RANDOM_UNAVAILABLE == 0 && CD_RANDOM_OK == 1 && CD_RANDOM_CHUNK == 65536, "the random codes");
_Static_assert(CD_TOKEN_CDOM == 0x80000000u, "cdom's token range");
