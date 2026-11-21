/* The box description and the span renderer of Netscape 2's self-painting page (docs/descent.md, "Netscape 2 paints itself").
 * A page is a list of items in paint order (back to front); each row of the picture is made from the spans of the items that
 * cross it, never pixel by pixel, and comes out as runs of one palette colour, which the host writes as stretched 1x1 GIFs.
 * The layout is fixed C structs (tools/gen_ns2_render.py writes page_data.c), no text format. */
#ifndef RENDER_H
#define RENDER_H
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef short int16_t;
typedef unsigned uint32_t;
typedef int int32_t;

#define RD_W 592
#define RD_H 320

/* A paint: a solid colour, or a linear gradient: t = (gx (x - ox) + gy (y - oy) + g0) >> 16 runs 0 to 4096 along the gradient line, and
 * the stops (at, rgb) are its colours there. */
enum { RD_SOLID = 0, RD_LINEAR = 1 };
typedef struct { uint16_t at; uint32_t rgb; } RdStop;
typedef struct { uint8_t kind; uint16_t stop, nstop; uint32_t rgb; int32_t gx, gy, g0; int16_t ox, oy; } RdPaint;

/* An item: a rounded rectangle [x0, x1) x [y0, y1) with corner radius rt at the top and rb at the bottom (a circle is a
 * square of radius half its side), painted with paint at alpha (255 opaque, less blends over what is under it); state says
 * in which states of the page it is drawn (bit 0 normal, bit 1 hover).  kind RD_TEXT draws texts[text] instead (glyphs over
 * what is under, the text's colour at each glyph run's level); kind RD_RING only the band of width bw inside the edge. */
enum { RD_BOX = 0, RD_TEXT = 1, RD_RING = 2 };
typedef struct { int16_t x0, y0, x1, y1; uint8_t rt, rb; uint8_t paint, alpha, state, kind, bw; uint16_t text; } RdItem;

/* A glyph is one word, w | h << 8 | row0 << 16: w x h, its rows' runs from rd_glyph_rows[row0 + r] to
 * rd_glyph_rows[row0 + r + 1]; a run is one word, x | n << 8 | level << 16: n pixels from x at a level (1 to rd_levels - 1:
 * the share of the text's colour).  A place is one word, x | y << 10 | glyph << 20: a glyph's top left.  One word each,
 * since Netscape 2 pays for every word read, and a struct's fields are a read each. */
/* A text: places [first, first + n), its colour and alpha. */
typedef struct { uint16_t first, n; uint32_t rgb; uint8_t alpha; } RdText;
/* A link region: the runs inside it are wrapped in a link to page target. */
typedef struct { int16_t x0, y0, x1, y1; uint8_t target; } RdLink;

typedef struct {
  const RdItem *items; int nitems;
  const RdPaint *paints; int npaints; const RdStop *stops;
  const RdText *texts; const uint32_t *places;
  const RdLink *links; int nlinks;
} RdPage;

extern const uint32_t rd_palette[];
extern const int rd_npalette;
extern const uint16_t rd_grid_start[];
extern const uint8_t rd_grid_cand[];
extern const uint32_t rd_glyphs[];
extern const uint16_t rd_glyph_rows[];
extern const uint32_t rd_glyph_runs[];
extern const int rd_levels;
extern const RdPage rd_pages[];
extern const int rd_npages;

/* The output: a stream of 32-bit words.  A line is RD_OP_LINE | height, its pieces, then RD_OP_END (a BR); a piece is
 * RD_OP_RUN | colour << 12 | width (an IMG), or RD_OP_LINK | link (the A that opens; link = page * 16 + index) and
 * RD_OP_UNLINK (the A that closes).  Identical rows make one line of their count's height. */
#define RD_OP_LINE (1u << 24)
#define RD_OP_RUN (2u << 24)
#define RD_OP_LINK (3u << 24)
#define RD_OP_UNLINK (4u << 24)
#define RD_OP_END (5u << 24)
#define RD_OUT_MAX 4096

/* rd_begin(page, state) starts a picture; rd_rows(n) renders up to n more rows into rd_out (rd_nout words, whole lines;
 * identical rows of the batch make one line) and answers 1 while rows remain. */
void rd_begin(int page, int state);
int rd_rows(int n);
/* rd_at(y): the next batch starts at row y; rd_y(): the row the next batch starts at; rd_dirty(page): the rows whose items
 * differ between the page's states, y0 * 1024 + y1 (0: none), which a host's cache of batches draws again on a change. */
void rd_at(int y);
int rd_y(void);
int rd_dirty(int page);
extern uint32_t rd_out[RD_OUT_MAX];
extern int rd_nout;
/* Statistics of the last picture: spans applied, segments and runs made. */
extern int rd_stat_spans, rd_stat_runs, rd_stat_text_runs;
/* The gradient's step: t >> rd_ramp_shift (of 4096) is one colour. */
extern int rd_ramp_shift;
/* 1: leave the text out (the measure of what text costs) */
extern int rd_skip_text;

/* The HTML of an op stream (the host's job in the browser; here for the native build and the differential test). */
int rd_html(const uint32_t *ops, int n, char *buf, int cap);
#endif
