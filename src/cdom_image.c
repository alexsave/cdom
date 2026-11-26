/* cdom_image: the entry points, the EXIF orientation, and the streaming scaler (cdom_image.h). The formats are
 * cdom_image_jpeg.c and cdom_image_png.c; libjpeg's error exit is cdom_image_sjlj.c. */
#include <stddef.h>
#include <string.h>
#include "cdom_image_int.h"

static int is_png(const uint8_t *p, uint32_t len) { return len >= 8 && !memcmp(p, "\x89PNG\r\n\x1a\n", 8); }
static int is_jpeg(const uint8_t *p, uint32_t len) { return len >= 3 && p[0] == 0xff && p[1] == 0xd8 && p[2] == 0xff; }

uint32_t cd_image_probe(const uint8_t *p, uint32_t len, cd_image_info *info) {
    memset(info, 0, sizeof *info);
    info->orientation = 1;
    if (is_jpeg(p, len)) return cd_image_jpeg_probe(p, len, info);
    if (is_png(p, len)) return cd_image_png_probe(p, len, info);
    return CD_IMAGE_NOT_IMAGE;
}

/* ---- the fed decoder ---- */

static int bad_box(uint32_t w, uint32_t h) { return !w || !h || w > CD_IMAGE_BOX_MAX || h > CD_IMAGE_BOX_MAX; }

static void dec_init(cd_image_dec *d, uint32_t size, uint32_t max_w, uint32_t max_h) {
    memset(d, 0, sizeof *d);
    d->size = size; d->box_w = max_w; d->box_h = max_h; d->status = CD_IMAGE_MORE;
    d->info.orientation = 1;
}

/* The next n bytes of the file at p (the room's, or all of the rest where they lie), taken already counted. Before
 * the format is known, the first bytes wait at the room's front (min(8, size) of them say it), and are then handed
 * to the format from the start. */
static uint32_t feed(cd_image_dec *d, const uint8_t *p, uint32_t n) {
    if (!d->format) {
        const uint8_t *h = p;
        uint32_t hn = n;
        if (d->buf) { d->used += n; h = d->buf; hn = d->used; }
        if (hn < (d->size < 8 ? d->size : 8)) return CD_IMAGE_MORE;
        if (is_jpeg(h, hn)) d->format = CD_IMAGE_JPEG;
        else if (is_png(h, hn)) d->format = CD_IMAGE_PNG;
        else return CD_IMAGE_NOT_IMAGE;
        if (d->buf) { p = d->buf; n = d->used; d->used = 0; }
    }
    return d->format == CD_IMAGE_JPEG ? cd_image_jpeg_put(d, p, n) : cd_image_png_put(d, p, n);
}

/* What the decoder holds, freed; the block itself is the caller's. */
static void dec_free(cd_image_dec *d) {
    if (d->format == CD_IMAGE_JPEG) cd_image_jpeg_end(d);
    else if (d->format == CD_IMAGE_PNG) cd_image_png_end(d);
    cd_free(d->walk.exif); d->walk.exif = 0;
    cd_free(d->rgba); d->rgba = 0;
    cd_free(d->buf); d->buf = 0;
}

uint32_t cd_image_begin(uint32_t size, uint32_t max_w, uint32_t max_h, cd_image_dec **out) {
    *out = 0;
    if (bad_box(max_w, max_h)) return CD_IMAGE_BAD_BOX;
    cd_image_dec *d = cd_alloc_try(sizeof *d);
    if (!d) return CD_IMAGE_NO_ROOM;
    dec_init(d, size, max_w, max_h);
    const uint32_t most = CD_IMAGE_PIECE + CD_IMAGE_JPEG_KEEP;
    d->cap = size < most ? size : most;
    d->buf = cd_alloc_try(d->cap ? d->cap : 1);
    if (!d->buf) { cd_free(d); return CD_IMAGE_NO_ROOM; }
    *out = d;
    return CD_IMAGE_OK;
}

static uint32_t room_max(const cd_image_dec *d) {
    if (d->status != CD_IMAGE_MORE || d->step) return 0;
    uint32_t m = d->cap - d->used, left = d->size - d->taken;
    if (m > CD_IMAGE_PIECE) m = CD_IMAGE_PIECE;
    return m < left ? m : left;
}

