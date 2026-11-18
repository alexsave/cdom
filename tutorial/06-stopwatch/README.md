# 6. Stopwatch: frames, timers and the clock

The time comes from the clock, `cd_now()`, and not from counting ticks.
While the watch runs, a frame callback draws it, and the frames stop when the watch stops.
A short note uses a timer, and a second Reset stops the first timer before it starts another.

```sh
make -C tutorial/06-stopwatch && open tutorial/06-stopwatch/build/index.html
```

## The React version

```jsx
import { useEffect, useRef, useState } from "react";

const pad = (n) => String(n).padStart(2, "0");
const format = (ms) => {
  const cs = Math.floor(ms / 10);
  return `${pad(Math.floor(cs / 6000))}:${pad(Math.floor(cs / 100) % 60)}.${pad(cs % 100)}`;
};

export default function Stopwatch() {
  const [running, setRunning] = useState(false);
  const [accumulated, setAccumulated] = useState(0); // ms of the runs that are over
  const [now, setNow] = useState(0);
  const [laps, setLaps] = useState([]); // each lap's total, in ms
  const [resets, setResets] = useState(0);
  const [noteShown, setNoteShown] = useState(false);
  const started = useRef(0);

  const elapsed = accumulated + (running ? now - started.current : 0);

  // While running, every frame reads the clock and renders. The cleanup cancels the pending frame.
  useEffect(() => {
    if (!running) return;
    let frame;
    const tick = () => {
      setNow(performance.now());
      frame = requestAnimationFrame(tick);
    };
    frame = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frame);
  }, [running]);

  // The note clears itself. A new Reset starts a new timer, and the cleanup clears the one before it.
  useEffect(() => {
    if (resets === 0) return;
    const id = setTimeout(() => setNoteShown(false), 1500);
    return () => clearTimeout(id);
  }, [resets]);

  function startStop() {
    if (running) {
      setAccumulated(accumulated + performance.now() - started.current);
      setRunning(false);
    } else {
      const t = performance.now();
      started.current = t;
      setNow(t);
      setRunning(true);
    }
  }

  const lap = () => setLaps([...laps, accumulated + performance.now() - started.current]);

  function reset() {
    setAccumulated(0);
    setLaps([]);
    setResets(resets + 1);
    setNoteShown(true);
  }

  const rows = laps
    .map((total, i) => ({ n: i + 1, split: total - (i ? laps[i - 1] : 0) }))
    .reverse();

  return (
    <main className="watch">
      <output className="time">{format(elapsed)}</output>
      <div className="controls">
        <button className="start" onClick={startStop}>{running ? "Stop" : "Start"}</button>
        <button className="lap" disabled={!running} onClick={lap}>Lap</button>
        <button className="reset" disabled={running || accumulated === 0} onClick={reset}>Reset</button>
      </div>
      <p className="note" role="status">{noteShown ? "Reset" : ""}</p>
      <ol className="laps">
        {rows.map((r) => (
          <li key={r.n}>
            <span className="name">Lap {r.n}</span>
            <span className="split">{format(r.split)}</span>
          </li>
        ))}
      </ol>
    </main>
  );
}
```

## The cdom version

[`src/app.c`](src/app.c), the parts that matter:

```c
static int running;
static double accumulated_ms, started_ms; /* the clock: the runs that are over, and the run going now */
static uint32_t shown_cs;                 /* what is on the screen, in whole hundredths */

static double elapsed_ms(void) {
  return accumulated_ms + (running ? cd_now() - started_ms : 0.0);
}

/* Runs before each paint while cd_frames(1) is on. It writes only when the hundredths changed. */
void cd_on_frame(double) {
  uint32_t cs = hundredths(elapsed_ms());
  if (cs != shown_cs) show(cs);
}

static void on_start_stop(const cd_event_rec *, void *) {
  if (running) {
    accumulated_ms = elapsed_ms();
    running = 0;
    cd_frames(0);                         /* no frame is asked for after this: nothing to clean up later */
    show(hundredths(accumulated_ms));
  } else {
    started_ms = cd_now();
    running = 1;
    cd_frames(1);
  }
  sync();
}

static void on_reset(const cd_event_rec *, void *) {
  ...
  if (note_timer) cd_timer_stop(note_timer); /* a Reset before the note clears replaces its timer */
  cd_set_text_node(note_text, "Reset");
  note_timer = cd_timer_cb(NOTE_MS, clear_note, 0);
}
```

## What to notice

- **No effect to clean up.** The React version starts a frame loop in `useEffect` and cancels it in the returned cleanup, keyed on `running`.
  In cdom, `cd_frames(0)` sits in the handler where `running` changes to 0.
  There is no cleanup function to forget, because the loop is stopped at the one place that stops the watch.
- **The time comes from the clock.** Adding 10 ms per tick drifts, and a background tab that slows its timers makes the watch fall behind.
  Here `cd_now()` is read at each draw, so a late frame shows the right time, at any frame rate.
- **Frames are the right clock for drawing.** A frame callback runs once before each paint, and it is paused in a hidden tab, so there is no timer to tidy up when the page is hidden.
  The React version re-runs the whole component on every frame, because `performance.now()` is always a new number.
- **Skip the write when the text would not change.** A hundredth lasts 10 ms, so a screen faster than 100 Hz has frames that land in the hundredth already on the screen.
  `cd_on_frame` compares the hundredths it would show with the ones on the screen, and writes only when they differ.
  The React version has no such check, so it renders the component on every frame.
- **Timer tokens.** `cd_timer_cb` returns a token, and `cd_timer_stop(token)` is `clearTimeout`.
  Stopping a timer that already fired does nothing, so the app does not need to know whether it fired, and a token that was stopped can never stop a later timer in the same slot.
  The React version closes over the id in its effect, and the cleanup it returns is what clears the timer.
