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
typedef struct { uint32_t id; cd_str text; uint8_t done; } Todo;
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

static void item_patch(void *ctx, uint32_t i, cd_item *it) {
  Todo *t = find(it->key);
  ItemView *v = &views[it->data];
  if (v->done == t->done) return;              /* nothing changed: the row stays as it is */
  v->done = t->done;
  cd_set_class(it->node, t->done ? L("item done") : L("item"));
  cd_set_bool(v->check, L("checked"), t->done);
}

static void item_destroy(void *ctx, cd_item *it) {
  ItemView *v = &views[it->data];
  cd_id_free(v->check);                        /* also drops the handlers on them */
  cd_id_free(v->remove);
  free_views[nfree_views++] = it->data;
}
```

## What to notice

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
- **Interned text, unlike the greeting in lesson 3.** A todo's text lives as long as the todo, and its row is built again each time the filter changes.
  So it is interned once on add, with `cd_intern`, and cached by id after that.
  The greeting in lesson 3 changes on every keystroke, which is why it was a one-off `cd_tmp`.
  The string table keeps a removed todo's text, so it grows with what the user types, not with each render.
