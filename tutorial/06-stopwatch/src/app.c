/* Lesson 6: time. The stopwatch shows the time since Start, less the time it was stopped. The time comes from
 * cd_now(), the clock, and not from counting frames, so it is right at any frame rate: a slow frame shows a
 * later time, and the watch never runs slow. Frames are the right clock for drawing: cd_on_frame runs once
 * before each paint, and it stops with the page's frames when the tab is hidden, so there is no timer to
 * clear when the page goes away. */
#include "cdom.h"
#include <stdio.h>

#define L(s) CD_LIT(s)

/* Laps are kept in the order they were taken. A watch with more laps than this drops the new ones. */
#define LAP_CAP 512

/* How long the "Reset" note stays on the page. */
#define NOTE_MS 1500

/* The state. The clock is the runs added up: accumulated_ms is the time of the runs that are over, and the
 * run that is going began at started_ms. Both are in ms on cd_now's clock. */
static int running;
static double accumulated_ms, started_ms;

/* The laps. split_cs[N - 1] is lap N's time since the lap before it, in whole hundredths. Laps are only
 * ever added, and Reset takes them all away, so lap N is always split_cs[N - 1]. */
static uint32_t nlaps;
static uint32_t split_cs[LAP_CAP];
static uint32_t lap_mark_cs;  /* the elapsed time at the last lap, in hundredths */
static uint32_t keys[LAP_CAP]; /* the lap numbers to show, newest first; the keyed list reads them by index */

/* The hundredths on the screen. A frame compares against it, so a frame that would write the same text skips. */
static uint32_t shown_cs;

static cd_id time_text, start_btn, start_text, lap_btn, reset_btn, note_text, laps_ol;
static cd_list laps_list;
static uint32_t note_timer; /* the pending timer that clears the note, 0 when none is pending */

/* The clock now, in ms: a run that is going adds the time since it started. */
static double elapsed_ms(void) {
  return accumulated_ms + (running ? cd_now() - started_ms : 0.0);
}

/* Whole hundredths, rounded down, which is what the display shows. The double is cut to an integer once,
 * here, and everything after it is integer math. The cap keeps the cast in range after 497 days. */
static uint32_t hundredths(double ms) {
  double cs = ms / 10.0;
  return cs >= 4294967295.0 ? 0xffffffffu : (uint32_t)cs;
}

/* MM:SS.cc, each part zero-padded. Minutes take more digits past 99, as they should. */
static uint32_t format_time(char *buf, uint32_t cs) {
  int n = snprintf(buf, 16, "%02u:%02u.%02u", cs / 6000, (cs / 100) % 60, cs % 100);
  return (uint32_t)n;
}

/* The view of the clock: one text write. The text is a one-off string (cd_tmp), since it changes every
 * hundredth and interning it would grow the string table for nothing. */
static void show(uint32_t cs) {
  char buf[16];
  uint32_t n = format_time(buf, cs);
  cd_set_text_node(time_text, cd_tmp(buf, n));
  shown_cs = cs;
}

/* cd_on_frame runs before each paint while cd_frames(1) is on. It reads the clock now, and writes only when
 * the hundredths changed. A hundredth lasts 10 ms, so a screen faster than 100 Hz has frames that land in the
 * hundredth already on the screen, and those skip the write. The frame's own timestamp is not used: the clock
 * is read at the moment of the draw. */
void cd_on_frame(double ms) {
  (void)ms;
  uint32_t cs = hundredths(elapsed_ms());
  if (cs != shown_cs) show(cs);
}

/* The view of the buttons: the start button's label, and which buttons can be pressed. Lap needs a run going.
 * Reset needs something to reset, and no run going. */
static void sync(void) {
  cd_set_text_node(start_text, running ? L("Stop") : L("Start"));
  cd_set_bool(lap_btn, L("disabled"), !running);
  cd_set_bool(reset_btn, L("disabled"), running || accumulated_ms == 0.0);
}

/* The lap rows are built from the keys, and the keys are the lap numbers, newest first. A row is created
 * once, when its lap is taken, and its text is fixed from then on. The text is a one-off string, since each
 * lap's text is sent once and interning it would keep every lap of every run in the string table. */
static cd_id lap_create(void *ctx, uint32_t i, uint32_t *data) {
  (void)ctx;
  uint32_t n = keys[i];
  char name[16], split[16];
  uint32_t name_len = (uint32_t)snprintf(name, sizeof name, "Lap %u", n);
  uint32_t split_len = format_time(split, split_cs[n - 1]);

  cd_id row = cd_open_id(L("li"));
  cd_open(L("span")); cd_class(L("name")); cd_text(cd_tmp(name, name_len)); cd_close();
  cd_open(L("span")); cd_class(L("split")); cd_text(cd_tmp(split, split_len)); cd_close();
  cd_close();
  *data = 0; /* a row keeps nothing of its own: no ids and no handlers */
  return row;
}

/* patch: a lap survived, and there is nothing to write. A lap's number and its split are fixed when it is
 * taken, so a row that is kept is already right. The keyed list still calls patch for each kept key, and it
 * returns at once. */
static void lap_patch(void *ctx, uint32_t i, cd_item *it) {
  (void)ctx; (void)i; (void)it;
}

/* destroy: the row's root is already removed from the page. The row has no ids of its own, so there is
 * nothing else to free. */
static void lap_destroy(void *ctx, cd_item *it) {
  (void)ctx; (void)it;
}

static const cd_list_fns LAP_FNS = {0, lap_create, lap_patch, lap_destroy};

/* The list is the laps, newest first: the keys are the lap numbers, the largest on top. */
static void render_laps(void) {
  for (uint32_t i = 0; i < nlaps; i++) keys[i] = nlaps - i;
  cd_list_update(&laps_list, keys, nlaps, &LAP_FNS);
}