uint8_t *cd_image_room(cd_image_dec *d, uint32_t *max) {
    *max = room_max(d);
    return d->buf + d->used;
}

uint32_t cd_image_put(cd_image_dec *d, uint32_t n) {
    if (d->status != CD_IMAGE_MORE) return d->status;
    if (n > room_max(d)) __builtin_trap();               /* more than the room: the caller wrote past it */
    d->step = 0;
    d->taken += n;
    d->status = feed(d, d->buf + d->used, n);
    return d->status;
}

void cd_image_take(cd_image_dec *d, uint8_t **rgba, uint32_t *w, uint32_t *h) {
    *rgba = d->rgba; *w = d->rgba ? d->w : 0; *h = d->rgba ? d->h : 0;
    d->rgba = 0;
}

uint32_t cd_image_dec_warnings(const cd_image_dec *d) { return d->warnings; }

void cd_image_end(cd_image_dec *d) {
    if (!d) return;
    dec_free(d);
    cd_free(d);
}

/* One put of the whole file where it lies: the decoder's block on the stack, no room (the format reads p itself),
 * and the steps that take no bytes run here. */
uint32_t cd_image_resize(const uint8_t *p, uint32_t len, uint32_t max_w, uint32_t max_h, uint8_t **rgba,
                         uint32_t *w, uint32_t *h) {
    return cd_image_decode(p, len, max_w, max_h, rgba, w, h, 0);
}
uint32_t cd_image_decode(const uint8_t *p, uint32_t len, uint32_t max_w, uint32_t max_h, uint8_t **rgba, uint32_t *w,
                         uint32_t *h, uint32_t *warnings) {
    *rgba = 0; *w = 0; *h = 0;
    if (bad_box(max_w, max_h)) return CD_IMAGE_BAD_BOX;
    cd_image_dec d;
    dec_init(&d, len, max_w, max_h);
    d.taken = len;
    uint32_t st = feed(&d, p, len);
    while (st == CD_IMAGE_MORE && d.step) { d.step = 0; st = feed(&d, p + len, 0); }
    if (st == CD_IMAGE_MORE) st = CD_IMAGE_CORRUPT;      /* not reached: with every byte in, only steps answer MORE */
    if (st == CD_IMAGE_OK) cd_image_take(&d, rgba, w, h);
    if (warnings) *warnings = d.warnings;
    dec_free(&d);
    return st;
}

/* ---- the size ---- */

/* The largest size inside bw x bh with the aspect of w x h, never larger than w x h: the side the box binds is the
 * box's, and the other is rounded to nearest (halves up), at least 1. Shown sizes: the caller swaps for 5 to 8. */
static void fit_box(uint32_t w, uint32_t h, uint32_t bw, uint32_t bh, uint32_t *dw, uint32_t *dh) {
    if (w <= bw && h <= bh) { *dw = w; *dh = h; return; }
    if ((uint64_t)w * bh >= (uint64_t)h * bw) {         /* the width binds: w/h >= bw/bh */
        uint64_t v = ((uint64_t)h * bw * 2 + w) / ((uint64_t)w * 2);
        *dw = bw; *dh = v ? (uint32_t)v : 1;
    } else {
        uint64_t v = ((uint64_t)w * bh * 2 + h) / ((uint64_t)h * 2);
        *dh = bh; *dw = v ? (uint32_t)v : 1;
    }
}

void cd_image_target(uint32_t sw, uint32_t sh, uint32_t o, uint32_t bw, uint32_t bh, uint32_t *dw, uint32_t *dh) {
    if (o >= 5 && o <= 8) fit_box(sw, sh, bh, bw, dw, dh);   /* the shown image is sh x sw: its box, as stored */
    else fit_box(sw, sh, bw, bh, dw, dh);
}

/* ---- EXIF ---- */

