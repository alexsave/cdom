/* The rows app, server-rendered. The same app.c that runs in the browser runs
 * here natively: cdom.c emits its op stream into the C sink (cdom_html.c), which
 * writes the HTML; the model goes into the page as a base64 state blob, and the
 * page resumes (cdom.h): listeners for the event types the render listened to go on the
 * mount, and the wasm loads only on the first event.
 *
 *   GET /?n=<rows>             the page with n rows (default 1000), seed SSR_SEED
 *   GET /csr/                  the plain client-rendered benchmark page, same build
 *   everything else            static files from the root (out/ next to the binary)
 *
 * Usage: server [--port 8701] [--root dir] | server --render <n> (page to stdout)
 * Each request renders in a forked child (ssr_forked): cdom.c and app.c keep their model in
 * globals, so a fresh process image is the clean slate for every page. */
#include "../../src/cdom.h"
#include "../../src/cdom_html.h"
#include "../../src/httpd.h"
#include "../../src/ssr_page.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SSR_SEED 0x2545f491u /* fixed, so the page is a pure function of n */
#define MAX_ROWS 100000

extern uint32_t cd_seed;
void app_ssr_fill(uint32_t n); /* app.c: the model "Create n rows" would make from cd_seed */

/* The shell (src/ssr_page.h): the benchmark's stylesheet and the readable shim from out/. */
static const ssr_page SHELL = {
  .title = "cdom-\"keyed\"",
  .head = "<link href=\"/css/currentStyle.css\" rel=\"stylesheet\"/>",
  .shim_src = "cdom.js",
  .wasm = "app.wasm",
};

static void fill(void *ctx) {
  cd_seed = SSR_SEED;
  app_ssr_fill(*(uint32_t *)ctx);
  cd_main();
}

/* The whole page for n rows. Runs the app once: call it in a fresh process. */
static void render_page(ssr_buf *p, void *ctx) { ssr_render(p, &SHELL, fill, ctx, 4096 + *(uint32_t *)ctx * 320); }

static int parse_n(const char *s, size_t len, uint32_t *n) {
  if (!len || len > 6) return -1;
  uint32_t v = 0;
  for (size_t i = 0; i < len; i++) {
    if (s[i] < '0' || s[i] > '9') return -1;
    v = v * 10 + (uint32_t)(s[i] - '0');
  }
  if (v > MAX_ROWS) return -1;
  *n = v;
  return 0;
}

/* GET /: render in a forked child, read the page back through a pipe. */
static void index_page(const httpd_req *q, httpd_res *r) {
  uint32_t n = 1000, len = 0;
  const char *v = httpd_query(q, "n", &len);
  if (v && parse_n(v, len, &n)) {
    httpd_status(r, 400, "text/plain");
    httpd_write(r, "n must be 0..100000\n", 20);
    return;
  }
  ssr_buf p = {0};
  if (ssr_forked(&p, render_page, &n)) {
    free(p.d);
    httpd_status(r, 500, "text/plain");
    httpd_write(r, "render failed\n", 14);
    return;
  }
  httpd_status(r, 200, "text/html; charset=utf-8");
  httpd_write(r, p.d, p.len);
  free(p.d);
}

int main(int argc, char **argv) {
  int port = 8701;
  const char *root = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--render") && i + 1 < argc) {
      uint32_t n;
      if (parse_n(argv[i + 1], strlen(argv[i + 1]), &n)) return 2;
      ssr_buf p = {0};
      render_page(&p, &n);
      return fwrite(p.d, 1, p.len, stdout) == p.len ? 0 : 1;
    }
    if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--root") && i + 1 < argc) root = argv[++i];
    else {
      fprintf(stderr, "usage: %s [--port p] [--root dir] | --render n\n", argv[0]);
      return 2;
    }
  }
  static char def[4096];
  if (!root) { /* default: the directory holding the binary */
    snprintf(def, sizeof def, "%s", argv[0]);
    char *s = strrchr(def, '/');
    if (s) *s = 0; else snprintf(def, sizeof def, ".");
    root = def;
  }
  httpd_route("GET", "/", index_page);
  fprintf(stderr, "rows ssr: http://127.0.0.1:%d/ (root %s)\n", port, root);
  return httpd_serve((uint16_t)port, root) ? 1 : 0;
}
