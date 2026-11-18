/* Lesson 1: a page built once. cd_main runs one time when the page starts; everything it builds stays
 * until the app changes it. Nothing here changes, so there is no state and no handler. */
#include "cdom.h"

/* A string is written as plain C, and cdom reads it one of three ways. A literal written as the argument
 * ("main", "Hello, cdom") goes into the page's string table when the app is built: free at run time.
 * Read-only data (FRUITS[i] below) is read from C memory once, the first time it is used, then cached.
 * Anything else, a buffer that changes, is read every time a call uses it. */

/* Data the page shows. In React this would be a prop or an import; here it is a C array. */
static const char *const FRUITS[] = {"apple", "banana", "cherry", 0};

/* The page's CSS is src/app.css. The build puts every .css file under src/ in a <style> in the page's
 * <head>, so the page is styled before this code runs. */

/* cd_main starts building at the end of the mount (<div id="app">); cd_at is for building elsewhere. */
void cd_main(void) {
  cd_open("main"); cd_class("card");

  cd_open("h1"); cd_text("Hello, cdom"); cd_close();
  cd_open("p"); cd_text("This page was built by C, compiled to WebAssembly."); cd_close();

  /* A list from data: a plain loop where JSX would use .map(), up to the 0 that ends FRUITS. The names
   * are read-only data, so each is read from C memory the first time cd_text sees it and cached after. */
  cd_open("ul");
  for (uint32_t i = 0; FRUITS[i]; i++) {
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
