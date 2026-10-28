/* The escape-hatch demo: a Chart.js line chart on the canvas chart.cdt declares with
 * <canvas ref="chart" host>, driven from C through the one generic HOST op (cdom.h). The
 * template renders the page and the point list; this file holds the actions and every call into
 * the widget. The same C renders the page on the server (apps/chart/server.c), where host calls
 * reach the C sink: a PROP leaves its attribute in the HTML and the rest are counted. */
#include "chart.h"

static const char *const COLORS[] = {"#d33", "#36c", "#2a2", "#c80"};
#define NCOLORS 4u

/* every string a host call names, interned once */
enum { K_CHART, K_TYPE, K_LINE, K_DATA, K_LABELS, K_DATASETS, K_LABEL, K_Y, K_BORDER, K_TENSION, K_OPTIONS,
       K_ANIMATION, K_MAINTAIN, K_SET_DATA, K_SET_LABELS, K_SET_BORDER, K_SET_TENSION, K_UPDATE, K_DESTROY,
       K_CHECKED, K_PLUGINS, K_LEGEND, K_DISPLAY, K_COLOR0, NK = K_COLOR0 + NCOLORS };
static const char *const KS[K_COLOR0] = {"Chart", "type", "line", "data", "labels", "datasets", "label", "y",
  "borderColor", "tension", "options", "animation", "maintainAspectRatio", "data.datasets.0.data",
  "data.labels", "data.datasets.0.borderColor", "data.datasets.0.tension", "update", "destroy", "checked",
  "plugins", "legend", "display"};
static cd_str K[NK];

extern uint32_t cd_seed;
static uint32_t rnd(Plot *s) { /* xorshift32 */
  s->rng ^= s->rng << 13; s->rng ^= s->rng >> 17; s->rng ^= s->rng << 5;
  return s->rng % 100;
}
static void push_point(Plot *s) {
  Point *p = Plot_points_push(s);
  p->x = s->next_x++;
  p->y = rnd(s);
}

/* writes go through chart.h's setters: they mark what the template reads (sync() below compares
 * the widget's own copy, so it needs no marks) */
void add_point(Plot *s, const cd_event_rec *e) { (void)e; push_point(s); }
void recolor(Plot *s, const cd_event_rec *e) { (void)e; Plot_set_color(s, (s->color + 1) % NCOLORS); }
void destroy(Plot *s, const cd_event_rec *e) { (void)e; Plot_set_mounted(s, 0); }
void remount(Plot *s, const cd_event_rec *e) { (void)e; Plot_set_mounted(s, 1); }
/* the button and the checkbox both flip the state; PROP then sets the box from it */
void toggle_smooth(Plot *s, const cd_event_rec *e) { (void)e; Plot_set_smooth(s, !s->smooth); }
/* a DOM CustomEvent named cdping on the paragraph (on:cdping): e->key is the name's string id */
void ping(Plot *s, const cd_event_rec *e) { (void)e; Plot_set_pings(s, s->pings + 1); }

/* ---- the widget: what it shows, and the host calls that bring it to the state ---- */
static struct { uint32_t live, n, color, smooth; } shown;
static uint32_t checked = 2; /* the checkbox's last PROP; 2: never sent */

static void v_labels(const Plot *s) { /* the x values: an ARRAY of U32 */
  cd_v_arr(s->points_n);
  for (uint32_t i = 0; i < s->points_n; i++) cd_v_u32(s->points[i].x);
}
static void v_data(const Plot *s) { /* the y values: F64S, a Float64Array in JS */
  double *y = cd_alloc(s->points_n * 8 + 8);
  for (uint32_t i = 0; i < s->points_n; i++) y[i] = s->points[i].y;
  cd_v_f64s(y, s->points_n);
  cd_free(y);
}
static double tension(const Plot *s) { return s->smooth ? 0.4 : 0; }

/* new Chart(canvas, {type: "line", data: {labels, datasets: [{label, data, borderColor, tension}]},
 *                    options: {animation: false, maintainAspectRatio: false,
 *                              plugins: {legend: {display: false}}}}) */
