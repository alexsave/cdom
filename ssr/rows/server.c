/* The rows app, server-rendered. The same app.c that runs in the browser runs
 * here natively: cdom.c emits its op stream into the C sink (cdom_html.c), which
 * writes the HTML; the model goes into the page as a base64 state blob, and the
 * shim hydrates over the server's nodes instead of building them.
 *
 *   GET /?n=<rows>[&verify=1]  the page with n rows (default 1000), seed SSR_SEED
 *   GET /csr/                  the plain client-rendered benchmark page, same build
 *   everything else            static files from the root (out/ next to the binary)
 *
 * Usage: server [--port 8701] [--root dir] | server --render <n> (page to stdout)
 * Each request renders in a forked child: cdom.c and app.c keep their model in
 * globals, so a fresh process image is the clean slate for every page. */
#include "../../src/cdom.h"
#include "../../src/cdom_html.h"
#include "../../src/httpd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define SSR_SEED 0x2545f491u /* fixed, so the page is a pure function of n */
#define MAX_ROWS 100000

extern uint32_t cd_seed;
void app_ssr_fill(uint32_t n); /* app.c: the model "Create n rows" would make from cd_seed */

/* ---- a growable page buffer ---- */
typedef struct { char *d; size_t len, cap; } page;

static char *room(page *p, size_t n) {
  if (p->len + n > p->cap) {
    size_t c = p->cap ? p->cap : 65536;
    while (c < p->len + n) c *= 2;
    p->d = realloc(p->d, c);
    if (!p->d) abort();
    p->cap = c;
  }
  p->len += n;
  return p->d + p->len - n;
}

static void puts_(page *p, const char *s) {
  size_t n = strlen(s);
  memcpy(room(p, n), s, n);
}

static void base64(page *p, const uint8_t *s, size_t n) {
  static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char *d = room(p, (n + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 3 <= n; i += 3, d += 4) {
    uint32_t v = (uint32_t)s[i] << 16 | (uint32_t)s[i + 1] << 8 | s[i + 2];
    d[0] = A[v >> 18]; d[1] = A[(v >> 12) & 63]; d[2] = A[(v >> 6) & 63]; d[3] = A[v & 63];
  }
  if (i < n) {
    uint32_t v = (uint32_t)s[i] << 16 | (i + 1 < n ? (uint32_t)s[i + 1] << 8 : 0);
    d[0] = A[v >> 18]; d[1] = A[(v >> 12) & 63];
    d[2] = i + 1 < n ? A[(v >> 6) & 63] : '=';
    d[3] = '=';
  }
}

/* The whole page for n rows. Runs the app once: call it in a fresh process. */
static void render_page(page *p, uint32_t n, int verify) {
  cd_html_reset();
  cd_seed = SSR_SEED;
  app_ssr_fill(n);
  cd_main();
  cd_flush();
  puts_(p, "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\"/>"
           "<title>cdom-\"keyed\"</title><link href=\"/css/currentStyle.css\" rel=\"stylesheet\"/>"
           "<link rel=\"preload\" href=\"app.wasm\" as=\"fetch\" type=\"application/wasm\" crossorigin/>"
           "</head><body><div id=\"main\">");
  uint32_t guess = 4096 + n * 320, got;
  char *h = room(p, guess);
  got = cd_html_write(h, guess);
  p->len -= guess;
  if (got > guess) got = cd_html_write(room(p, got), got);
  else p->len += got;
  puts_(p, "</div><script type=\"application/octet-stream\" id=\"cdom-state\">");
  uint32_t need = cd_state_save(0, 0);
  uint8_t *blob = malloc(need ? need : 1);
  if (!blob || cd_state_save(blob, need) != need) abort();
  base64(p, blob, need);
  free(blob);
  puts_(p, "</script><script src=\"cdom.js\"></script><script>cdom.start('app.wasm', "
           "document.getElementById('main'), null, {hydrate: 'cdom-state'");
  if (verify) puts_(p, ", verify: true");
  puts_(p, "});</script></body></html>");
}

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
  const char *vf = httpd_query(q, "verify", &len);
  int verify = vf && len == 1 && vf[0] == '1';
  int fd[2];
  if (pipe(fd)) goto fail;
  pid_t pid = fork();
  if (pid < 0) {
    close(fd[0]);
    close(fd[1]);
    goto fail;
  }
  if (!pid) {
    close(fd[0]);
    page p = {0};
    render_page(&p, n, verify);
    for (size_t o = 0; o < p.len;) {
      ssize_t k = write(fd[1], p.d + o, p.len - o);
      if (k <= 0) _exit(1);
      o += (size_t)k;
    }
    _exit(0);
  }
  close(fd[1]);
  page p = {0};
  for (;;) {
    ssize_t k = read(fd[0], room(&p, 65536), 65536);
    p.len -= 65536 - (k > 0 ? (size_t)k : 0);
    if (k <= 0) break;
  }
  close(fd[0]);
  int st = 0;
  waitpid(pid, &st, 0);
  if (!WIFEXITED(st) || WEXITSTATUS(st)) {
    free(p.d);
    goto fail;
  }
  httpd_status(r, 200, "text/html; charset=utf-8");
  httpd_write(r, p.d, p.len);
  free(p.d);
  return;
fail:
  httpd_status(r, 500, "text/plain");
  httpd_write(r, "render failed\n", 14);
}

int main(int argc, char **argv) {
  int port = 8701;
  const char *root = 0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--render") && i + 1 < argc) {
      uint32_t n;
      if (parse_n(argv[i + 1], strlen(argv[i + 1]), &n)) return 2;
      page p = {0};
      render_page(&p, n, 0);
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
