/* ssr_page: the shell every server-rendered cdom page is written in, and the plumbing around
 * one render (a growable buffer, the #main HTML out of the sink, a render in a forked child).
 * The servers (ssr/rows/server.c, apps/<name>/server.c) fill an ssr_page and write it; what
 * differs between apps is data in the struct, never a second copy of the shell.
 *
 * The page, in order:
 *   <!DOCTYPE html><html lang="en"><head><meta charset="utf-8"/><title>TITLE</title>HEAD</head>
 *   <body><div id="main">MAIN</div>
 *   <script type="application/octet-stream" id="cdom-state">BASE64(STATE)</script>
 *   SCRIPTS                         a widget library's <script src>: defined before the shim runs,
 *                                   so before the wasm can start (cdom.h, HOST)
 *   <script src="SHIM_SRC"></script> or <script>SHIM_TEXT</script>
 *   <script>cdom.resume('WASM', 'main', 'cdom-state', EVENTS);</script>
 * with ";cdom.boot()" after the resume call when eager is set: the wasm loads at once instead of
 * at the first event (a page whose widget should show before anyone touches it).
 * There are no newlines between the parts. TITLE, HEAD and SCRIPTS are written as given (they
 * are the server's own literals). */
#ifndef SSR_PAGE_H
#define SSR_PAGE_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
  const char *title;                 /* raw HTML inside <title> */
  const char *head;                  /* extra head HTML, or 0 */
  const char *scripts;               /* HTML before the shim, or 0 */
  const char *shim_src;              /* the shim's URL; 0 to inline shim_text */
  const char *shim_text;             /* the (minified) shim, inlined when shim_src is 0 */
  uint32_t shim_len;
  const char *main_html;             /* the render, from the sink (ssr_main) */
  uint32_t main_len;
  const uint8_t *state;              /* the state blob (cd_state_save), written as base64 */
  uint32_t state_len;
  const char *events;                /* cd_html_resume_events: "['click'],['cdping']" */
  uint32_t events_len;
  const char *wasm;                  /* the module's URL */
  int eager;                         /* 1: cdom.boot() right after cdom.resume */
} ssr_page;

/* Writes the page into dst. Returns its length; when that is more than cap, nothing past cap
 * is written and the return value is the length needed (the cd_html_write rule). */
uint32_t ssr_page_write(char *dst, uint32_t cap, const ssr_page *p);

/* A growable byte buffer for a page. */
typedef struct { char *d; size_t len, cap; } ssr_buf;
char *ssr_room(ssr_buf *b, size_t n); /* n more bytes at the end; aborts when out of memory */

/* One render, the common part: cd_html_reset, then the caller's model setup and cd_main
 * (done by fill), cd_flush, then the page with #main from the sink and the state from
 * cd_state_save, appended to out. The page's other fields come from shell (main_html, state and
 * events are filled here). guess sizes the first try at the #main HTML. */
void ssr_render(ssr_buf *out, const ssr_page *shell, void (*fill)(void *ctx), void *ctx, uint32_t guess);

/* Runs render(out, ctx) in a forked child and reads the bytes back: cdom.c and the apps keep
 * their model in globals, so a fresh process image is the clean slate for every page. Returns 0
 * on success, -1 when the child failed (out is then empty). */
int ssr_forked(ssr_buf *out, void (*render)(ssr_buf *out, void *ctx), void *ctx);

#endif
