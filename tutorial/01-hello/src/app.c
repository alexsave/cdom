/* Lesson 1: a page built once. cd_main runs one time when the page starts; everything it builds stays
 * until the app changes it. Nothing here changes, so there is no state and no handler. */
#include "cdom.h"

/* A string is written as plain C. A literal ("main", "Hello, cdom") goes into the page's string table when
 * the app is built, so it costs nothing at run time. Any other string is read from C memory when the
 * call that takes it runs. */

/* Data the page shows. In React this would be a prop or an import; here it is a C array. */
static const char *const FRUITS[] = {"apple", "banana", "cherry"};
#define NFRUITS (sizeof FRUITS / sizeof FRUITS[0])

static void style(void) {
  cd_open("style");
  cd_text(":root { color-scheme: light dark; font-family: system-ui, sans-serif; }"
          "body { margin: 0; }"
          ".card { max-width: 32rem; margin: 3rem auto; padding: 0 1rem; line-height: 1.5; }"
          ".card h1 { margin: 0 0 0.5rem; }"
          ".card a { color: light-dark(#1f5fd0, #8ab4ff); }");
  cd_close();
}

void cd_main(void) {
  cd_at(CD_MOUNT, 0); /* build into the mount (<div id="app">), appending */
  style();

  cd_open("main"); cd_class("card");

  cd_open("h1"); cd_text("Hello, cdom"); cd_close();
  cd_open("p"); cd_text("This page was built by C, compiled to WebAssembly."); cd_close();

  /* A list from data: a plain loop where JSX would use .map(). The names are not literals, so each
   * is read from C memory by cd_text when it runs. */
  cd_open("ul");
  for (uint32_t i = 0; i < NFRUITS; i++) {
    cd_open("li"); cd_text(FRUITS[i]); cd_close();
  }
  cd_close();

  /* Attributes go on the element opened last, before its children. */
  cd_open("a");
  cd_attr("href", "https://github.com/alexsave/cdom");
  cd_text("cdom on GitHub");
  cd_close();

  cd_close(); /* main: the tree enters the page when its root closes */
}
