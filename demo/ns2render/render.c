/* The span renderer (render.h).  For each row: the items crossing it, back to front, each as a sorted list of spans merged
 * into the row's segment list in one pass (an opaque span replaces what is under it, a translucent one blends over each
 * segment it covers); then each segment becomes runs of palette colours (a gradient's segment one run a gradient step), runs
 * of the same colour merge, and the link regions cut them.  The work is per row and per item, never per pixel. */
#include "render.h"

uint32_t rd_out[RD_OUT_MAX];
int rd_nout;
int rd_stat_spans, rd_stat_runs, rd_stat_text_runs;
int rd_ramp_shift = 6;
int rd_skip_text;

/* Every table the rows read is of 32-bit words: JavaScript 1.0 (tools/js1c) reads a byte or a half through a helper call,
 * a word directly, and multiplies 32-bit integers through another helper (JMu), so the rows multiply only in doubles,
 * which are exact here (every product is under 2^53) and native there. */

/* --- composites: what a segment shows, a base paint and translucent colours over it, hash-consed ------------------------ */
#define CMAX 1024
#define CHASH 2048
static int c_parent[CMAX];   /* 0: a base; else the composite under this one, plus 1 */
static int c_paint[CMAX];    /* the base's paint (every composite keeps its base's) */
static int c_alpha[CMAX];
static uint32_t c_rgb[CMAX];
static int c_idx[CMAX];      /* a solid composite's palette colour, -1 not yet known */
static int ncomp;
static int c_hash[CHASH];    /* composite + 1, by (parent, rgb, alpha) */
/* (composite, gradient step) -> palette colour + 1, for composites over a gradient */
#define SHASH 2048
static int s_key[SHASH];
static int s_val[SHASH];

/* A page's paints and items, unpacked into words by rd_begin. */
typedef struct { int kind, stop, nstop; uint32_t rgb; int gx, gy, g0, ox, oy; } Paint;   /* ints: a double in memory costs JavaScript 1.0 a conversion each load */
typedef struct { int x0, y0, x1, y1, rt, rb, paint, alpha, state, kind, bw, text; } Item;
#define IMAX 256
static Paint paints[64];
static Item items[IMAX];
static int nitems;
static int by_y0[IMAX];      /* the items in order of their first row (paint order among equals) */
static int act[IMAX], nact, next_y0;

static const RdPage *pg;

static void comp_reset(void) {
  int i;
  ncomp = 0;
  for (i = 0; i < CHASH; i++) c_hash[i] = 0;
  for (i = 0; i < SHASH; i++) s_key[i] = 0;
}

static int comp_new(int parent, int paint, uint32_t rgb, int alpha) {
  int c = ncomp++;
  c_parent[c] = parent;
  c_paint[c] = paint;
  c_rgb[c] = rgb;
  c_alpha[c] = alpha;
  c_idx[c] = -1;
  return c;
}

/* The composite of rgb at alpha over composite under (the last answer kept: a text's runs ask the same over and over). */
static int last_under = -1, last_alpha, last_c;
static uint32_t last_rgb;
static int comp_over(int under, uint32_t rgb, int alpha) {
  if (under == last_under && rgb == last_rgb && alpha == last_alpha) return last_c;
  uint32_t h = (uint32_t)under ^ ((uint32_t)under << 6) ^ rgb ^ (rgb >> 13) ^ ((uint32_t)alpha << 3);
  int k = (int)(h & (CHASH - 1)), c;
  for (;;) {
    if (!c_hash[k]) {
      if (ncomp >= CMAX) return under;
      c = comp_new(under + 1, c_paint[under], rgb, alpha);
      c_hash[k] = c + 1;
      break;
    }
    c = c_hash[k] - 1;
    if (c_parent[c] == under + 1 && c_rgb[c] == rgb && c_alpha[c] == alpha) break;
    k = (k + 1) & (CHASH - 1);
  }
  last_under = under; last_rgb = rgb; last_alpha = alpha; last_c = c;
  return c;
}

