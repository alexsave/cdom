# 2. Counter: state and events

The count is a C variable.
A click changes it, and the handler then updates the one text node that shows it.

```sh
make -C tutorial/02-counter && open tutorial/02-counter/build/index.html
```

## The React version

```jsx
import { useState } from "react";

export default function Counter() {
  const [count, setCount] = useState(0);
  return (
    <div className="counter">
      <output>{count}</output>
      <div className="row">
        <button aria-label="decrement" onClick={() => setCount((c) => c - 1)}>−</button>
        <button aria-label="increment" onClick={() => setCount((c) => c + 1)}>+</button>
        <button onClick={() => setCount(0)}>reset</button>
      </div>
    </div>
  );
}
```

## The cdom version

[`src/app.c`](src/app.c), the parts that matter:

```c
static int32_t count;                        /* the state */
static cd_id count_text, minus, plus, reset; /* the nodes the app touches later */

static void step(const cd_event_rec *, void *ctx) {
  count += (int32_t)(intptr_t)ctx;           /* ctx carries the step: -1 or 1 */
  show();                                    /* one DOM write: the text node's value */
}

void cd_main(void) {
  ...
  cd_open("output"); cd_text_num(0); cd_name_text(count_text = cd_id_new()); cd_close();
  minus = cd_open_id("button"); cd_text("−"); cd_close();
  ...
  cd_on(minus, CD_EV_CLICK, step, (void *)(intptr_t)-1);
  cd_on(plus, CD_EV_CLICK, step, (void *)(intptr_t)1);
  cd_on(reset, CD_EV_CLICK, on_reset, nullptr);
}
```

## What to notice

- **Ids, not selectors.** `cd_id_new` hands out a number; `cd_name_text` and `cd_open_id` bind it to a node.
  Later calls (`cd_set_text_node_num(count_text, ...)`) reach the node by that number, which is an index into the shim's table.
- **No re-render.** `setCount` in React schedules the component to run again, builds a new virtual tree and diffs it against the old one to find that one text changed.
  In cdom the handler already knows what changed, so it writes that one text node and nothing else runs.
- **Handlers with context.** `cd_on(id, type, fn, ctx)` routes one event type on one node to `fn`, and hands it `ctx`.
  Here `ctx` is the step, so one function serves both buttons, as one arrow function would in React.
  A parameter a handler does not use goes unnamed, since cdom builds as C23: `step` takes the event as `const cd_event_rec *` and no name.
- **One listener.** The shim puts a single click listener on the mount and passes each click to C, which finds the handler; buttons have no listener of their own.
- **Wire after building.** The handlers are registered once the tree is built, which keeps the building code a plain description of the page.
- **Numbers without printf.** `cd_set_text_node_num` sends an unsigned number as it is.
  A count that can go below zero needs its minus sign, and the raw way is a branch and `cd_fmt` for the negative case:

  ```c
  if (count >= 0) {
    cd_set_text_node_num(count_text, (uint32_t)count);
  } else {
    char buf[12];
    uint32_t n = cd_fmt(buf, count, 0, CD_UNIT_NONE);
    cd_set_text_node(count_text, cd_tmp(buf, n));
  }
  ```

  The shorthand is `cd_buf` from `cdom_ui.h` (the lesson's Makefile says `APP_MODULES := ui`), a string builder over a buffer you own:

  ```c
  char room[12];
  cd_buf b = cd_buf_in(room, sizeof room);
  cd_buf_i64(&b, count);                          /* the sign comes with the number */
  cd_set_text_node(count_text, cd_tmp(b.p, b.len));
  ```

  There is no special case for a negative count, so the branch goes away.
  `cd_tmp(b.p, b.len)` hands cdom the bytes, which it reads when the call runs, so a value that keeps changing is never stale.
  The buttons below stay raw (`cd_open_id`, `cd_on`), because this lesson is where you learn what a button is.