static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }
static uint32_t rd16(const uint8_t *p, int le) { return le ? (uint32_t)p[1] << 8 | p[0] : be16(p); }
static uint32_t rd32(const uint8_t *p, int le) {
    return le ? (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]
              : (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* The segments before the scan, each FF, marker, 2-byte length (counting itself); the first APP1 that starts
 * "Exif\0\0" (one that does not, such as XMP's, is passed over) holds a TIFF header and IFD0, whose entry 0x0112
 * (SHORT) is the orientation. Every offset is checked against the segment's room, in 64 bits. */
uint32_t cd_image_orientation(const uint8_t *p, uint32_t len) {
    if (len < 4 || p[0] != 0xff || p[1] != 0xd8) return 1;
    uint64_t at = 2;
    while (at + 4 <= len && p[at] == 0xff) {
        uint32_t marker = p[at + 1], seg = be16(p + at + 2);
        if (marker == 0xff) { at++; continue; }                 /* fill bytes */
        if (marker == 0xda || marker == 0xd9 || seg < 2) break; /* the scan, or the end: no more headers */
        if (marker == 0xe1 && seg >= 16 && at + 2 + seg <= len && !memcmp(p + at + 4, "Exif\0\0", 6))
            return cd_image_exif_orientation(p + at + 10, seg - 8);   /* the TIFF header, and the bytes from it to the end */
        at += 2 + (uint64_t)seg;
    }
    return 1;
}

uint32_t cd_image_exif_orientation(const uint8_t *t, uint64_t room) {
    int le = t[0] == 'I';
    if ((t[0] != 'I' && t[0] != 'M') || t[1] != t[0] || rd16(t + 2, le) != 42) return 1;
    uint64_t ifd = rd32(t + 4, le);
    if (ifd + 2 > room) return 1;
    uint32_t count = rd16(t + ifd, le);
    for (uint32_t i = 0; i < count && ifd + 2 + 12 * (uint64_t)(i + 1) <= room; i++) {
        const uint8_t *e = t + ifd + 2 + 12 * (uint64_t)i; /* tag, type, count, value */
        if (rd16(e, le) == 0x0112 && rd16(e + 2, le) == 3) {
            uint32_t o = rd16(e + 8, le);
            return o >= 1 && o <= 8 ? o : 1;
        }
    }
    return 1;
}

/* ---- the scaler ---- */

int cd_image_scaler_init(cd_image_scaler *s, uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh, uint32_t o) {
    memset(s, 0, sizeof *s);
    if (o < 1 || o > 8) o = 1;
    s->sw = sw; s->sh = sh; s->dw = dw; s->dh = dh; s->o = o; s->step = 4;
    s->ow = o >= 5 ? dh : dw;
    s->oh = o >= 5 ? dw : dh;
    uint64_t out = (uint64_t)dw * dh * 4;
    if (out > 0xffffffffu) return 0;
    s->out = cd_alloc_try((uint32_t)out);
    s->acc = cd_alloc_try(dw * 3 * (uint32_t)sizeof(uint64_t) + (CDOM_IMAGE_SIMD ? dw * 3 : 0));
    s->hrow = cd_alloc_try(dw * 3 * (uint32_t)sizeof(uint32_t));
    if (!s->out || !s->acc || !s->hrow) { cd_image_scaler_free(s); return 0; }
    memset(s->acc, 0, (size_t)dw * 3 * sizeof(uint64_t));
    return 1;
}

void cd_image_scaler_free(cd_image_scaler *s) {
    cd_free(s->out); cd_free(s->acc); cd_free(s->hrow);
    s->out = 0; s->acc = 0; s->hrow = 0;
}

/* Exact area average, in integers. Scale both axes so that source pixel c covers [c*dw, (c+1)*dw) and output pixel x
 * covers [x*sw, (x+1)*sw): the overlap of the two is an integer, and the weights of one output pixel sum to sw
 * (across) and sh (down). A pixel is the weighted sum divided by sw*sh, rounded to nearest with halves up, in
 * integers: (2a + t) / 2t. The across sum is at most 65535*255 and fits 32 bits; the down sum, at most
 * 65535*65535*255 (under 2^40), fits 64 with room for the doubling. */

/* Output row y, finished: the sums in acc, divided, stored where the orientation puts them (an offset and a step
 * in bytes; the offset is only made a pointer while it is inside the image). */
static void emit(cd_image_scaler *s, uint32_t y) {
    const uint32_t dw = s->dw, dh = s->dh, ow = s->ow, oh = s->oh;
    const uint64_t t = (uint64_t)s->sw * s->sh;
#if !CDOM_IMAGE_SIMD
    const uint64_t t2 = t * 2;
#endif
    ptrdiff_t at, step;
    switch (s->o) {
        case 2: at = ((ptrdiff_t)y * ow + dw - 1) * 4; step = -4; break;
        case 3: at = ((ptrdiff_t)(dh - 1 - y) * ow + dw - 1) * 4; step = -4; break;
        case 4: at = (ptrdiff_t)(dh - 1 - y) * ow * 4; step = 4; break;
        case 5: at = (ptrdiff_t)y * 4; step = (ptrdiff_t)ow * 4; break;
        case 6: at = (ptrdiff_t)(ow - 1 - y) * 4; step = (ptrdiff_t)ow * 4; break;
        case 7: at = ((ptrdiff_t)(oh - 1) * ow + (ow - 1 - y)) * 4; step = -(ptrdiff_t)ow * 4; break;
        case 8: at = ((ptrdiff_t)(oh - 1) * ow + y) * 4; step = -(ptrdiff_t)ow * 4; break;
        default: at = (ptrdiff_t)y * ow * 4; step = 4; break;
    }
    const uint32_t ch = s->step == 1 ? 1 : 3;   /* sums a pixel: grey's one is R, G and B */
#if CDOM_IMAGE_SIMD
    const uint8_t *q = (const uint8_t *)(s->acc + (size_t)dw * 3);
    cd_image_scaler_div((uint8_t *)q, s->acc, dw * ch, t);
    if (ch == 1)
        for (uint32_t x = 0; x < dw; x++, at += step, q++) {
            uint8_t *d = s->out + at;
            d[0] = d[1] = d[2] = q[0]; d[3] = 255;
        }
    else
        for (uint32_t x = 0; x < dw; x++, at += step, q += 3) {
            uint8_t *d = s->out + at;
            d[0] = q[0]; d[1] = q[1]; d[2] = q[2]; d[3] = 255;
        }
#else
    const uint64_t *a = s->acc;
    if (ch == 1)
        for (uint32_t x = 0; x < dw; x++, at += step, a++) {
            uint8_t *d = s->out + at;
            uint64_t v = (a[0] * 2 + t) / t2;
            d[0] = v > 255 ? 255 : (uint8_t)v;
            d[1] = d[2] = d[0]; d[3] = 255;
        }
    else
        for (uint32_t x = 0; x < dw; x++, at += step, a += 3) {
            uint8_t *d = s->out + at;
            for (int k = 0; k < 3; k++) {
                uint64_t v = (a[k] * 2 + t) / t2;
                d[k] = v > 255 ? 255 : (uint8_t)v;
            }
            d[3] = 255;
        }
#endif
    memset(s->acc, 0, (size_t)dw * ch * sizeof(uint64_t));
}

void cd_image_scaler_row(cd_image_scaler *s, const uint8_t *px) {
    if (s->row >= s->sh) return;
    s->row++;
    const uint32_t sw = s->sw, dw = s->dw, dh = s->dh, sh = s->sh;
    uint32_t *h = s->hrow;
    const uint32_t n = s->step == 1 ? dw : dw * 3;   /* the sums: grey's one a pixel */
    if (s->step == 1) cd_image_scaler_across_grey(h, px, sw, dw);   /* grey rows, a byte a pixel (the PNG path) */
    else if (s->step == 3) cd_image_scaler_across_rgb(h, px, sw, dw);   /* RGB rows as they are (the scalar build's PNG path) */
    else cd_image_scaler_across(h, px, sw, dw);
    /* down: this source row covers [row*dh, (row+1)*dh) in units of 1/dh; output row oy ends at (oy+1)*sh */
    uint64_t rend = (uint64_t)s->row * dh;
    while (s->ypos < rend) {
        uint64_t yend = (uint64_t)(s->oy + 1) * sh;
        uint64_t b = rend < yend ? rend : yend;
        uint64_t wgt = b - s->ypos;
        cd_image_scaler_down(s->acc, h, wgt, n);
        s->ypos = b;
        if (b == yend) { emit(s, s->oy); s->oy++; }
    }
}

uint8_t *cd_image_scaler_take(cd_image_scaler *s) {
    uint8_t *o = s->oy == s->dh ? s->out : 0;
    if (o) s->out = 0;
    cd_image_scaler_free(s);
    return o;
}