/* --- colours ---------------------------------------------------------------------------------------------------------- */
static uint32_t mix(uint32_t under, uint32_t over, int a) {
  uint32_t out = 0;
  int sh;
  for (sh = 0; sh < 24; sh += 8) {
    int u = (int)((under >> sh) & 255), o = (int)((over >> sh) & 255);
    out |= (uint32_t)((o * a + u * (255 - a) + 127) / 255) << sh;
  }
  return out;
}

/* A paint's colour at gradient step s. */
static uint32_t paint_at(const Paint *p, int s) {
  if (p->kind == RD_SOLID) return p->rgb;
  int t = (s << rd_ramp_shift) + (1 << rd_ramp_shift >> 1);
  const RdStop *st = pg->stops + p->stop;
  int i, n = p->nstop;
  if (t <= st[0].at) return st[0].rgb;
  for (i = 1; i < n; i++)
    if (t <= st[i].at) {
      int span = st[i].at - st[i - 1].at;
      return mix(st[i - 1].rgb, st[i].rgb, span ? (t - st[i - 1].at) * 255 / span : 255);
    }
  return st[n - 1].rgb;
}

/* The composite's colour at step s: its base's, then each colour over it from the bottom up. */
static uint32_t comp_rgb(int c, int s) {
  int chain[64];
  int n = 0;
  while (c_parent[c] && n < 64) { chain[n++] = c; c = c_parent[c] - 1; }
  uint32_t rgb = paint_at(&paints[c_paint[c]], s);
  while (n) { c = chain[--n]; rgb = mix(rgb, c_rgb[c], c_alpha[c]); }
  return rgb;
}

/* The nearest palette colour (squared RGB distance, the lower index on a tie), among the candidates of its grid cell. */
static int nearest(uint32_t rgb) {
  int r = (int)(rgb >> 16 & 255), g = (int)(rgb >> 8 & 255), b = (int)(rgb & 255);
  int cell = ((r >> 5) << 6) + ((g >> 5) << 3) + (b >> 5);   /* 8 cells a channel: tools/gen_ns2_render.py's GRID */
  int i, best = 0;
  double bd = 1e9;
  for (i = rd_grid_start[cell]; i < rd_grid_start[cell + 1]; i++) {
    int k = rd_grid_cand[i];
    uint32_t p = rd_palette[k];
    double dr = r - (int)(p >> 16 & 255), dg = g - (int)(p >> 8 & 255), db = b - (int)(p & 255);
    double d = dr * dr + dg * dg + db * db;
    if (d < bd || (d == bd && k < best)) { bd = d; best = k; }
  }
  return best;
}

static int comp_index(int c, int s) {
  if (paints[c_paint[c]].kind == RD_SOLID) {
    if (c_idx[c] < 0) c_idx[c] = nearest(comp_rgb(c, 0));
    return c_idx[c];
  }
  int key = (c << 12 | s) + 1;
  int k = (key ^ (key >> 7) ^ (key >> 13)) & (SHASH - 1);
  while (s_key[k]) {
    if (s_key[k] == key) return s_val[k];
    k = (k + 1) & (SHASH - 1);
  }
  int v = nearest(comp_rgb(c, s));
  s_key[k] = key;
  s_val[k] = v;
  return v;
}

/* --- the row's segments: segment i is [sx[i], sx[i + 1]) of composite sc[i]; two buffers, swapped by apply ------------- */
#define SEGMAX 600
static int seg_x[2][SEGMAX + 1], seg_c[2][SEGMAX];
static int *sx = seg_x[0], *sc = seg_c[0], *nx = seg_x[1], *nc = seg_c[1];
static int nseg;
/* the spans of one item on this row: [a, b), and for a translucent span the colour's alpha (0: opaque, the composite) */
#define SPANMAX 512
static int pa[SPANMAX], pb[SPANMAX], pal_[SPANMAX];
static int npan;
static int span_comp;           /* an opaque item's composite */
static uint32_t span_rgb;       /* a translucent item's colour */

