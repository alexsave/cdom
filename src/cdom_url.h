/* cdom_url: the page's address, for an app that routes in C. Link src/cdom_url.c to use it. C owns
 * routing: it reads the address, decides what a path shows, and pushes or replaces the address as
 * it navigates; the shim only reports the address and applies what C asks (the History API). A
 * "path" here is what follows the origin: the pathname, then the search and the hash, if any
 * ("/cards/7?deck=red#top"), as UTF-8 bytes, percent-encoded as the browser keeps it. */
#ifndef CDOM_URL_H
#define CDOM_URL_H
#include <stdint.h>

/* One part of the current address, written to dst (at most cap bytes); returns its length, which
 * may be more than cap (then only cap bytes were written: ask again with more room).
 * CD_URL_PATH: the pathname ("/cards/7"); CD_URL_SEARCH: the query with its "?" ("?deck=red", or
 * nothing); CD_URL_HASH: the fragment with its "#" ("#top", or nothing); CD_URL_ALL: the three in
 * a row, a path as cd_url_push takes it and cd_on_url hands it over. */
enum { CD_URL_PATH, CD_URL_SEARCH, CD_URL_HASH, CD_URL_ALL };
uint32_t cd_url_read(uint32_t part, char *dst, uint32_t cap);

/* Show path (len bytes, read during the call) in the address bar: push adds a history entry (back
 * returns to the address before), replace changes the current one. A relative path resolves
 * against the current address. Nothing loads and no cd_on_url follows: C shows what the path
 * means itself. CD_URL_OK, or CD_URL_REFUSED when the browser would not change the address (a path
 * on another origin, a page whose history cannot change, or too many changes too fast: Safari
 * refuses past 100 in 30 seconds), and the address is as it was. */
enum { CD_URL_REFUSED, CD_URL_OK };
uint32_t cd_url_push(const char *path, uint32_t len);
uint32_t cd_url_replace(const char *path, uint32_t len);

/* The document's title (the tab's and the history entry's name), len bytes of UTF-8. Set it after
 * a push, so the new entry gets it. */
void cd_set_title(const char *text, uint32_t len);

/* The app's hook (an empty weak default in cdom_url.c): the user went back or forward (or followed
 * a link to a #fragment of this page), and the address is now path (len bytes, valid until it
 * returns). The shim listens from start for every app that links cdom_url.c. */
void cd_on_url(const uint8_t *path, uint32_t len);
#endif
