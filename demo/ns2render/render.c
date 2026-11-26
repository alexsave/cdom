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
static int c_info[CMAX];     /* a solid composite's palette colour + 1 (0: not yet known); over a gradient, -(slot + 1) */
static uint32_t c_okey[CMAX];  /* the last colour put over the composite, rgb << 8 | alpha, and the composite it made + 1 */
static int c_oval[CMAX];
/* the palette colours of a gradient composite's steps: gtab[slot * GSTEPS + step] + 1, 0 not yet known */
#define GSLOTS 64
#define GSTEPS 64
static int gtab[GSLOTS * GSTEPS];
static int nslot;
static int ncomp;
static int c_hash[CHASH];    /* composite + 1, by (parent, rgb, alpha) */
/* (composite, gradient step) -> palette colour + 1, for composites over a gradient */
#define SHASH 2048
static int s_key[SHASH];
static int s_val[SHASH];
static int s_count;

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
static int page_no;

static void comp_reset(void) {
    int i;
    ncomp = 0;
    for (i = 0; i < CHASH; i++) c_hash[i] = 0;
    for (i = 0; i < SHASH; i++) s_key[i] = 0;
    s_count = 0;
}

static int comp_new(int parent, int paint, uint32_t rgb, int alpha) {
    int c = ncomp++;
    c_parent[c] = parent;
    c_paint[c] = paint;
    c_rgb[c] = rgb;
    c_alpha[c] = alpha;
    c_info[c] = 0;
    c_okey[c] = 0;
    c_oval[c] = 0;
    if (paints[paint].kind != RD_SOLID) {
        if (nslot < GSLOTS) { int i; for (i = 0; i < GSTEPS; i++) gtab[nslot * GSTEPS + i] = 0; c_info[c] = -(++nslot); }
        else c_info[c] = -(GSLOTS + 1);   /* no slot: the hash */
    }
    return c;
}

/* The composite of rgb at alpha over composite under (each composite keeps the last answer: a text's runs and a shadow's
 * rows ask the same over and over). */
static int comp_over(int under, uint32_t rgb, int alpha) {
    uint32_t key = rgb << 8 | (uint32_t)alpha;
    if (c_okey[under] == key && c_oval[under]) return c_oval[under] - 1;
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
    c_okey[under] = key;
    c_oval[under] = c + 1;
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
    int info = c_info[c];
    if (info > 0) return info - 1;
    if (info == 0) { int v = nearest(comp_rgb(c, 0)); c_info[c] = v + 1; return v; }
    if (info > -(GSLOTS + 1) && s < GSTEPS) {
        int at = (-info - 1) * GSTEPS + s, v = gtab[at];
        if (v) return v - 1;
        v = nearest(comp_rgb(c, s));
        gtab[at] = v + 1;
        return v;
    }
    int key = (c << 12 | s) + 1;
    int k = (key ^ (key >> 7) ^ (key >> 13)) & (SHASH - 1);
    while (s_key[k]) {
        if (s_key[k] == key) return s_val[k];
        k = (k + 1) & (SHASH - 1);
    }
    int v = nearest(comp_rgb(c, s));
    if (s_count < SHASH * 3 / 4) {   /* a full table would never end a probe: past three quarters, answers are not kept */
        s_key[k] = key;
        s_val[k] = v;
        s_count++;
    }
    return v;
}

/* --- the row's segments: seg[i] = x << 16 | composite, segment i is [its x, the next one's x); seg[nseg] = RD_W << 16 ------- */
/* One word a segment and one a span: Netscape 2 pays about 35 us for every word JavaScript 1.0 reads or writes. */
#define SEGMAX 600
static int seg[SEGMAX + 1], mid[SEGMAX + 1];
static int nseg;
/* the spans of one item on this row, sorted: sp[k] = a | b << 10 | alpha << 20 ([a, b); alpha 0: opaque, span_comp) */
#define SPANMAX 512
static int sp[SPANMAX];
static int npan, span_end;
static int span_comp;           /* an opaque item's composite */
static uint32_t span_rgb;       /* a translucent item's colour */