static void span(int a, int b, int alpha) {
  if (a < 0) a = 0;
  if (b > RD_W) b = RD_W;
  if (npan && a < pb[npan - 1]) a = pb[npan - 1];   /* overlapping glyphs: the later one starts where the earlier ends */
  if (b <= a || npan >= SPANMAX) return;
  pa[npan] = a;
  pb[npan] = b;
  pal_[npan] = alpha;
  npan++;
}

static int m_n;
static void emit(int x, int c) {
  if (m_n && nc[m_n - 1] == c) return;
  if (m_n >= SEGMAX) return;
  nx[m_n] = x;
  nc[m_n] = c;
  m_n++;
}

/* Merges the spans into the segments: the segments before the first span and after the last are copied as they are, the
 * ones between merged with the spans in one pass. */
static void apply(void) {
  int i = 0, k = 0, x, a = pa[0], b = pb[npan - 1], *t;
  rd_stat_spans += npan;
  while (sx[i + 1] <= a) { nx[i] = sx[i]; nc[i] = sc[i]; i++; }
  m_n = i;
  x = sx[i];
  while (x < b) {
    int e = sx[i + 1];
    if (k < npan && pa[k] <= x) {
      int end = pb[k] < e ? pb[k] : e;
      emit(x, pal_[k] ? comp_over(sc[i], span_rgb, pal_[k]) : span_comp);
      x = end;
      if (x == pb[k]) k++;
    } else {
      int nxt = k < npan ? pa[k] : RD_W;
      int end = nxt < e ? nxt : e;
      emit(x, sc[i]);
      x = end;
    }
    if (x == e) i++;
  }
  if (i < nseg && sx[i] < b) { emit(b, sc[i]); i++; }   /* the rest of the segment the last span ends inside */
  for (; i < nseg; i++) emit(sx[i], sc[i]);
  nx[m_n] = RD_W;
  nseg = m_n;
  t = sx; sx = nx; nx = t;
  t = sc; sc = nc; nc = t;
}

/* --- rounded corners: the inset of row dy of a corner of radius r -------------------------------------------------------- */
#define RMAX 160
#define IPOOL 3072
static int inset_tab[IPOOL];
static int inset_base[RMAX];   /* where radius r's rows start in inset_tab, plus 1; 0 not made yet */
static int inset_next;

static int inset(int r, int dy) {
  if (r >= RMAX) r = RMAX - 1;
  if (!inset_base[r] && inset_next + r > IPOOL) {   /* the pool is full: this radius is not kept */
    double d = (double)r - (double)dy - 0.5;
    return (int)((double)r - __builtin_sqrt((double)r * r - d * d) + 0.5);
  }
  if (!inset_base[r]) {
    int i;
    for (i = 0; i < r; i++) {
      double d = (double)r - (double)i - 0.5;
      double w = __builtin_sqrt((double)r * r - d * d);
      inset_tab[inset_next + i] = (int)((double)r - w + 0.5);
    }
    inset_base[r] = inset_next + 1;
    inset_next += r;
  }
  return inset_tab[inset_base[r] - 1 + dy];
}

/* The item's horizontal extent on row y (its corners cut), as l and r; 0 when the row misses it. */
static int extent(const Item *it, int y, int *l, int *r) {
  int in = 0, dy = y - it->y0, up = it->y1 - 1 - y;
  if (dy < it->rt) in = inset(it->rt, dy);
  else if (up < it->rb) in = inset(it->rb, up);
  *l = it->x0 + in;
  *r = it->x1 - in;
  return *r > *l;
}

