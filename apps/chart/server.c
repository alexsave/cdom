/* The chart demo, server-rendered: the same app.c that runs in the browser runs here natively
 * into the C sink. The page lists the points and leaves the canvas empty (a widget does not run
 * on the server); Chart.js loads from the CDN before the shim, and the page resumes (cdom.h):
 * the wasm loads at the first event, the deferred MOUNT draws the chart, then the event lands.
 * With ?eager=1 the page calls cdom.boot() at once, so the chart shows without an interaction.
 *
 *   GET /?n=<points>&eager=1   the page (n default 5, at most 1000)
 *   everything else            static files from the root (out/ next to the binary): app.wasm
 *
 * Usage: server [--port 8705] [--root dir] | server --render <n> [--eager] (page to stdout)
 * The minified shim is read from <root>/cdom.min.js at start and inlined into every page. */
#include "../../src/cdom.h"
#include "../../src/cdom_html.h"
#include "../../src/httpd.h"
#include "../../src/ssr_page.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SSR_SEED 0x2545f491u /* fixed, so the page is a pure function of n */
#define MAX_POINTS 1000

extern uint32_t cd_seed;
void app_ssr_fill(uint32_t n); /* app.c: n points drawn from cd_seed */

static ssr_page shell = {
  .title = "cdom chart",
  .head = "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"/><style>"
          "body{font:15px/1.4 system-ui,sans-serif;margin:16px;color:#222;background:#fff}"
          ".chart{position:relative;max-width:640px;height:300px;border:1px solid #ddd}"
          /* while no chart is mounted (before the first interaction, after Destroy): Chart.js
           * styles the canvas display:block when it mounts and takes that off when destroyed */
          ".chart:has(>canvas:not([style*=block]))::before{content:\"chart not mounted\";position:absolute;"
          "inset:0;display:grid;place-items:center;color:#888}"
          ".bar{display:flex;flex-wrap:wrap;gap:8px;margin:12px 0;align-items:center}"
          "#points{columns:4 120px;padding-left:20px}</style>",
  /* a widget library goes before the shim, so it is defined before the wasm can start */
  .scripts = "<script src=\"https://cdnjs.cloudflare.com/ajax/libs/Chart.js/4.4.1/chart.umd.min.js\" "
             "integrity=\"sha384-bs/nf9FbdNouRbMiFcrcZfLXYPKiPaGVGplVbv7dLGECccEXDW+S3zjqSKR5ZEaD\" "
             "crossorigin=\"anonymous\"></script>",
  .wasm = "app.wasm",
};

typedef struct { uint32_t n; int eager; } req;

static void fill(void *ctx) {
  cd_seed = SSR_SEED;
  app_ssr_fill(((req *)ctx)->n);
  cd_main();
}

static void render_page(ssr_buf *p, void *ctx) {
  ssr_page s = shell;
  s.eager = ((req *)ctx)->eager;
  ssr_render(p, &s, fill, ctx, 8192 + ((req *)ctx)->n * 64);
}

static int parse_n(const char *s, size_t len, uint32_t *n) {
  if (!len || len > 4) return -1;
  uint32_t v = 0;
  for (size_t i = 0; i < len; i++) {
    if (s[i] < '0' || s[i] > '9') return -1;
    v = v * 10 + (uint32_t)(s[i] - '0');
  }
  if (v > MAX_POINTS) return -1;
  *n = v;
  return 0;
}

static void index_page(const httpd_req *q, httpd_res *r) {
  req rq = {5, 0};
  uint32_t len = 0;
  const char *v = httpd_query(q, "n", &len);
  if (v && parse_n(v, len, &rq.n)) {
    httpd_status(r, 400, "text/plain");
    httpd_write(r, "n must be 0..1000\n", 18);
    return;
  }
  v = httpd_query(q, "eager", &len);
  rq.eager = v && len == 1 && *v == '1';
  ssr_buf p = {0};
  if (ssr_forked(&p, render_page, &rq)) {
    free(p.d);
    httpd_status(r, 500, "text/plain");
    httpd_write(r, "render failed\n", 14);
    return;
  }
  httpd_status(r, 200, "text/html; charset=utf-8");
  httpd_write(r, p.d, p.len);
  free(p.d);
}

static void load_shim(const char *root) {
  char path[4200];
  snprintf(path, sizeof path, "%s/cdom.min.js", root);
  FILE *f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "chart: cannot read %s (run apps/chart/build.sh)\n", path); exit(2); }
  ssr_buf b = {0};
  size_t k;
  while ((k = fread(ssr_room(&b, 65536), 1, 65536, f)) == 65536) {}
  b.len -= 65536 - k;
  fclose(f);
  if (memmem(b.d, b.len, "</", 2)) { fprintf(stderr, "chart: the shim holds \"</\": it cannot be inlined\n"); exit(2); }
  shell.shim_text = b.d;
  shell.shim_len = (uint32_t)b.len;
}

int main(int argc, char **argv) {
  int port = 8705, render = -1, eager = 0;
  const char *root = 0;
  uint32_t n = 5;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--render") && i + 1 < argc) {
      if (parse_n(argv[i + 1], strlen(argv[i + 1]), &n)) return 2;
      render = 1;
      i++;
    } else if (!strcmp(argv[i], "--eager")) eager = 1;
    else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--root") && i + 1 < argc) root = argv[++i];
    else {
      fprintf(stderr, "usage: %s [--port p] [--root dir] | --render n [--eager]\n", argv[0]);
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
  load_shim(root);
  if (render > 0) {
    req rq = {n, eager};
    ssr_buf p = {0};
    render_page(&p, &rq);
    return fwrite(p.d, 1, p.len, stdout) == p.len ? 0 : 1;
  }
  httpd_route("GET", "/", index_page);
  fprintf(stderr, "chart: http://127.0.0.1:%d/ (root %s)\n", port, root);
  return httpd_serve((uint16_t)port, root) ? 1 : 0;
}
