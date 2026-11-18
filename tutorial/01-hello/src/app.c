/* Lesson 1: a page built once. cd_main runs one time when the page starts; everything it builds stays
 * until the app changes it. Nothing here changes, so there is no state and no handler. */
#include "cdom.h"

/* CD_LIT interns a string literal once per call site, so the second time a line runs it costs a load,
 * not a hash. L keeps the tree below readable. */
#define L(s) CD_LIT(s)

/* Data the page shows. In React this would be a prop or an import; here it is a C array. */
static const char *const FRUITS[] = {"apple", "banana", "cherry"};
#define NFRUITS (sizeof FRUITS / sizeof FRUITS[0])

static void style(void) {
  cd_open(L("style"));
  cd_text(L(":root { color-scheme: light dark; font-family: system-ui, sans-serif; }"
            "body { margin: 0; }"
            ".card { max-width: 32rem; margin: 3rem auto; padding: 0 1rem; line-height: 1.5; }"
            ".card h1 { margin: 0 0 0.5rem; }"
            ".card a { color: light-dark(#1f5fd0, #8ab4ff); }"));
  cd_close();
}

void cd_main(void) {
  cd_at(CD_MOUNT, 0); /* build into the mount (<div id="app">), appending */
  style();

  cd_open(L("main")); cd_class(L("card"));

  cd_open(L("h1")); cd_text(L("Hello, cdom")); cd_close();
  cd_open(L("p")); cd_text(L("This page was built by C, compiled to WebAssembly.")); cd_close();

  /* A list from data: a plain loop where JSX would use .map(). The names are not literals, so each is
   * passed with cd_tmpz, which sends its bytes with the next op. */
  cd_open(L("ul"));
  for (uint32_t i = 0; i < NFRUITS; i++) {
    cd_open(L("li")); cd_text(cd_tmpz(FRUITS[i])); cd_close();
  }
  cd_close();

  /* Attributes go on the element opened last, before its children. */
  cd_open(L("a"));
  cd_attr(L("href"), L("https://github.com/alexsave/cdom"));
  cd_text(L("cdom on GitHub"));
  cd_close();

  cd_close(); /* main: the tree enters the page when its root closes */
}
