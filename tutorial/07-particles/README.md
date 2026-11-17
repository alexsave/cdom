# 7. Particles: pixels from C

The picture is one array of pixels, and C fills it every frame.
The particles are integers that C moves, and the trails are a fade that C applies to the same array.
Each frame ends with one call that sends the whole array to the canvas.
A click bursts particles where it lands.

```sh
make -C tutorial/07-particles && open tutorial/07-particles/build/index.html
```

## The React version

```jsx
import { useEffect, useRef, useState } from "react";

const W = 320, H = 200, CAP = 2048, STEP_UNITS = 1000, MAX_STEPS = 4;
const FLOOR = (H - 1) * 256;
const NIGHT = [0x0b, 0x10, 0x20]; // #0b1020

export default function Particles() {
  const canvasRef = useRef(null);
  const image = useRef(null);      // the picture, as the canvas wants it
  const parts = useRef([]);        // the live particles, in fixed point
  const [playing, setPlaying] = useState(true);
  const [count, setCount] = useState(0);

  // The loop runs while playing. Its cleanup cancels the frame, and the picture stays on the canvas.
  useEffect(() => {
    const canvas = canvasRef.current;
    const ctx = canvas.getContext("2d");
    image.current ??= ctx.createImageData(W, H);
    const data = image.current.data;
    if (!playing) return;

    let frame, last = null, acc = 0;
    const tick = (now) => {
      if (last === null) last = now; // no time to run for the first frame after Play
      acc += (now - last) * 60;
      last = now;
      for (let n = 0; acc >= STEP_UNITS && n < MAX_STEPS; n++) {
        step(parts.current);
        acc -= STEP_UNITS;
      }
      if (acc >= STEP_UNITS) acc = 0;

      for (let i = 0; i < data.length; i++) {
        if (i % 4 !== 3) data[i] = NIGHT[i % 4] + (((data[i] - NIGHT[i % 4]) * 7) / 8 | 0);
      }
      paint(parts.current, data);
      ctx.putImageData(image.current, 0, 0);
      setCount(parts.current.length); // React skips the render when the number is the same
      frame = requestAnimationFrame(tick);
    };
    frame = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frame);
  }, [playing]);

  function onPointerDown(e) {
    const box = e.currentTarget.getBoundingClientRect();
    const x = ((e.clientX - box.left) * W) / box.width;
    const y = ((e.clientY - box.top) * H) / box.height;
    for (let i = 0; i < 200 && parts.current.length < CAP; i++) {
      parts.current.push(spawn(x, y));
    }
    if (!playing) {
      // Paused: no loop is running, so draw the burst once.
      const ctx = canvasRef.current.getContext("2d");
      paint(parts.current, image.current.data);
      ctx.putImageData(image.current, 0, 0);
      setCount(parts.current.length);
    }
  }

  return (
    <main className="sky">
      <h1>Particles</h1>
      <canvas
        ref={canvasRef}
        className="view"
        width={W}
        height={H}
        onPointerDown={onPointerDown}
      />
      <div className="controls">
        <button className="pause" onClick={() => setPlaying(!playing)}>
          {playing ? "Pause" : "Play"}
        </button>
        <output className="count">{count} particles</output>
      </div>
    </main>
  );
}

function spawn(x, y) {
  return {
    x: x * 256, y: y * 256,
    vx: Math.floor(Math.random() * 1025) - 512,
    vy: -(768 + Math.floor(Math.random() * 768)),
    life: 90 + Math.floor(Math.random() * 120),
    colour: [128 + Math.floor(Math.random() * 128), 128 + Math.floor(Math.random() * 128), 128 + Math.floor(Math.random() * 128)],
  };
}

function step(parts) {
  for (let i = 0; i < parts.length; ) {
    const p = parts[i];
    p.vy += 40;
    p.x += p.vx;
    p.y += p.vy;
    if (p.y > FLOOR) {
      p.y = FLOOR;
      p.vy = -Math.trunc((p.vy * 3) / 4);
      p.vx = Math.trunc((p.vx * 7) / 8);
    }
    p.life--;
    if (p.life <= 0 || p.x < 0 || p.x >= W * 256) {
      parts[i] = parts[parts.length - 1];
      parts.pop();
    } else {
      i++;
    }
  }
}

function paint(parts, data) {
  for (const p of parts) {
    const x = p.x >> 8, y = p.y >> 8;
    for (let dy = 0; dy < 2; dy++)
      for (let dx = 0; dx < 2; dx++) {
        const qx = x + dx, qy = y + dy;
        if (qx < 0 || qx >= W || qy < 0 || qy >= H) continue;
        const k = (qy * W + qx) * 4;
        data[k] = p.colour[0]; data[k + 1] = p.colour[1]; data[k + 2] = p.colour[2]; data[k + 3] = 255;
      }
  }
}
```

## The cdom version

[`src/app.c`](src/app.c), the parts that matter:

```c
static uint32_t px[WORLD_W * WORLD_H];   /* the picture: 0xAABBGGRR per pixel */
static particle particles[CAP];
static uint32_t rng;                      /* one xorshift32, seeded from cd_seed */

void cd_on_frame(double ms) {
  acc += (ms - last_ms) * 60.0;           /* 1/60 ms units: a step is 1000 */
  last_ms = ms;
  while (acc >= STEP_UNITS && steps < MAX_STEPS) { step(); acc -= STEP_UNITS; steps++; }
  draw();                                 /* fade, paint, cd_blit(canvas, px, 320, 200) */
}

static void on_click(const cd_event_rec *e, void *ctx) {
  double box[4];
  if (!cd_rect(canvas, box)) return;
  burst(to_fixed((e->x - box[0]) * WORLD_W / box[2], WORLD_W - 1),
        to_fixed((e->y - box[1]) * WORLD_H / box[3], WORLD_H - 1));
}

void cd_main(void) {
  rng = cd_seed | 1;
  ...
  cd_on(canvas, CD_EV_POINTERDOWN, on_click, 0);
  cd_on(pause_btn, CD_EV_CLICK, on_pause, 0);
  cd_frames(1);
}
```

## What to notice

- **The crossing is one call.** The React version has a loop in JS that moves the particles and writes the pixels, then one `putImageData`.
  In cdom the step, the paint and the fade are all C, on integers, and each frame makes one `cd_blit`.
  No particle is an object that crosses to JS, and nothing is allocated while the page runs.
- **One generator, seeded once.** `rng` is the only source of randomness, and `cd_seed` seeds it.
  The page can fix the seed (`options.seed` in `cdom.start`), and then a run repeats exactly.
  The React version calls `Math.random`, so no two runs match.
- **Draws in logical units.** A velocity is drawn in 1/256 px per step, and a colour is a byte of a draw.
  The canvas's size on the screen never enters a draw, so the same seed gives the same particles at any size.
  Only the click is mapped to the screen, once, and only as a position.
- **A fixed time step.** Each frame adds its elapsed time, and runs whole steps of 1000/60 ms.
  A 60 Hz screen and a 120 Hz screen run the same steps for the same time.
  The React version does the same arithmetic in JS, so the two agree only as far as the floating point does.
- **No effect to clean up.** The React version starts the loop in `useEffect`, cancels it in the cleanup, and keys it on `playing`.
  In cdom, `cd_frames(0)` sits in the handler that pauses, and nothing else has to stop the loop.
- **Pause keeps the picture.** While the frames are off, the last blit stays on the canvas, so nothing has to redraw it.
  Play clears `have_last`, so the first frame after it takes no time, and the paused time is not played back as a jump.