/* The spans of a text item on row y, every glyph run that crosses it, at its level's share of the text's alpha. */
static void text_spans(const RdText *t, int y) {
  int i, lv[8];
  for (i = 0; i < rd_levels && i < 8; i++) {
    int a = t->alpha * i / (rd_levels - 1);
    lv[i] = a >= 255 ? 255 : a < 1 ? 1 : a;
  }
  for (i = 0; i < t->n; i++) {
    const RdPlace *p = &pg->places[t->first + i];
    const RdGlyph *g = &rd_glyphs[p->glyph];
    int gy = y - p->y, px = p->x;
    if (gy < 0 || gy >= g->h) continue;
    int j, row = g->row0 + gy;
    for (j = rd_glyph_rows[row]; j < rd_glyph_rows[row + 1]; j++) {
      const RdRun *u = &rd_glyph_runs[j];
      span(px + u->x, px + u->x + u->n, lv[u->level]);
    }
  }
}

/* The active items of row y, in paint order: the list of the row before, less the items that ended, plus the ones that
 * start here. */
static void activate(int y) {
  int i, n = 0;
  for (i = 0; i < nact; i++) if (items[act[i]].y1 > y) act[n++] = act[i];
  nact = n;
  while (next_y0 < nitems && items[by_y0[next_y0]].y0 <= y) {
    int k = by_y0[next_y0++], j = nact++;
    while (j > 0 && act[j - 1] > k) { act[j] = act[j - 1]; j--; }
    act[j] = k;
  }
}

static void row_segments(int y) {
  int i;
  sx[0] = 0;
  sx[1] = RD_W;
  sc[0] = 0;   /* composite 0: the page's paper, made by rd_begin */
  nseg = 1;
  activate(y);
  for (i = 0; i < nact; i++) {
    const Item *it = &items[act[i]];
    int l, r;
    npan = 0;
    if (it->kind == RD_TEXT) {
      if (rd_skip_text) continue;
      const RdText *t = &pg->texts[it->text];
      span_rgb = t->rgb;
      text_spans(t, y);
    } else {
      if (!extent(it, y, &l, &r)) continue;
      int a = it->alpha == 255 ? 0 : it->alpha;
      if (a) span_rgb = paints[it->paint].rgb;
      else span_comp = it->paint;   /* composite k is paint k alone (rd_begin makes them first) */
      if (it->kind == RD_RING && y >= it->y0 + it->bw && y < it->y1 - it->bw) {
        Item in;
        int l2, r2, bw = it->bw;
        in.x0 = it->x0 + bw; in.x1 = it->x1 - bw;
        in.y0 = it->y0 + bw; in.y1 = it->y1 - bw;
        in.rt = it->rt > bw ? it->rt - bw : 0;
        in.rb = it->rb > bw ? it->rb - bw : 0;
        if (extent(&in, y, &l2, &r2)) { span(l, l2, a); span(r2, r, a); }
        else span(l, r, a);
      } else span(l, r, a);
    }
    if (npan) apply();
  }
}

/* --- runs, links and lines -------------------------------------------------------------------------------------------- */
static uint32_t line_a[1024], line_b[1024], *cur = line_a, *pend = line_b;
static int ncur, npend, pend_h, row_y;

static int run_c = -1, run_w;
static void run_flush(void) {
  if (run_c >= 0 && run_w > 0 && ncur < 1020) { cur[ncur++] = RD_OP_RUN | (uint32_t)run_c << 12 | (uint32_t)run_w; rd_stat_runs++; }
  run_c = -1;
  run_w = 0;
}
static void run_add(int c, int w) {
  if (c != run_c) { run_flush(); run_c = c; }
  run_w += w;
}

/* The gradient step at x of a row whose t at the paint's ox is row0 (16.16): floor((gx (x - ox) + row0) / 65536), in
 * doubles, the same as the 32-bit shift. */