static void mount(const Plot *s) {
  cd_host(page_v.chart, CD_HOST_MOUNT, K[K_CHART]);
  cd_v_obj(3);
  cd_v_key(K[K_TYPE]); cd_v_str(K[K_LINE]);
  cd_v_key(K[K_DATA]);
  cd_v_obj(2);
  cd_v_key(K[K_LABELS]); v_labels(s);
  cd_v_key(K[K_DATASETS]);
  cd_v_arr(1);
  cd_v_obj(4);
  cd_v_key(K[K_LABEL]); cd_v_str(K[K_Y]);
  cd_v_key(K[K_DATA]); v_data(s);
  cd_v_key(K[K_BORDER]); cd_v_str(K[K_COLOR0 + s->color]);
  cd_v_key(K[K_TENSION]); cd_v_f64(tension(s));
  cd_v_key(K[K_OPTIONS]);
  cd_v_obj(3);
  cd_v_key(K[K_ANIMATION]); cd_v_bool(0);
  cd_v_key(K[K_MAINTAIN]); cd_v_bool(0);
  cd_v_key(K[K_PLUGINS]);
  cd_v_obj(1);
  cd_v_key(K[K_LEGEND]);
  cd_v_obj(1);
  cd_v_key(K[K_DISPLAY]); cd_v_bool(0);
  cd_host_end();
}

static void set_begin(uint32_t path) { cd_host(page_v.chart, CD_HOST_SET, K[path]); }

/* Brings the widget and the checkbox to the state; called after every render. The points only
 * ever grow at the end, so their count says whether they changed. */
static void sync(const Plot *s) {
  if (checked != s->smooth) {
    cd_host(page_v.smooth, CD_HOST_PROP, K[K_CHECKED]);
    cd_v_bool((int)s->smooth);
    cd_host_end();
    checked = s->smooth;
  }
  if (!s->mounted) {
    if (shown.live) {
      cd_host(page_v.chart, CD_HOST_UNMOUNT, K[K_DESTROY]);
      cd_v_null();
      cd_host_end();
      shown.live = 0;
    }
    return;
  }
  if (!shown.live) {
    mount(s);
    shown.live = 1;
    shown.n = s->points_n;
    shown.color = s->color;
    shown.smooth = s->smooth;
    return;
  }
  int dirty = 0;
  if (shown.n != s->points_n) {
    set_begin(K_SET_LABELS); v_labels(s); cd_host_end();
    set_begin(K_SET_DATA); v_data(s); cd_host_end();
    shown.n = s->points_n;
    dirty = 1;
  }
  if (shown.color != s->color) {
    set_begin(K_SET_BORDER); cd_v_str(K[K_COLOR0 + s->color]); cd_host_end();
    shown.color = s->color;
    dirty = 1;
  }
  if (shown.smooth != s->smooth) {
    set_begin(K_SET_TENSION); cd_v_f64(tension(s)); cd_host_end();
    shown.smooth = s->smooth;
    dirty = 1;
  }
  if (dirty) {
    cd_host(page_v.chart, CD_HOST_CALL, K[K_UPDATE]);
    cd_v_arr(0);
    cd_host_end();
  }
}

void cd_on_event(const cd_event_rec *e) {
  page_dispatch(e);
  page_render(&S);
  sync(&S);
}

void cd_main(void) {
  for (uint32_t i = 0; i < K_COLOR0; i++) K[i] = cd_internz(KS[i]);
  for (uint32_t i = 0; i < NCOLORS; i++) K[K_COLOR0 + i] = cd_internz(COLORS[i]);
  if (!S.rng) S.rng = cd_seed | 1; /* a loaded state keeps its rng */
  page_init();
  page_create(&S);
  page_render(&S);
  sync(&S); /* on a resumed page these run right after cd_main, on the server's nodes */
}

#ifndef __wasm__
/* The server's model: n points drawn from cd_seed. */
void app_ssr_fill(uint32_t n) {
  S.rng = cd_seed | 1;
  for (uint32_t i = 0; i < n; i++) push_point(&S);
}
#endif
