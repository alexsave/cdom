/* The server under test for tests/httpd.mjs: two routes and a static root.
 * Usage: httpd_test <port> <root> */
#include "../src/httpd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void hello(const httpd_req *q, httpd_res *r) {
  uint32_t n = 0;
  const char *v = httpd_query(q, "n", &n);
  httpd_status(r, 200, "text/plain; charset=utf-8");
  httpd_write(r, "hello ", 6);
  if (v) httpd_write(r, v, n);
}

static void echo(const httpd_req *q, httpd_res *r) {
  const char *t = httpd_header(q, "x-echo-type");
  httpd_status(r, 200, t ? t : "application/octet-stream");
  httpd_write(r, q->body, q->body_len);
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s port root\n", argv[0]);
    return 2;
  }
  httpd_route("GET", "/hello", hello);
  httpd_route("POST", "/echo", echo);
  return httpd_serve((uint16_t)atoi(argv[1]), argv[2]) ? 1 : 0;
}