static int step_of(const Paint *p, double row0, int x) {
  int t = (int)__builtin_floor(((double)p->gx * (x - p->ox) + row0) / 65536.0);
  return (t < 0 ? 0 : t > 4095 ? 4095 : t) >> rd_ramp_shift;
}

/* The runs of [a, b) of the row's segments; the calls of a row go left to right, so each starts at the segment where the
 * last one ended (seg_at). */
static int seg_at;
static void runs_of(int y, int a, int b) {
  int i;
  for (i = seg_at; i < nseg; i++) {
    int s0 = sx[i], s1 = sx[i + 1], c = sc[i];
    if (s1 <= a) continue;
    if (s0 >= b) break;
    seg_at = i;
    if (s0 < a) s0 = a;
    if (s1 > b) s1 = b;
    const Paint *p = &paints[c_paint[c]];
    if (p->kind == RD_SOLID) { run_add(comp_index(c, 0), s1 - s0); continue; }
    double row0 = (double)p->gy * (y - p->oy) + p->g0;
    if (p->gx == 0) { run_add(comp_index(c, step_of(p, row0, 0)), s1 - s0); continue; }
    /* a gradient across the row: walk its steps, the x where each ends estimated from t, then made exact */
    int x = s0;
    while (x < s1) {
      int s = step_of(p, row0, x);
      int edge = p->gx > 0 ? (s + 1) << rd_ramp_shift : s << rd_ramp_shift;
      int xe = p->ox + (int)(((double)edge * 65536.0 - row0) / p->gx);
      if (xe <= x) xe = x + 1;
      if (xe > s1) xe = s1;
      while (xe > x + 1 && step_of(p, row0, xe - 1) != s) xe--;
      while (xe < s1 && step_of(p, row0, xe) == s) xe++;
      run_add(comp_index(c, s), xe - x);
      x = xe;
    }
  }
}

static void line_flush(void) {
  int i;
  if (!npend) return;
  if (rd_nout + npend + 2 > RD_OUT_MAX) return;
  rd_out[rd_nout++] = RD_OP_LINE | (uint32_t)pend_h;
  for (i = 0; i < npend; i++) rd_out[rd_nout++] = pend[i];
  rd_out[rd_nout++] = RD_OP_END;
  npend = 0;
}

static int page_no;
static void row(int y) {
  int i, x = 0;
  row_segments(y);
  seg_at = 0;
  ncur = 0;
  run_c = -1;
  run_w = 0;
  for (i = 0; i < pg->nlinks; i++) {
    const RdLink *k = &pg->links[i];
    if (y < k->y0 || y >= k->y1 || k->x0 < x) continue;
    runs_of(y, x, k->x0);
    run_flush();
    cur[ncur++] = RD_OP_LINK | (uint32_t)(page_no * 16 + i);
    runs_of(y, k->x0, k->x1);
    run_flush();
    cur[ncur++] = RD_OP_UNLINK;
    x = k->x1;
  }
  runs_of(y, x, RD_W);
  run_flush();
  if (npend && ncur == npend) {
    for (i = 0; i < ncur && cur[i] == pend[i]; i++) {}
    if (i == ncur) { pend_h++; rd_stat_runs -= ncur; return; }
  }
  line_flush();
  uint32_t *t = pend;
  pend = cur;
  cur = t;
  npend = ncur;
  pend_h = 1;
}

