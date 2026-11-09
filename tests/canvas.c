/* Exercises the canvas and frame ops through the real shim: blit, blit after memory grows, the frame loop
 * starting and stopping, and calling a method on a node. canvas.html checks what reached the page. */
#include "../src/cdom.h"

#define EXPORT(n) __attribute__((export_name(n)))
#define IMPORT(n) __attribute__((import_module("env"), import_name(n)))
IMPORT("frame_seen") void frame_seen(uint32_t count, double ms);

static cd_id canvas, target;
static uint32_t frames, stop_after;

static cd_str z(const char *s) { return cd_internz(s); }

/* A pattern no byte order mistake survives: every channel of every pixel is different. */
static void fill(uint8_t *p, uint32_t w, uint32_t h, uint32_t salt) {
  for (uint32_t i = 0; i < w * h; i++) {
    p[i * 4] = (uint8_t)(i * 7 + salt);
    p[i * 4 + 1] = (uint8_t)(i * 13 + 1 + salt);
    p[i * 4 + 2] = (uint8_t)(i * 29 + 2 + salt);
    p[i * 4 + 3] = 255;
  }
}

void cd_main(void) {
  cd_at(CD_MOUNT, 0);
  cd_open(z("canvas")); cd_attr(z("width"), z("7")); cd_attr(z("height"), z("5"));
  canvas = cd_id_new(); cd_name_el(canvas); cd_close();
  cd_open(z("div")); cd_attr(z("id"), z("target"));
  target = cd_id_new(); cd_name_el(target); cd_close();
}

void cd_on_event(const cd_event_rec *e) { (void)e; }

static uint8_t small[7 * 5 * 4];
EXPORT("blit_small") uint32_t blit_small(uint32_t salt) {
  fill(small, 7, 5, salt);
  cd_blit(canvas, small, 7, 5);
  cd_flush();
  return (uint32_t)(uintptr_t)small;
}

/* Grows memory first, so the shim's views are stale when the blit is applied. */
EXPORT("blit_grown") uint32_t blit_grown(uint32_t salt) {
  uint8_t *p = cd_alloc(4u << 20);
  fill(p, 7, 5, salt);
  cd_blit(canvas, p, 7, 5);
  cd_flush();
  return (uint32_t)(uintptr_t)p;
}

EXPORT("frames_for") void frames_for(uint32_t n) {
  frames = 0;
  stop_after = n;
  cd_frames(1);
  cd_frames(1); /* starting twice must not double the loop */
  cd_flush();
}

void cd_on_frame(double ms) {
  frames++;
  frame_seen(frames, ms);
  if (frames >= stop_after) cd_frames(0);
}

EXPORT("frame_count") uint32_t frame_count(void) { return frames; }
/* A stop from outside a frame (an event, a timer) while the next frame is already asked for: that frame must
 * not reach cd_on_frame. */
EXPORT("frames_stop") void frames_stop(void) { cd_frames(0); cd_flush(); }
EXPORT("call_ping") void call_ping(void) { cd_call(target, z("ping")); cd_flush(); }
EXPORT("target_id") uint32_t target_id(void) { return target; }
