/* httpd: a minimal single-process HTTP/1.1 server over POSIX sockets and poll().
 * Static files from one directory, GET/POST routes registered by the app,
 * keep-alive and pipelining, bounded header and body sizes, 127.0.0.1 only.
 * Handlers run to completion on the one thread; a handler that must not share
 * state between requests can fork (ssr/rows/server.c does). */
#ifndef HTTPD_H
#define HTTPD_H
#include <stddef.h>
#include <stdint.h>

#ifndef HTTPD_MAX_HEAD
#define HTTPD_MAX_HEAD 16384 /* request line + headers; more is 431 and a close */
#endif
#ifndef HTTPD_MAX_BODY
#define HTTPD_MAX_BODY (64u << 20) /* Content-Length above this is 413 and a close */
#endif
#define HTTPD_MAX_HEADERS 64

typedef struct {
  const char *name, *value; /* NUL-terminated; value has its surrounding spaces trimmed */
} httpd_hdr;

typedef struct {
  const char *method; /* "GET", "POST", ... */
  const char *path;   /* percent-decoded, without the query; always starts with '/' */
  const char *query;  /* raw bytes after '?', "" when there is none */
  const char *body;   /* Content-Length bytes (not NUL-terminated) */
  uint32_t body_len;
  const httpd_hdr *headers;
  uint32_t nheaders;
} httpd_req;

typedef struct httpd_res httpd_res;
typedef void httpd_handler(const httpd_req *q, httpd_res *r);

/* Register a handler for an exact method and path (before httpd_serve). A GET
 * route also answers HEAD. Returns 0, or -1 when the table is full. */
int httpd_route(const char *method, const char *path, httpd_handler *h);

/* Case-insensitive header lookup: the first value, or 0. */
const char *httpd_header(const httpd_req *q, const char *name);

/* The raw value of key in the query string (not decoded), or 0; *len gets its length. */
const char *httpd_query(const httpd_req *q, const char *key, uint32_t *len);

/* The response: status and content type (default 200, text/plain), then body bytes.
 * httpd_room returns space for n more bytes (the body grows by n); httpd_shrink
 * gives back what was not used, so a writer can reserve, write, then trim. */
void httpd_status(httpd_res *r, int status, const char *content_type);
void httpd_write(httpd_res *r, const void *p, size_t n);
char *httpd_room(httpd_res *r, size_t n);
void httpd_shrink(httpd_res *r, size_t unused);

/* Serve on 127.0.0.1:port with static files under root (0 for none). Runs until
 * a fatal socket error; returns -1 then (or when the port cannot be bound). */
int httpd_serve(uint16_t port, const char *root);

#endif
