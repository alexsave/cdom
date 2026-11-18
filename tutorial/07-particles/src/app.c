/* Lesson 7: pixels from C. The picture is one array of pixels in C memory. C moves the particles in
 * integer math, paints them into the array, fades the old trails, and sends the whole array to the canvas
 * with one call per frame. JS never sees a particle: the one crossing per frame is the work. */
#include "cdom.h"
#include <stdio.h>


/* The world is a fixed 320x200 of logical pixels. The canvas is drawn at that size and CSS scales it up, and
 * the simulation never looks at the CSS size: a click is converted to world pixels once, when it arrives. */
#define WORLD_W 320
#define WORLD_H 200
#define NPIX (WORLD_W * WORLD_H)

/* The night colour, #0b1020, as the canvas reads a pixel (0xAABBGGRR). */
#define NIGHT 0xff20100bu

/* Positions and velocities are fixed point: ONE is one world pixel, so a particle moves in 1/256 px. */
#define ONE 256
#define FLOOR ((WORLD_H - 1) * ONE)
#define GRAVITY 40 /* added to vy every step: 40/256 px per step, every step */

#define CAP 2048  /* the most particles alive at once; a burst past it drops its extra particles */
#define BURST 200 /* particles per click */

/* The time step is 1000/60 ms. Time is kept in units of 1/60 ms, so one step is 1000 units. Each frame adds
 * its elapsed time and runs as many whole steps as it holds, and the rest waits for the next frame. So a
 * 60 Hz screen and a 120 Hz screen run the same steps for the same time, and a frame that comes late does not
 * make the particles jump. At most MAX_STEPS run in one frame, so a stalled tab does not owe a long catch-up. */
#define STEP_UNITS 1000.0
#define MAX_STEPS 4

typedef struct {
  int32_t x, y, vx, vy; /* fixed point: position in 1/256 px, velocity in 1/256 px per step */
  int32_t life;         /* steps left before the particle dies */
  uint32_t colour;      /* 0xAABBGGRR */
} particle;

static particle particles[CAP];
static uint32_t nparts;

/* The picture: one RGBA pixel per world pixel. Every frame fades it, paints the particles into it, and cd_blit
 * copies it to the canvas. */
static uint32_t px[NPIX];

/* One xorshift32 generator, seeded from cd_seed. cdom.start's options.seed fixes the seed, so a run repeats
 * exactly. Every draw is in logical units: a velocity is in 1/256 px per step, and a colour is three bytes of
 * one draw. Nothing scales a draw by the canvas's CSS size, so one seed gives the same particles at any size. */
static uint32_t rng;
static uint32_t next_random(void) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}

/* The state of the clock and the controls. */
static double acc, last_ms;   /* the time not yet run as steps, in 1/60 ms, and the last frame's time */
static int have_last;         /* 0 until a frame has a time to compare with: the first frame after Play */
static int playing = 1;

static cd_id canvas, pause_btn, pause_text, count_text;
static uint32_t shown_count;  /* the count on the screen, so a frame writes the text only when it changed */

static uint32_t rgba(uint32_t r, uint32_t g, uint32_t b) { return r | (g << 8) | (b << 16) | 0xff000000u; }

/* Each channel keeps 7/8 of its distance from the night colour each frame, so a particle leaves a trail that
 * fades. The difference is signed and divided toward zero, so a channel one step away lands on the night
 * colour exactly, and the sky holds still once the trails are gone. */
static uint32_t fade_channel(uint32_t c, uint32_t night) {
  int32_t d = (int32_t)c - (int32_t)night;
  return (uint32_t)((int32_t)night + d * 7 / 8);
}

static void fade(void) {
  for (uint32_t i = 0; i < NPIX; i++) {
    uint32_t p = px[i], out = 0;
    for (uint32_t s = 0; s < 32; s += 8) out |= fade_channel((p >> s) & 0xffu, (NIGHT >> s) & 0xffu) << s;
    px[i] = out;
  }
}

/* A burst: BURST particles at (x, y), each with a random push and colour. Four draws per particle, always,
 * even when the cap has no room: the draws do not depend on how many particles are alive. */