static void span(int a, int b, int alpha) {
    if (a < 0) a = 0;
    if (b > RD_W) b = RD_W;
    if (a < span_end) a = span_end;   /* overlapping glyphs: the later one starts where the earlier ends */
    if (b <= a || npan >= SPANMAX) return;
    sp[npan++] = a | b << 10 | alpha << 20;
    span_end = b;
}

/* Merges the spans into the segments in place: the segments the spans' range [a, b) touches (found by binary search) are
 * merged with the spans into mid, and replace them; the segments after move only when the count changes. */
static void apply(void) {
    int np = npan, n = nseg, k = 0, m = 0, i, lo, hi, md, x, e, end, c, oc, cur, nxt, lastc, i0, i1, drop, delta, endall;
    int s0 = sp[0], sk = s0, a = s0 & 1023, b = (sp[np - 1] >> 10) & 1023, pk = a;
    int lu = -1, la = -1, lc = 0;
    uint32_t rgb = span_rgb;
    int opaque = span_comp;
    rd_stat_spans += np;
    lo = 0; hi = n - 1;
    while (lo < hi) { md = (lo + hi + 1) >> 1; if ((seg[md] >> 16) <= a) lo = md; else hi = md - 1; }
    i0 = lo;
    hi = n - 1;
    while (lo < hi) { md = (lo + hi + 1) >> 1; if ((seg[md] >> 16) < b) lo = md; else hi = md - 1; }
    i1 = lo;
    lastc = i0 ? seg[i0 - 1] & 65535 : -1;
    i = i0;
    cur = seg[i];
    nxt = seg[i + 1];
    x = cur >> 16;
    endall = seg[i1 + 1] >> 16;
    while (x < endall) {
        e = nxt >> 16;
        oc = cur & 65535;
        if (pk <= x) {
            int bk = (sk >> 10) & 1023, al = sk >> 20;
            end = bk < e ? bk : e;
            if (!al) c = opaque;
            else if (oc == lu && al == la) c = lc;
            else { c = comp_over(oc, rgb, al); lu = oc; la = al; lc = c; }
            if (end == bk) { k++; if (k < np) { sk = sp[k]; pk = sk & 1023; } else pk = RD_W; }
        } else {
            end = pk < e ? pk : e;
            c = oc;
        }
        if (c != lastc) { mid[m++] = x << 16 | c; lastc = c; }
        x = end;
        if (x == e) { i++; cur = nxt; if (i < n) nxt = seg[i + 1]; }
    }
    /* the segment after the range, when it has the colour the range ended in, merges into it */
    drop = i1 + 1 < n && (seg[i1 + 1] & 65535) == lastc;
    delta = m - (i1 - i0 + 1 + drop);
    if (delta > 0) for (i = n; i > i1 + drop; i--) seg[i + delta] = seg[i];
    else if (delta < 0) for (i = i1 + 1 + drop; i <= n; i++) seg[i + delta] = seg[i];
    for (i = 0; i < m; i++) seg[i0 + i] = mid[i];
    nseg = n + delta;
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
    int i, j, n = npan, end = span_end, last = rd_levels - 1, al = t->alpha, first = t->first, nt = t->n;
    const uint32_t *places = pg->places;
    for (i = 0; i < nt; i++) {
        uint32_t p = places[first + i], g = rd_glyphs[p >> 20];
        int px = (int)(p & 1023), gy = y - (int)(p >> 10 & 1023);
        if (gy < 0 || gy >= (int)(g >> 8 & 255)) continue;
        int row = (int)(g >> 16) + gy, j1 = rd_glyph_rows[row + 1];
        for (j = rd_glyph_rows[row]; j < j1; j++) {
            uint32_t u = rd_glyph_runs[j];
            int a = px + (int)(u & 255), b = a + (int)(u >> 8 & 255), lv = al * (int)(u >> 16) / last;
            if (b > RD_W) b = RD_W;
            if (a < end) a = end;   /* overlapping glyphs: the later one starts where the earlier ends */
            if (b <= a || n >= SPANMAX) continue;
            sp[n++] = a | b << 10 | (lv >= 255 ? 255 : lv < 1 ? 1 : lv) << 20;
            end = b;
        }
    }
    npan = n;
    span_end = end;
}

