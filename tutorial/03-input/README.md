# 3. Input: a text field and derived state

The field keeps its own value, and C reads it when the user types.
The greeting and the character count are computed from that value in the handler.

```sh
make -C tutorial/03-input && open tutorial/03-input/build/index.html
```

## The React version

```jsx
import { useState } from "react";

export default function Greeting() {
  const [name, setName] = useState("");
  const count = [...name].length;
  return (
    <main className="form">
      <label htmlFor="name">Your name</label>
      <input id="name" autoComplete="off" value={name} onChange={(e) => setName(e.target.value)} />
      <p className="greeting">Hello, {name === "" ? "stranger" : name}!</p>
      <p className="count">{count} {count === 1 ? "character" : "characters"}</p>
    </main>
  );
}
```

## The cdom version

[`src/app.c`](src/app.c), the parts that matter:

```c
static cd_id input, greeting_text, count_text;

static void on_input(const cd_event_rec *, void *) {
  char name[NAME_CAP];
  uint32_t n;
  if (!cd_input_whole(input, name, sizeof name, &n))     /* the whole value, as UTF-8 */
    n = cd_get_str(input, "value", name, sizeof name);   /* too long: the whole characters that fit */
  ...
  char room[7 + NAME_CAP + 1];
  cd_buf greeting = cd_buf_in(room, sizeof room);            /* cdom_ui.h: APP_MODULES := ui */
  cd_buf_putz(&greeting, "Hello, ");
  cd_buf_put(&greeting, name, n);
  cd_buf_putz(&greeting, "!");
  cd_set_text_node(greeting_text, cd_tmp(greeting.p, greeting.len));  /* a new string per keystroke */
  ...
}

void cd_main(void) {
  ...
  input = cd_open_id("input");
  cd_attr("id", "name");
  cd_attr("autocomplete", "off");
  cd_close();
  ...
  cd_on(input, CD_EV_INPUT, on_input, nullptr);
}
```

## What to notice

- **No round trip for the value.** In React the field's value is state: each keystroke sets it, the component runs again, and the value is written back into the input.
  In cdom the field owns its value, and C reads it only when an input event arrives.
  Nothing is kept in sync, and the cursor never jumps, because C never writes the field.
- **Derived state is computed in the handler.** React computes `count` and the greeting during render from `name`.
  Here the one handler that changes the value also writes the two text nodes, so there is no derived-state hook and nothing can be stale.
- **`cd_buf` builds the strings.** The raw way is `memcpy` and `strlen` into a buffer, with the offsets and the `'!'` position counted by hand:

  ```c
  char greeting[7 + NAME_CAP + 1];
  memcpy(greeting, "Hello, ", 7);
  memcpy(greeting + 7, name, n);
  greeting[7 + n] = '!';
  cd_set_text_node(greeting_text, cd_tmp(greeting, 8 + n));
  ```

  `cd_buf` (the lesson's Makefile says `APP_MODULES := ui`) keeps the length for you, so the pieces are appended in reading order and no offset is written down.
  A piece that does not fit is left out whole and no byte lands outside the buffer.
  The count line is `cd_buf_u64(&count, chars)` and then `cd_buf_putz` of the word, where the raw version needed `cd_fmt`, `strlen`, `memcpy` and `<string.h>`.
- **`cd_tmp` for strings that change every keystroke.** Each greeting is a new string, built in a buffer with a length.
  A literal lives in the page's string table, but a string like this is not one, so `cd_tmp(greeting, len)` hands cdom the bytes and cdom reads them when the call runs.
  Nothing is kept, so the next keystroke can overwrite the buffer.
- **Bytes are not characters.** `cd_input_whole` reads UTF-8 bytes, so "Zoë" is four bytes.
  The count takes the characters by skipping continuation bytes (`(b & 0xC0) == 0x80`).
  The buffer is fixed at 256 bytes, and `cd_input_whole` reads the whole value or none of it.
  When the value does not fit, it reports the full length, and `cd_get_str` of `"value"` reads as many whole characters as fit, so a longer value is shown cut.
