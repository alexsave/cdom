# 5. Todo list: the keyed list

The todos are a plain array of structs.
The list on the page is that array seen through a filter, and `cd_list_update` brings the page in line with it.

```sh
make -C tutorial/05-todo && open tutorial/05-todo/build/index.html
```

## The React version

```jsx
import { useState } from "react";

const FILTERS = { All: () => true, Active: (t) => !t.done, Done: (t) => t.done };

export default function Todo() {
  const [todos, setTodos] = useState([]);
  const [nextId, setNextId] = useState(1);
  const [text, setText] = useState("");
  const [filter, setFilter] = useState("All");
  const left = todos.filter((t) => !t.done).length;

  function add(e) {
    e.preventDefault();
    const value = text.trim();
    if (value === "") return;
    setTodos([...todos, { id: nextId, text: value, done: false }]);
    setNextId(nextId + 1);
    setText("");
  }

  const toggle = (id) => setTodos((ts) => ts.map((t) => (t.id === id ? { ...t, done: !t.done } : t)));
  const remove = (id) => setTodos((ts) => ts.filter((t) => t.id !== id));
  const clearDone = () => setTodos((ts) => ts.filter((t) => !t.done));

  return (
    <main className="todo">
      <form className="add" onSubmit={add}>
        <input
          className="new"
          placeholder="What needs doing?"
          aria-label="New todo"
          autoComplete="off"
          value={text}
          onChange={(e) => setText(e.target.value)}
        />
        <button>Add</button>
      </form>
      <ul className="items">
        {todos.filter(FILTERS[filter]).map((t) => (
          <li key={t.id} className={t.done ? "item done" : "item"}>
            <input type="checkbox" checked={t.done} onChange={() => toggle(t.id)} />
            <span className="text">{t.text}</span>
            <button className="remove" aria-label="Remove" onClick={() => remove(t.id)}>×</button>
          </li>
        ))}
      </ul>
      <footer>
        <span className="left">{left} {left === 1 ? "item" : "items"} left</span>
        <div className="filters">
          {Object.keys(FILTERS).map((name) => (
            <button key={name} aria-pressed={filter === name} onClick={() => setFilter(name)}>{name}</button>
          ))}
        </div>
        <button className="clear" onClick={clearDone}>Clear done</button>
      </footer>
    </main>
  );
}
```

## The cdom version

[`src/app.c`](src/app.c), the parts that matter:

```c
typedef struct { uint32_t id, len; char text[TEXT_CAP]; uint8_t done; } Todo;
static Todo todos[TODO_CAP];

/* The three things React does for you when it reconciles, written out. */
static cd_id item_create(void *ctx, uint32_t i, uint32_t *data);  /* a new key: build its row */
static void item_patch(void *ctx, uint32_t i, cd_item *it);       /* a kept key: write what changed */
static void item_destroy(void *ctx, cd_item *it);                 /* a gone key: free its ids */

static void render(void) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < ntodos; i++) if (shown(&todos[i])) keys[n++] = todos[i].id;
  cd_list_update(&list, keys, n, &TODO_FNS);   /* the keys to show, in order */
  ...                                          /* then the footer's count */
}

static void item_patch(void *, uint32_t, cd_item *it) {
  Todo *t = find(it->key);
  ItemView *v = &views[it->data];
  if (v->done == t->done) return;              /* nothing changed: the row stays as it is */
  v->done = t->done;
  cd_set_class(it->node, t->done ? "item done" : "item");
  cd_set_bool(v->check, "checked", t->done);
}

static void item_destroy(void *, cd_item *it) {
  ItemView *v = &views[it->data];
  cd_id_free(v->check);                        /* also drops the handlers on them */
  cd_id_free(v->remove);
  free_views[nfree_views++] = it->data;
}
```

```c
/* the row's remove button, raw: */
v->remove = cd_open_id("button");
cd_class("remove"); cd_attr("aria-label", "Remove"); cd_text("×");
cd_close();
cd_on(v->remove, CD_EV_CLICK, on_remove, (void *)(uintptr_t)t->id);

/* with cdom_ui.h (APP_MODULES := ui): */
v->remove = cd_button("remove", "×", on_remove, CD_CTX_OF(t->id), nullptr);
cd_set_attr(v->remove, "aria-label", "Remove");

/* a handler asks for the render instead of calling it: */
t->done = !t->done;
cd_dirty(render, nullptr);
```

## What to notice

- **`cd_button` and `CD_CTX_OF`.** A button with a class, a label and a click handler is the same four calls every time, so `cd_button` makes them one.
  It also sets `type="button"`, so a button inside a form submits nothing.
  An attribute it does not take, here `aria-label` or `aria-pressed`, is one `cd_set_attr` on the id it returns.
  `CD_CTX_OF(id)` and `CD_CTX_INDEX(ctx)` are the `(void *)(uintptr_t)` casts that carry a todo's id in a ctx, under a name.
  All of these are in `cdom_ui.h`, which the lesson's Makefile links with `APP_MODULES := ui`.
- **A change asks for the view, it does not draw it.** The raw handlers call `render()` themselves, so a handler that changes two things draws twice.
  `cd_dirty(render, nullptr)` marks the view dirty, and cdom runs it once before the event returns, however many times it was asked.
  `render` takes a `void *` for that reason, and it is the React setter's batching: several changes in one event, one render.

- **Keys are the todo ids.** `cd_list_update` takes the keys to show, in order, the way React's `key` prop is read.
  The ids come from a counter that never goes back, so a key that is gone can never name a different todo later.
- **create, patch and destroy are React's reconciliation, written out.** React creates, updates and removes the `<li>` elements for you when the array changes.
  Here each of the three is a function you write, and `cd_list_update` calls it for each key that is new, kept or gone.
- **Patch touches only what changed.** On every render React builds the element for each item and compares its props with the last ones.
  Here `item_patch` compares the one value that can change, the done flag, against what is on the screen, and writes the class and the checkbox only when it differs.
  The row's element is the same afterwards, so the other rows never see a change.
- **Filtering is passing fewer keys.** Switching to Active passes two keys: the done todo's row is destroyed, and the open rows are kept and patched.
  Switching back creates the done row again, in its place in the order.
- **Ids are freed in destroy.** A row has two ids of its own, the checkbox and the remove button, and their handlers are attached to those ids.
  `cd_remove` takes the row away, and `cd_id_free` in destroy drops the ids and their handlers together.
  In React the garbage collector takes the handlers with the elements.
- **The text stays in the todo, unlike the greeting in lesson 3.**
  A todo's text lives as long as the todo, and its row is built again each time the filter changes.
  So the bytes are copied into the todo on add, and each row build passes `cd_tmp(t->text, t->len)`.
  The text here is bytes with a length and no terminator, which is what `cd_tmp` is for.
  The greeting in lesson 3 is a plain buffer from `snprintf`, which ends in a 0 and needs no length.
  The bytes are read when the row is built and kept nowhere else, so what the user types does not grow any table.