/* The note's timer: it clears the note after NOTE_MS. A timer that fired is done, so the token is dropped. */
static void clear_note(void *ctx) {
  (void)ctx;
  note_timer = 0;
  cd_set_text_node(note_text, L(""));
}

/* ---- handlers ---- */

/* Start and Stop share one button. Stop folds the run that is ending into the accumulated time, and it is
 * where the frame loop is turned off: there is no effect to clean up later, because no frame is asked for
 * after this point. */
static void on_start_stop(const cd_event_rec *e, void *ctx) {
  (void)e; (void)ctx;
  if (running) {
    accumulated_ms = elapsed_ms(); /* still running here, so this adds the run that ends now */
    running = 0;
    cd_frames(0);
    show(hundredths(accumulated_ms)); /* the last draw, so the display ends on the stop time */
  } else {
    started_ms = cd_now();
    running = 1;
    cd_frames(1);
  }
  sync();
}

static void on_lap(const cd_event_rec *e, void *ctx) {
  (void)e; (void)ctx;
  if (!running || nlaps == LAP_CAP) return;
  /* The split is taken from whole hundredths, so the splits add up to the time shown. */
  uint32_t cs = hundredths(elapsed_ms());
  split_cs[nlaps++] = cs - lap_mark_cs;
  lap_mark_cs = cs;
  render_laps();
}

static void on_reset(const cd_event_rec *e, void *ctx) {
  (void)e; (void)ctx;
  accumulated_ms = 0.0;
  nlaps = 0;
  lap_mark_cs = 0;
  render_laps(); /* no laps: the keyed list removes every row, in one operation */
  show(0);
  sync();

  /* One note at a time. A Reset before the last note cleared cancels that note's timer, so the old timer
   * cannot clear the new note early. Stopping a timer that already fired does nothing. */
  if (note_timer) cd_timer_stop(note_timer);
  cd_set_text_node(note_text, L("Reset"));
  note_timer = cd_timer_cb(NOTE_MS, clear_note, 0);
}

/* The page's look. The colours are light-dark() tokens, so the page reads the same in both schemes. */
static void style(void) {
  cd_open(L("style"));
  cd_text(L(":root { color-scheme: light dark; font-family: system-ui, sans-serif; }"
            "body { margin: 0; }"
            "button { font: inherit; color: inherit; cursor: pointer; background: light-dark(#ffffff, #2a2a2e);"
            "  border: 1px solid light-dark(#8a8a8f, #6e6e73); border-radius: 0.375rem; }"
            "button:disabled { cursor: default; opacity: 0.45; }"
            "button:focus-visible { outline: 2px solid light-dark(#1f5fd0, #8ab4ff); outline-offset: 1px; }"
            ".watch { max-width: 28rem; margin: 3rem auto; padding: 0 1rem; text-align: center; }"
            ".watch .time { display: block; font-size: clamp(2.5rem, 14vw, 4rem); font-weight: 300;"
            "  line-height: 1.1; white-space: nowrap; font-variant-numeric: tabular-nums; }"
            ".controls { display: flex; justify-content: center; gap: 0.5rem; margin-top: 1.5rem; }"
            ".controls button { min-width: 6rem; font-size: 1.125rem; padding: 0.5rem 1rem; }"
            ".controls .start { color: light-dark(#1f5fd0, #8ab4ff); border-color: light-dark(#1f5fd0, #8ab4ff); }"
            ".note { min-height: 1.5em; margin: 1rem 0 0; color: light-dark(#5a5a60, #a1a1a8); }"
            ".laps { list-style: none; margin: 1rem 0 0; padding: 0; text-align: left;"
            "  font-variant-numeric: tabular-nums; }"
            ".laps li { display: flex; justify-content: space-between; padding: 0.625rem 0.25rem;"
            "  border-bottom: 1px solid light-dark(#e5e5ea, #3a3a3e); }"
            ".laps .name { color: light-dark(#5a5a60, #a1a1a8); }"));
  cd_close();
}

void cd_main(void) {
  cd_at(CD_MOUNT, 0);
  style();

  cd_open(L("main")); cd_class(L("watch"));
  cd_open(L("output")); cd_class(L("time")); cd_text(L("00:00.00")); cd_name_text(time_text = cd_id_new()); cd_close();

  cd_open(L("div")); cd_class(L("controls"));
  start_btn = cd_open_id(L("button")); cd_class(L("start"));
  cd_text(L("Start")); cd_name_text(start_text = cd_id_new()); cd_close();
  lap_btn = cd_open_id(L("button")); cd_class(L("lap")); cd_text(L("Lap")); cd_close();
  reset_btn = cd_open_id(L("button")); cd_class(L("reset")); cd_text(L("Reset")); cd_close();
  cd_close();

  /* The note keeps its height when it is empty, so the page does not move when it appears. */
  cd_open(L("p")); cd_class(L("note")); cd_attr(L("role"), L("status"));
  cd_text(L("")); cd_name_text(note_text = cd_id_new());
  cd_close();

  /* The list's own element: it holds nothing but the laps, so owns_parent lets a clear be one operation. */
  laps_ol = cd_open_id(L("ol")); cd_class(L("laps")); cd_close();
  laps_list.parent = laps_ol;
  laps_list.owns_parent = 1;
  cd_close();

  cd_on(start_btn, CD_EV_CLICK, on_start_stop, 0);
  cd_on(lap_btn, CD_EV_CLICK, on_lap, 0);
  cd_on(reset_btn, CD_EV_CLICK, on_reset, 0);

  show(0);
  sync();
}
