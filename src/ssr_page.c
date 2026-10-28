/* ssr_page: the server-rendered page shell (ssr_page.h). */
#include "ssr_page.h"
#include "cdom.h"
#include "cdom_html.h"
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---- writing with the cd_html_write rule: count everything, copy what fits ---- */
typedef struct { char *d; uint32_t cap, n; } w;

static void put(w *o, const char *s, size_t n) {
  if (o->n < o->cap) memcpy(o->d + o->n, s, n < o->cap - o->n ? n : o->cap - o->n);
  o->n += (uint32_t)n;
}
static void puts_(w *o, const char *s) {
  if (s) put(o, s, strlen(s));
}

static void base64(w *o, const uint8_t *s, size_t n) {
  static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char d[4];
  size_t i = 0;
  for (; i + 3 <= n; i += 3) {
    uint32_t v = (uint32_t)s[i] << 16 | (uint32_t)s[i + 1] << 8 | s[i + 2];
    d[0] = A[v >> 18]; d[1] = A[(v >> 12) & 63]; d[2] = A[(v >> 6) & 63]; d[3] = A[v & 63];
    put(o, d, 4);
  }
  if (i < n) {
    uint32_t v = (uint32_t)s[i] << 16 | (i + 1 < n ? (uint32_t)s[i + 1] << 8 : 0);
    d[0] = A[v >> 18]; d[1] = A[(v >> 12) & 63];
    d[2] = i + 1 < n ? A[(v >> 6) & 63] : '=';
    d[3] = '=';
    put(o, d, 4);
  }
}

uint32_t ssr_page_write(char *dst, uint32_t cap, const ssr_page *p) {
  w o = {dst, cap, 0};
  puts_(&o, "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\"/><title>");
  puts_(&o, p->title);
  puts_(&o, "</title>");
  puts_(&o, p->head);
  puts_(&o, "</head><body><div id=\"main\">");
  put(&o, p->main_html, p->main_len);
  puts_(&o, "</div><script type=\"application/octet-stream\" id=\"cdom-state\">");
  base64(&o, p->state, p->state_len);
  puts_(&o, "</script>");
  puts_(&o, p->scripts);
  if (p->shim_src) {
    puts_(&o, "<script src=\"");
    puts_(&o, p->shim_src);
    puts_(&o, "\"></script>");
  } else {
    puts_(&o, "<script>");
    put(&o, p->shim_text, p->shim_len);
    puts_(&o, "</script>");
  }
  puts_(&o, "<script>cdom.resume('");
  puts_(&o, p->wasm);
  puts_(&o, "', 'main', 'cdom-state', ");
  put(&o, p->events, p->events_len);
  puts_(&o, p->eager ? ");cdom.boot();</script></body></html>" : ");</script></body></html>");
  return o.n;
}

char *ssr_room(ssr_buf *b, size_t n) {
  if (b->len + n > b->cap) {
    size_t c = b->cap ? b->cap : 65536;
    while (c < b->len + n) c *= 2;
    b->d = realloc(b->d, c);
    if (!b->d) abort();
    b->cap = c;
  }
  b->len += n;
  return b->d + b->len - n;
}

void ssr_render(ssr_buf *out, const ssr_page *shell, void (*fill)(void *ctx), void *ctx, uint32_t guess) {
  cd_html_reset();
  fill(ctx);
  cd_flush();
  ssr_page p = *shell;
  char *main = malloc(guess ? guess : 1);
  if (!main) abort();
  uint32_t got = cd_html_write(main, guess);
  if (got > guess) {
    free(main);
    main = malloc(got);
    if (!main || cd_html_write(main, got) != got) abort();
  }
  uint32_t need = cd_state_save(0, 0);
  uint8_t *blob = malloc(need ? need : 1);
  if (!blob || cd_state_save(blob, need) != need) abort();
  uint32_t ne = cd_html_resume_events(0, 0);
  char *ev = malloc(ne ? ne : 1);
  if (!ev) abort();
  cd_html_resume_events(ev, ne);
  p.main_html = main;
  p.main_len = got;
  p.state = blob;
  p.state_len = need;
  p.events = ev;
  p.events_len = ne;
  uint32_t n = ssr_page_write(0, 0, &p);
  if (ssr_page_write(ssr_room(out, n), n, &p) != n) abort();
  free(main);
  free(blob);
  free(ev);
}

int ssr_forked(ssr_buf *out, void (*render)(ssr_buf *out, void *ctx), void *ctx) {
  int fd[2];
  if (pipe(fd)) return -1;
  pid_t pid = fork();
  if (pid < 0) {
    close(fd[0]);
    close(fd[1]);
    return -1;
  }
  if (!pid) {
    close(fd[0]);
    ssr_buf p = {0};
    render(&p, ctx);
    for (size_t o = 0; o < p.len;) {
      ssize_t k = write(fd[1], p.d + o, p.len - o);
      if (k <= 0) _exit(1);
      o += (size_t)k;
    }
    _exit(0);
  }
  close(fd[1]);
  for (;;) {
    ssize_t k = read(fd[0], ssr_room(out, 65536), 65536);
    out->len -= 65536 - (k > 0 ? (size_t)k : 0);
    if (k <= 0) break;
  }
  close(fd[0]);
  int st = 0;
  waitpid(pid, &st, 0);
  if (!WIFEXITED(st) || WEXITSTATUS(st)) {
    out->len = 0;
    return -1;
  }
  return 0;
}