void rd_begin(int page, int state) {
  int i, j;
  page_no = page;
  pg = &rd_pages[page];
  for (i = 0; i < pg->npaints && i < 64; i++) {
    const RdPaint *q = &pg->paints[i];
    Paint *p = &paints[i];
    p->kind = q->kind; p->stop = q->stop; p->nstop = q->nstop; p->rgb = q->rgb;
    p->gx = q->gx; p->gy = q->gy; p->g0 = q->g0; p->ox = q->ox; p->oy = q->oy;
  }
  nitems = 0;
  for (i = 0; i < pg->nitems && nitems < IMAX; i++) {
    const RdItem *q = &pg->items[i];
    if (!(q->state & state)) continue;
    Item *it = &items[nitems];
    it->x0 = q->x0; it->y0 = q->y0; it->x1 = q->x1; it->y1 = q->y1; it->rt = q->rt; it->rb = q->rb;
    it->paint = q->paint; it->alpha = q->alpha; it->state = q->state; it->kind = q->kind; it->bw = q->bw; it->text = q->text;
    /* by_y0: insertion by first row, after every item with the same first row (paint order kept) */
    for (j = nitems; j > 0 && items[by_y0[j - 1]].y0 > it->y0; j--) by_y0[j] = by_y0[j - 1];
    by_y0[j] = nitems;
    nitems++;
  }
  nact = 0;
  next_y0 = 0;
  comp_reset();
  last_under = -1;
  for (i = 0; i < pg->npaints; i++) comp_new(0, i, 0, 255);
  row_y = 0;
  npend = 0;
  rd_nout = 0;
  rd_stat_spans = rd_stat_runs = rd_stat_text_runs = 0;
}

int rd_rows(int n) {
  rd_nout = 0;
  /* a row adds at most one held-back line of 1,026 words: stop before rd_out could not take it */
  while (n-- > 0 && row_y < RD_H && rd_nout + 1026 <= RD_OUT_MAX) row(row_y++);
  if (row_y >= RD_H) { line_flush(); return 0; }
  return 1;
}

/* --- HTML --------------------------------------------------------------------------------------------------------------- */
static int put(char *buf, int at, int cap, const char *s) {
  while (*s) { if (at < cap) buf[at] = *s; at++; s++; }
  return at;
}
static int putn(char *buf, int at, int cap, int v) {
  char t[12];
  int n = 0;
  do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
  while (n) { if (at < cap) buf[at] = t[--n]; else --n; at++; }
  return at;
}

/* The markup of the ops: <IMG SRC=xx.gif WIDTH=w HEIGHT=h> a run (xx the colour in hex), BR a line's end, and the links
 * <A HREF="javascript:parent.cdomApp.rsK(n)" onMouseOver="window.status=parent.cdomApp.rsO(n);return true">...</A>.
 * The host's rsHtml (host.js) writes the same, byte for byte (the differential test). */
int rd_html(const uint32_t *ops, int n, char *buf, int cap) {
  static const char hex[] = "0123456789abcdef";
  int i, at = 0, h = 1, in_link = 0;
  for (i = 0; i < n; i++) {
    uint32_t op = ops[i] & 0xff000000u, v = ops[i] & 0xffffffu;
    if (op == RD_OP_LINE) h = (int)v;
    else if (op == RD_OP_RUN) {
      char nm[3];
      nm[0] = hex[v >> 16 & 15]; nm[1] = hex[v >> 12 & 15]; nm[2] = 0;
      at = put(buf, at, cap, "<IMG SRC=");
      at = put(buf, at, cap, nm);
      at = put(buf, at, cap, ".gif WIDTH=");
      at = putn(buf, at, cap, (int)(v & 4095));
      at = put(buf, at, cap, " HEIGHT=");
      at = putn(buf, at, cap, h);
      at = put(buf, at, cap, in_link ? " BORDER=0>" : ">");
    } else if (op == RD_OP_LINK) {
      at = put(buf, at, cap, "<A HREF=\"javascript:parent.cdomApp.rsK(");
      at = putn(buf, at, cap, (int)v);
      at = put(buf, at, cap, ")\" onMouseOver=\"window.status=parent.cdomApp.rsO(");
      at = putn(buf, at, cap, (int)v);
      at = put(buf, at, cap, ");return true\">");
      in_link = 1;
    } else if (op == RD_OP_UNLINK) { at = put(buf, at, cap, "</A>"); in_link = 0; }
    else if (op == RD_OP_END) at = put(buf, at, cap, "<BR>\n");
  }
  return at;
}