static void burst(int32_t x, int32_t y) {
  for (uint32_t i = 0; i < BURST; i++) {
    int32_t vx = (int32_t)(next_random() % 1025u) - 512;   /* -2 to 2 px per step */
    int32_t vy = -(768 + (int32_t)(next_random() % 768u));  /* 3 to 6 px per step, up */
    int32_t life = (int32_t)(90 + next_random() % 120u);    /* 1.5 to 3.5 s */
    uint32_t d = next_random();
    uint32_t colour = rgba(128 + (d & 127u), 128 + ((d >> 8) & 127u), 128 + ((d >> 16) & 127u));
    if (nparts == CAP) continue;
    particles[nparts].x = x;
    particles[nparts].y = y;
    particles[nparts].vx = vx;
    particles[nparts].vy = vy;
    particles[nparts].life = life;
    particles[nparts].colour = colour;
    nparts++;
  }
}

/* One step of the simulation, in integers. Gravity adds to the velocity, then the particle moves. A particle
 * that lands on the floor bounces at three quarters of its speed. A particle dies when its life runs out or
 * it leaves the sides. A dead one is replaced by the last one, so the array never has holes. */
static void step(void) {
  uint32_t i = 0;
  while (i < nparts) {
    particle *p = &particles[i];
    p->vy += GRAVITY;
    p->x += p->vx;
    p->y += p->vy;
    if (p->y > FLOOR) {
      p->y = FLOOR;
      p->vy = -(p->vy * 3) / 4;
      p->vx = p->vx * 7 / 8;
    }
    p->life--;
    if (p->life <= 0 || p->x < 0 || p->x >= WORLD_W * ONE) {
      particles[i] = particles[--nparts];
    } else {
      i++;
    }
  }
}

/* Each particle is a 2x2 block of pixels, so it reads well when the canvas is scaled up. Blocks past the edge
 * are cut at the edge. */
static void paint_particles(void) {
  for (uint32_t i = 0; i < nparts; i++) {
    int32_t x = particles[i].x >> 8, y = particles[i].y >> 8;
    for (int32_t dy = 0; dy < 2; dy++) {
      for (int32_t dx = 0; dx < 2; dx++) {
        int32_t qx = x + dx, qy = y + dy;
        if (qx >= 0 && qx < WORLD_W && qy >= 0 && qy < WORLD_H) px[qy * WORLD_W + qx] = particles[i].colour;
      }
    }
  }
}

/* The count text: one write, and only when the count changed. The text changes with the count, so it
 * is bytes with a length (cd_tmp), read when it is used. */
static void show_count(void) {
  if (nparts == shown_count) return;
  shown_count = nparts;
  char buf[24];
  uint32_t n = (uint32_t)snprintf(buf, sizeof buf, "%u particles", nparts);
  cd_set_text_node(count_text, cd_tmp(buf, n));
}

/* What the screen shows: fade, paint, one blit. */
static void draw(void) {
  fade();
  paint_particles();
  cd_blit(canvas, px, WORLD_W, WORLD_H);
  show_count();
}

/* A world position in whole pixels, as fixed point, kept inside the world. */
static int32_t to_fixed(double v, int32_t max_px) {
  if (v < 0) v = 0;
  if (v > max_px) v = max_px;
  return (int32_t)(v * ONE);
}

/* Runs before each paint while cd_frames(1) is on. It adds the time since the last frame, runs the whole steps
 * that time holds, and draws. */
void cd_on_frame(double ms) {
  if (!have_last) {
    last_ms = ms; /* the first frame after Play has no time to run: the paused time is not simulated */
    have_last = 1;
  }
  acc += (ms - last_ms) * 60.0; /* in units of 1/60 ms, so a step is STEP_UNITS */
  last_ms = ms;
  int steps = 0;
  while (acc >= STEP_UNITS && steps < MAX_STEPS) {
    step();
    acc -= STEP_UNITS;
    steps++;
  }
  if (acc >= STEP_UNITS) acc = 0.0; /* a backlog past MAX_STEPS is dropped, not replayed later */
  draw();
}

