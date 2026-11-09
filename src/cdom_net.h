/* cdom_net: WebSocket and fetch, bytes straight into C memory. Link src/cdom_net.c to use it.
 * Incoming bytes are copied into a buffer cdom owns and handed to the app's hook as (p, len),
 * valid until the hook returns: no strings and no JSON cross. Every answer arrives from the
 * event loop through an export that flushes on its way out. */
/* cd_ws_open_cb, cd_ws_send, cd_ws_close, cd_ws_buffered, cd_http_cb, cd_page_origin and cd_epoch_ms are part
 * of the I/O contract (cdom.h, top): they change only additively. */
#ifndef CDOM_NET_H
#define CDOM_NET_H
#include <stdint.h>

/* A socket id is good from cd_ws_open until its cd_on_ws_close: the id is free by then, and a
 * cd_ws_open (from that hook or later) may hand it out again, so the shim's table grows with the
 * sockets open at once, not with every socket a page ever opened. Sending on or closing an id
 * after its close answer throws (or reaches the socket that took the id since). */
uint32_t cd_ws_open(const char *url, uint32_t len);          /* -> socket id; binary messages */
void cd_ws_send(uint32_t id, const void *p, uint32_t len);   /* one binary message, p read during the call */
void cd_ws_close(uint32_t id);
/* The socket's bufferedAmount: bytes sent and not yet handed to the network. A sender that must
 * not queue without bound (a slow link) holds back while it is high. */
uint32_t cd_ws_buffered(uint32_t id);
/* A socket whose events go to a callback: fn(ctx, id, CD_WS_OPEN, 0, 0) when it opens, (ctx, id,
 * CD_WS_MSG, p, len) for each message (p valid until fn returns), and (ctx, id, CD_WS_CLOSE, 0, code)
 * once at the end (also for a socket that never opened), where cd_ws_open's would reach cd_on_ws_open,
 * cd_on_ws_msg and cd_on_ws_close. The hooks hear nothing of it, and fn nothing of another socket.
 * cd_ws_send, cd_ws_close and cd_ws_buffered take its id as any; the id is good until the close
 * answer, as cd_ws_open's (and may come back from either form then). Its callback slot is freed at the
 * close, before fn hears it; it counts against the 65,535 cd_timer_cb timers, cd_http_cb requests and
 * store answers pending at once (traps past that) while the socket lives.
 * A socket the browser refuses to make (its WebSocket constructor throws: a SyntaxError for a URL it
 * does not take, such as one of a scheme it opens no socket on or one with a fragment, a SecurityError where a browser refuses on
 * the spot, as Firefox does for ws: from an https: page) still gets its id: fn hears (ctx, id,
 * CD_WS_REFUSED, 0, 0), then (ctx, id, CD_WS_CLOSE, 0, CD_WS_REFUSED_CODE), each from the event loop
 * (never inside cd_ws_open_cb), and never CD_WS_OPEN. A caller that knows only OPEN, MSG and CLOSE sees
 * a close before an open, as for a server that never answered. Until that close, cd_ws_send drops the
 * bytes, cd_ws_close does nothing and cd_ws_buffered is 0. What a browser refuses later (a blocked
 * port, a Content-Security-Policy rule in Chrome) is a socket that failed: a close before an open. The
 * hooks' socket (cd_ws_open) has no refusal: the constructor's throw goes out of the call. */
#define CD_WS_OPEN    0
#define CD_WS_MSG     1
#define CD_WS_CLOSE   2
#define CD_WS_REFUSED 3
#define CD_WS_REFUSED_CODE 1006 /* the close code a refused socket's close carries: abnormal closure */
typedef void (*cd_ws_fn)(void *ctx, uint32_t id, uint32_t ev, const uint8_t *p, uint32_t len);
uint32_t cd_ws_open_cb(cd_ws_fn fn, void *ctx, const char *url, uint32_t len);

/* One request. The answer is cd_on_http(token, status, p, len): the response's status and body;
 * CD_HTTP_FAILED (0) when it never arrived (a network failure, a refused CORS check); or
 * CD_HTTP_TIMEOUT when deadline_ms passed first and the request was aborted (deadline_ms 0: none).
 * headers is one block of "Name: value\n" lines, hlen bytes (the last newline may be left out),
 * so "Authorization: Bearer <jwt>\nContent-Type: application/octet-stream\n" is a memcpy or two
 * in C and one string across. body is read during the call (blen 0: no body). The response's
 * headers are not read. */
void cd_http(uint32_t token, const char *method, uint32_t mlen, const char *url, uint32_t ulen,
             const char *headers, uint32_t hlen, const void *body, uint32_t blen, uint32_t deadline_ms);
#define CD_HTTP_FAILED  0
#define CD_HTTP_TIMEOUT 1   /* not an HTTP status (those are 100 to 599) */
/* The same request, answered to a callback: fn(ctx, status, p, len) once, with what cd_on_http would
 * get (the status or CD_HTTP_FAILED or CD_HTTP_TIMEOUT; p valid until fn returns), and cd_on_http does
 * not hear it. cdom picks the token, from its own range (cdom.h, CD_TOKEN_CDOM), so it never meets an
 * app's. A request with a callback cannot be cancelled; at most 65,535 cd_http_cb requests, cd_timer_cb
 * timers, cd_ws_open_cb sockets and store _cb answers are pending at once (traps past that). cd_http traps on a token in cdom's range. */
typedef void (*cd_http_fn)(void *ctx, uint32_t status, const uint8_t *p, uint32_t len);
void cd_http_cb(cd_http_fn fn, void *ctx, const char *method, uint32_t mlen, const char *url, uint32_t ulen,
                const char *headers, uint32_t hlen, const void *body, uint32_t blen, uint32_t deadline_ms);

/* The page's scheme and host, as location.protocol + "//" + location.host ("https://app.example:8443",
 * "http://127.0.0.1:5173"; "file://" for a page with no host), written to dst (at most cap bytes):
 * what a socket or a request on the page's own server is addressed by. Returns its length, which may be
 * more than cap (then only cap bytes were written). */
uint32_t cd_page_origin(char *dst, uint32_t cap);

/* Wall-clock time in epoch ms (Date.now()): what a server's expiry times are written in.
 * cd_now() is the page's own clock, for intervals. */
double cd_epoch_ms(void);

/* The app's hooks (empty weak defaults in cdom_net.c). They are the app's own: a library linked into
 * an app (an SDK, a client) never defines them, since the app may, and uses cd_ws_open_cb and
 * cd_http_cb, whose answers never reach them. */
void cd_on_ws_open(uint32_t id);
void cd_on_ws_msg(uint32_t id, const uint8_t *p, uint32_t len);
void cd_on_ws_close(uint32_t id, uint32_t code);
void cd_on_http(uint32_t token, uint32_t status, const uint8_t *p, uint32_t len);
#endif