/* The active items of row y, in paint order: the list of the row before, less the items that ended, plus the ones that
 * start here. */
static void activate(int y) {
    int i, n = 0;
    for (i = 0; i < nact; i++) if (items[act[i]].y1 > y) act[n++] = act[i];
    nact = n;
    while (next_y0 < nitems && items[by_y0[next_y0]].y0 <= y) {
        int k = by_y0[next_y0++], j;
        if (items[k].y1 <= y) continue;   /* ended above y: a batch that starts lower down (rd_at) passes it */
        j = nact++;
        while (j > 0 && act[j - 1] > k) { act[j] = act[j - 1]; j--; }
        act[j] = k;
    }
}

static void row_segments(int y) {
    int i;
    seg[0] = 0;   /* composite 0: the page's paper, made by rd_begin */
    seg[1] = RD_W << 16;
    nseg = 1;
    activate(y);
    for (i = 0; i < nact; i++) {
        const Item *it = &items[act[i]];
        int l, r;
        npan = 0;
        span_end = 0;
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
static int row_y;
static int prev_at = -1, prev_n, prev_h;   /* the batch's last line: where its LINE word is in rd_out, its op count, its hash */

/* The row's ops into out: its segments' colours as runs, neighbours of one colour merged, cut at the link regions' edges
 * (LINK and UNLINK round the runs inside one).  Answers the count; *hash gets a hash of the ops. */
static int row_runs(int y, uint32_t *out, int *hash) {
    int n = 0, i, rc = -1, rw = 0, nl = 0, li = 0, cut, x, s1, c, v, s, h = 0, cur, shift = rd_ramp_shift;
    int lx[8], lop[8];   /* the row's link edges, in x order, and the op each one opens or closes */
    uint32_t w;
    for (i = 0; i < pg->nlinks && nl < 8; i++) {
        const RdLink *k = &pg->links[i];
        if (y < k->y0 || y >= k->y1) continue;
        lx[nl] = k->x0; lop[nl] = (int)(RD_OP_LINK | (uint32_t)(page_no * 16 + i)); nl++;
        lx[nl] = k->x1; lop[nl] = (int)RD_OP_UNLINK; nl++;
    }
    cut = nl ? lx[0] : RD_W;
    cur = seg[0];
    for (i = 0; i < nseg; i++) {
        int nx = seg[i + 1];
        x = cur >> 16;
        s1 = nx >> 16;
        c = cur & 65535;
        cur = nx;
        int info = c_info[c], kind = info >= 0 ? RD_SOLID : RD_LINEAR, across = 0, ox = 0, edge0 = 0;
        double row0 = 0, gx = 0;
        if (kind != RD_SOLID) {
            const Paint *p = &paints[c_paint[c]];
            row0 = (double)p->gy * (y - p->oy) + p->g0;
            gx = p->gx;
            ox = p->ox;
            across = p->gx != 0;
        }
        while (x < s1) {
            /* the piece from x: to the segment's end, the gradient step's end, or the next link edge */
            int xe = s1;
            if (kind == RD_SOLID) v = info > 0 ? info - 1 : (info = comp_index(c, 0) + 1) - 1;
            else {
                int t = (int)__builtin_floor((gx * (x - ox) + row0) / 65536.0);
                s = (t < 0 ? 0 : t > 4095 ? 4095 : t) >> shift;
                if (across) {
                    edge0 = gx > 0 ? (s + 1) << shift : s << shift;
                    xe = ox + (int)(((double)edge0 * 65536.0 - row0) / gx);
                    if (xe <= x) xe = x + 1;
                    if (xe > s1) xe = s1;
                    for (;;) {   /* made exact: the last x of step s is xe - 1 */
                        if (xe > x + 1) {
                            t = (int)__builtin_floor((gx * (xe - 1 - ox) + row0) / 65536.0);
                            if (((t < 0 ? 0 : t > 4095 ? 4095 : t) >> shift) != s) { xe--; continue; }
                        }
                        break;
                    }
                    while (xe < s1) {
                        t = (int)__builtin_floor((gx * (xe - ox) + row0) / 65536.0);
                        if (((t < 0 ? 0 : t > 4095 ? 4095 : t) >> shift) != s) break;
                        xe++;
                    }
                }
                v = comp_index(c, s);
            }
            if (xe > cut) xe = cut;
            if (v != rc) {
                if (rc >= 0 && rw > 0 && n < 1020) { w = RD_OP_RUN | (uint32_t)rc << 12 | (uint32_t)rw; out[n++] = w; h = (h << 5) ^ (int)((uint32_t)h >> 27) ^ (int)w; }
                rc = v;
                rw = 0;
            }
            rw += xe - x;
            x = xe;
            while (x == cut && li < nl) {   /* a link edge: the run so far ends, the op goes out */
                if (rc >= 0 && rw > 0 && n < 1020) { w = RD_OP_RUN | (uint32_t)rc << 12 | (uint32_t)rw; out[n++] = w; h = (h << 5) ^ (int)((uint32_t)h >> 27) ^ (int)w; }
                rc = -1;
                rw = 0;
                if (n < 1020) { out[n++] = (uint32_t)lop[li]; h = (h << 5) ^ (int)((uint32_t)h >> 27) ^ lop[li]; }
                li++;
                cut = li < nl ? lx[li] : RD_W;
            }
        }
    }
    if (rc >= 0 && rw > 0 && n < 1020) { w = RD_OP_RUN | (uint32_t)rc << 12 | (uint32_t)rw; out[n++] = w; h = (h << 5) ^ (int)((uint32_t)h >> 27) ^ (int)w; }
    *hash = h;
    return n;
}

/* A row: its ops written in place as a line of rd_out, or, when they are the batch's last line's (the same count and hash,
 * then word for word), that line one row taller. */
static void row(int y) {
    int i, h, at = rd_nout, n;
    row_segments(y);
    n = row_runs(y, rd_out + at + 1, &h);
    if (prev_at >= 0 && n == prev_n && h == prev_h) {
        for (i = 1; i <= n && rd_out[at + i] == rd_out[prev_at + i]; i++) {}
        if (i > n) { rd_out[prev_at]++; return; }
    }
    rd_stat_runs += n;
    rd_out[at] = RD_OP_LINE | 1u;
    rd_out[at + 1 + n] = RD_OP_END;
    rd_nout = at + n + 2;
    prev_at = at;
    prev_n = n;
    prev_h = h;
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
    nslot = 0;
    for (i = 0; i < pg->npaints; i++) comp_new(0, i, 0, 255);
    row_y = 0;
    prev_at = -1;
    rd_nout = 0;
    rd_stat_spans = rd_stat_runs = rd_stat_text_runs = 0;
}

int rd_rows(int n) {
    rd_nout = 0;
    prev_at = -1;   /* no line spans two batches, so a batch's markup stands alone */
    /* a row adds at most a line of 1,022 words: stop before rd_out could not take it */
    while (n-- > 0 && row_y < RD_H && rd_nout + 1022 <= RD_OUT_MAX) row(row_y++);
    return row_y < RD_H;
}

void rd_at(int y) { row_y = y; }
int rd_y(void) { return row_y; }

int rd_dirty(int page) {
    const RdPage *p = &rd_pages[page];
    int i, y0 = RD_H, y1 = 0;
    for (i = 0; i < p->nitems; i++)
        if (p->items[i].state != 3) {
            if (p->items[i].y0 < y0) y0 = p->items[i].y0;
            if (p->items[i].y1 > y1) y1 = p->items[i].y1;
        }
    return y0 < y1 ? y0 * 1024 + y1 : 0;
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