/* A click bursts particles where it was. The canvas's box on the screen maps the click to world pixels. cd_rect
 * is asked once per click, because the layout can change between clicks. */
static void on_click(const cd_event_rec *e, void *ctx) {
  (void)ctx;
  double box[4];
  if (!cd_rect(canvas, box) || box[2] <= 0 || box[3] <= 0) return;
  double wx = ((double)e->x - box[0]) * WORLD_W / box[2];
  double wy = ((double)e->y - box[1]) * WORLD_H / box[3];
  burst(to_fixed(wx, WORLD_W - 1), to_fixed(wy, WORLD_H - 1));
  if (!playing) draw(); /* the frames are off while paused, so this click draws its own burst */
}

/* Pause stops the frames and keeps the picture on the canvas. Play starts them again, and the first frame
 * after Play has no elapsed time, so the paused time is not simulated as a jump. */
static void on_pause(const cd_event_rec *e, void *ctx) {
  (void)e; (void)ctx;
  playing = !playing;
  if (playing) {
    have_last = 0;
    cd_frames(1);
  } else {
    cd_frames(0);
  }
  cd_set_text_node(pause_text, playing ? "Pause" : "Play");
}

/* The page's look. The canvas keeps its night colour in both schemes: the page around it follows them. */
static void style(void) {
  cd_open("style");
  cd_text(":root { color-scheme: light dark; font-family: system-ui, sans-serif; }"
          "body { margin: 0; }"
          "button { font: inherit; color: inherit; cursor: pointer; background: light-dark(#ffffff, #2a2a2e);"
          "  border: 1px solid light-dark(#8a8a8f, #6e6e73); border-radius: 0.375rem; }"
          "button:focus-visible { outline: 2px solid light-dark(#1f5fd0, #8ab4ff); outline-offset: 1px; }"
          ".sky { max-width: 40rem; margin: 2rem auto; padding: 0 1rem; }"
          ".sky h1 { margin: 0 0 1rem; font-size: 1.75rem; font-weight: 600; }"
            /* The canvas is 320 px wide in its attributes, so it is 16:10 at any width. box-shadow draws its edge
             * without taking any space, so the picture keeps its exact 16:10 box. */
          ".view { display: block; width: 100%; max-width: 40rem; height: auto; aspect-ratio: 16 / 10;"
          "  image-rendering: pixelated; background: #0b1020; border-radius: 0.5rem;"
          "  box-shadow: 0 0 0 1px light-dark(#c9c9d1, #3a3a3e); }"
          ".controls { display: flex; align-items: center; gap: 0.75rem; margin-top: 0.75rem; }"
          ".controls button { min-width: 6rem; font-size: 1.125rem; padding: 0.5rem 1rem; }"
          ".count { color: light-dark(#5a5a60, #a1a1a8); font-variant-numeric: tabular-nums; }");
  cd_close();
}

void cd_main(void) {
  /* The seed comes before anything draws, so the first burst is the same for the same seed. */
  rng = cd_seed | 1; /* xorshift32 stays at 0 from 0 */
  style();

  cd_open("main"); cd_class("sky");
  cd_open("h1"); cd_text("Particles"); cd_close();
  canvas = cd_open_id("canvas"); cd_class("view");
  cd_attr("width", "320"); cd_attr("height", "200");
  cd_close();
  cd_open("div"); cd_class("controls");
  pause_btn = cd_open_id("button"); cd_class("pause");
  cd_text("Pause"); cd_name_text(pause_text = cd_id_new()); cd_close();
  cd_open("output"); cd_class("count");
  cd_text("0 particles"); cd_name_text(count_text = cd_id_new()); cd_close();
  cd_close();
  cd_close();

  cd_on(canvas, CD_EV_POINTERDOWN, on_click, 0);
  cd_on(pause_btn, CD_EV_CLICK, on_pause, 0);

  /* One burst in the middle, so the page is never empty when it opens. */
  burst(to_fixed(WORLD_W / 2.0, WORLD_W - 1), to_fixed(WORLD_H / 2.0, WORLD_H - 1));
  draw();
  cd_frames(1);
}
