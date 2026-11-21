/* The native build of the renderer (render.h), for the Mac's tests and the reference pictures:
 *   ns2render html <page> <state> [batch]   the view's HTML (the same bytes the browser host writes) on stdout
 *   ns2render ppm <page> <state> <out.ppm>  the picture the runs make, 592 x 320, colours from the palette
 *   ns2render stats <page> <state>          runs, lines, HTML bytes, spans, and the same with the text left out
 *   ns2render time <page> <state> <n>       the renderer's time, n pictures
 * RD_SKIP_TEXT=1 leaves the text out, RD_RAMP_SHIFT=n sets the gradient's step (4096 >> n steps).
 * page 0 is still, 1 details; state 1 is normal, 2 the pill hovered. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "render.h"

static uint32_t ops[200000];
static int nops;

static void render(int page, int state, int batch) {
  nops = 0;
  rd_begin(page, state);
  int more;
  do {
    more = rd_rows(batch);
    memcpy(ops + nops, rd_out, (size_t)rd_nout * 4);
    nops += rd_nout;
  } while (more);
}

static void ppm(const char *path) {
  static unsigned char px[RD_H][RD_W][3];
  int i, y = 0, x = 0, h = 1;
  for (i = 0; i < nops; i++) {
    uint32_t op = ops[i] & 0xff000000u, v = ops[i] & 0xffffffu;
    if (op == RD_OP_LINE) { h = (int)v; x = 0; }
    else if (op == RD_OP_RUN) {
      uint32_t rgb = rd_palette[v >> 12 & 255];
      int w = (int)(v & 4095), yy, xx;
      for (yy = y; yy < y + h && yy < RD_H; yy++)
        for (xx = x; xx < x + w && xx < RD_W; xx++) {
          px[yy][xx][0] = (unsigned char)(rgb >> 16);
          px[yy][xx][1] = (unsigned char)(rgb >> 8);
          px[yy][xx][2] = (unsigned char)rgb;
        }
      x += w;
    } else if (op == RD_OP_END) y += h;
  }
  FILE *f = fopen(path, "wb");
  if (!f) { perror(path); exit(1); }
  fprintf(f, "P6\n%d %d\n255\n", RD_W, RD_H);
  fwrite(px, 1, sizeof px, f);
  fclose(f);
}

static void count(int *runs, int *lines, int *links) {
  int i;
  *runs = *lines = *links = 0;
  for (i = 0; i < nops; i++) {
    uint32_t op = ops[i] & 0xff000000u;
    if (op == RD_OP_RUN) ++*runs;
    else if (op == RD_OP_END) ++*lines;
    else if (op == RD_OP_LINK) ++*links;
  }
}

int main(int argc, char **argv) {
  static char html[4 << 20];
  if (argc < 4) { fprintf(stderr, "usage: ns2render html|ppm|stats|time page state [arg]\n"); return 2; }
  int page = atoi(argv[2]), state = atoi(argv[3]);
  if (getenv("RD_RAMP_SHIFT")) rd_ramp_shift = atoi(getenv("RD_RAMP_SHIFT"));
  if (getenv("RD_SKIP_TEXT")) rd_skip_text = atoi(getenv("RD_SKIP_TEXT"));
  if (!strcmp(argv[1], "html")) {
    render(page, state, argc > 4 ? atoi(argv[4]) : 8);
    int n = rd_html(ops, nops, html, sizeof html);
    fwrite(html, 1, (size_t)n, stdout);
  } else if (!strcmp(argv[1], "ppm") && argc > 4) {
    render(page, state, 8);
    ppm(argv[4]);
  } else if (!strcmp(argv[1], "stats")) {
    int runs, lines, links, truns, tlines, tlinks;
    render(page, state, 8);
    int spans = rd_stat_spans;
    count(&runs, &lines, &links);
    int bytes = rd_html(ops, nops, html, sizeof html);
    rd_skip_text = 1;
    render(page, state, 8);
    count(&truns, &tlines, &tlinks);
    int tbytes = rd_html(ops, nops, html, sizeof html);
    rd_skip_text = 0;
    printf("page %d state %d: runs %d lines %d links %d bytes %d spans %d words %d | no text: runs %d lines %d bytes %d | text adds %d runs (%.0f%%)\n",
           page, state, runs, lines, links, bytes, spans, nops, truns, tlines, tbytes, runs - truns, 100.0 * (runs - truns) / runs);
  } else if (!strcmp(argv[1], "time")) {
    int i, n = argc > 4 ? atoi(argv[4]) : 100;
    clock_t t0 = clock();
    for (i = 0; i < n; i++) render(page, state, 8);
    printf("%.3f ms a picture\n", (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / n);
  } else return 2;
  return 0;
}
