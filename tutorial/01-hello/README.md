# 1. Hello: building a page

A cdom app is one C function, `cd_main`, that runs once when the page starts and builds the page with calls.
This lesson builds a heading, a paragraph, a list from an array and a link, and nothing on it ever changes.

```sh
make -C tutorial/01-hello && open tutorial/01-hello/build/index.html
```

## The React version

```jsx
const FRUITS = ["apple", "banana", "cherry"];

export default function App() {
  return (
    <main className="card">
      <h1>Hello, cdom</h1>
      <p>This page was built by C, compiled to WebAssembly.</p>
      <ul>
        {FRUITS.map((f) => <li key={f}>{f}</li>)}
      </ul>
      <a href="https://github.com/alexsave/cdom">cdom on GitHub</a>
    </main>
  );
}
```

## The cdom version

[`src/app.c`](src/app.c) builds the same tree:

```c
cd_at(CD_MOUNT, 0);                       /* build into <div id="app">, appending */
cd_open(L("main")); cd_class(L("card"));
cd_open(L("h1")); cd_text(L("Hello, cdom")); cd_close();
cd_open(L("ul"));
for (uint32_t i = 0; i < NFRUITS; i++) {
  cd_open(L("li")); cd_text(cd_internz(FRUITS[i])); cd_close();
}
cd_close();
...
cd_close();                               /* the whole tree enters the page here */
```

## What to notice

- `cd_open` and `cd_close` are JSX's opening and closing tags, and `cd_text` is a text child.
  Attributes (`cd_attr`, `cd_class`) go on the element opened last, before its first child.
- A list is a plain `for` loop.
  There is no `key` here because nothing in this list ever changes; lesson 5 shows the keyed list.
- Strings are interned: each one crosses into JavaScript once and is named by a small number after that.
  `CD_LIT("...")` does it for a literal, once per call site, and `cd_internz` for a string made at run time.
- The tree is built off the page and enters it in one step, when its root (`main`) closes.
- The CSS is a `<style>` element the app builds like any other, so the lesson is one C file.
  A larger app brings its own page instead (`APP_PAGE_SRC`, see the main README).
- React runs this function again whenever state changes.
  `cd_main` runs exactly once, and nothing runs again unless the app asks: the next lesson shows how.

## The build

The [`Makefile`](Makefile) is the one `make new` writes, with a title and warnings as errors:

```make
CDOM ?= $(abspath ../..)
APP_WASM := build/app.wasm
APP_SRC := src/app.c
APP_PAGE := build/index.html
APP_TITLE := Hello, cdom
APP_CFLAGS := -Wall -Wextra -Werror
include $(CDOM)/cdom.mk
```

`make` compiles the C to `build/app.wasm` and writes `build/index.html`, one file with the shim and the module inside it, which opens from disk.
