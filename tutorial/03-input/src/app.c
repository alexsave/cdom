/* Lesson 3: a text field and derived state. The field keeps its own value: the user types into the DOM,
 * and C reads the value only when an input event says it changed. Nothing is written back into the field,
 * so there is no value to keep in sync and no cursor to move. The greeting and the count are derived from
 * the value in the handler, the one place the value changes. */
#include "cdom.h"
#include <string.h>


/* The field's value is copied out only for the length of one handler. A value longer than this is cut to
 * the whole characters that fit. */
#define NAME_CAP 256

static cd_id input, greeting_text, count_text;

/* Characters, not bytes: in UTF-8 every byte that is not a continuation byte (10xxxxxx) starts a character. */
static uint32_t characters_in(const char *s, uint32_t n) {
  uint32_t count = 0;
  for (uint32_t i = 0; i < n; i++) {
    if ((s[i] & 0xC0) != 0x80) count++;
  }
  return count;
}

/* The one handler: every keystroke, paste or deletion lands here. The greeting and the count are computed
 * from the value now, so they are never stale. */
static void on_input(const cd_event_rec *, void *) {
  char name[NAME_CAP];
  uint32_t n;
  /* cd_input_whole reads the whole value, or none of it when it does not fit (and then n is its whole length
   * in bytes). A value longer than the buffer is shown cut: cd_get_str of "value" writes as many whole
   * characters as fit, and returns how many bytes those are. */
  if (!cd_input_whole(input, name, sizeof name, &n)) n = cd_get_str(input, "value", name, sizeof name);

  if (n == 0) {
    cd_set_text_node(greeting_text, "Hello, stranger!");
  } else {
    char greeting[7 + NAME_CAP + 1];
    memcpy(greeting, "Hello, ", 7);
    memcpy(greeting + 7, name, n);
    greeting[7 + n] = '!';
    /* cd_tmp: the greeting is bytes with a length and no terminator, and it is read when it is used. */
    cd_set_text_node(greeting_text, cd_tmp(greeting, 8 + n));
  }

  uint32_t chars = characters_in(name, n);
  char count[24 + 12];
  uint32_t digits = cd_fmt(count, (int32_t)chars, 0, CD_UNIT_NONE);
  const char *word = chars == 1 ? " character" : " characters";
  uint32_t word_len = (uint32_t)strlen(word);
  memcpy(count + digits, word, word_len);
  cd_set_text_node(count_text, cd_tmp(count, digits + word_len));
}

/* The page's CSS is src/app.css. The build puts every .css file under src/ in a <style> in the page's
 * <head>, so the page is styled before this code runs. */

void cd_main(void) {
  cd_open("main"); cd_class("form");
  cd_open("label"); cd_attr("for", "name"); cd_text("Your name"); cd_close();
  input = cd_open_id("input");
  cd_attr("id", "name");
  cd_attr("autocomplete", "off");
  cd_close();
  cd_open("p"); cd_class("greeting"); cd_text("Hello, stranger!"); cd_name_text(greeting_text = cd_id_new()); cd_close();
  cd_open("p"); cd_class("count"); cd_text("0 characters"); cd_name_text(count_text = cd_id_new()); cd_close();
  cd_close();

  /* The handler is registered after the field is closed, so the id names the input element. */
  cd_on(input, CD_EV_INPUT, on_input, nullptr);
}
