/* cdom_html: the native sink. A C implementation of shim/cdom.js for the native
 * build of cdom.c: it defines every import cdom.c calls (j_strings, j_open, ...
 * j_host, j_delegate, js_input; natively they are plain functions), builds an in-memory node tree
 * from those calls, and writes that tree as HTML.
 * It is the server renderer and the C conformance implementation of the ops.
 *
 * Usage (one render):
 *   cd_html_reset();            fresh tree and id table, node 0 is the mount
 *   ... app code: cd_main(); cd_flush();
 *   n = cd_html_write(buf, cap); the mount's children as HTML
 *
 * Semantics follow the DOM calls the shim makes, including where the shim would
 * throw (a missing node, a child under a text node, attach into a parent that
 * lost the remembered sibling): the sink then prints the call and aborts.
 * Node memory lives until the next cd_html_reset (the DOM keeps a node alive
 * while anything names it; an arena per render is the exact and simple match).
 *
 * HTML rules:
 *  - no whitespace between nodes; attributes in the order they were first set;
 *    class is an attribute like any other, written where the first CLASS set it;
 *  - text and attribute values escape & < > "; tag and attribute names are
 *    written in ASCII lower case, as the HTML DOM stores them;
 *  - void elements (br hr img input meta link area base col embed source track
 *    wbr) have no close tag; children under one cannot be expressed in HTML and
 *    abort the write;
 *  - an empty text node, which HTML cannot carry, is written as <!--e-->, and a
 *    text node right after a non-empty text node is written after <!---->, so
 *    the parser keeps the two apart. Resuming (cdom.h) reads them back: <!--e-->
 *    stands for the empty text node, <!----> is skipped. No comment is written
 *    inside script, style, textarea or title, where it would be text;
 *  - script and style hold raw text (no escaping; "</" inside aborts the write);
 *    a pre, textarea or listing whose first text starts with a newline gets one
 *    extra newline, since the parser drops the first;
 *  - the output is a pure function of the calls: no addresses, no clocks.
 *  Content the HTML parser rearranges (text directly in a table, a p inside a p)
 *  is not detected here. */
#ifndef CDOM_HTML_H
#define CDOM_HTML_H
#include <stdint.h>

/* Fresh tree and id table; node 0 is the (unwritten) mount. Interned strings
 * the core already sent stay known, as they do in the shim. */
void cd_html_reset(void);

/* Writes the mount's children as HTML into dst. Returns the length; when that
 * is more than cap, nothing past cap is written and the return value is the
 * length needed. */
uint32_t cd_html_write(char *dst, uint32_t cap);

/* The node named id as HTML (its outerHTML), for tests that check what one id shows; 0
 * when no node has that id. Same return rule as cd_html_write. */
uint32_t cd_html_node(uint32_t id, char *dst, uint32_t cap);

/* The number of DOM calls (every import but strings and input) since the last cd_html_reset:
 * what a change costs across the wasm boundary, counted where the shim would count it. A walk
 * of any length is one call. */
uint64_t cd_html_ops(void);

/* A dev build's warnings (cdom.h, CDOM_DEV): each is printed to stderr as "cdom dev: <msg>" and
 * counted since the last cd_html_reset; the last one's text stays readable (NUL-terminated, "" for
 * none, cut at 255 bytes). */
uint64_t cd_html_dev_count(void);
const char *cd_html_dev_last(void);

/* The event types the render listened to, as the last two arguments of the page's
 * cdom.resume call (shim/cdom.js): the built-in types, then the custom names (caught in the
 * capture phase), e.g. "['click'],['cdping']". Names are checked against [A-Za-z0-9_.:-] when
 * listened to, so the text is a safe script literal. Kept for the process (the core installs a
 * type once). Same return rule as cd_html_write. */
uint32_t cd_html_resume_events(char *dst, uint32_t cap);

/* host calls (cdom.h) since the last cd_html_reset, by kind (CD_HOST_MOUNT..UNMOUNT).
 * On the server a widget does not run: host calls are counted and their values decoded and
 * checked, and only PROP leaves a trace: value, checked, selected and
 * disabled become the attribute (the text of a textarea, the selected option of a select)
 * a no-JS page shows. Every other PROP, MOUNT, CALL, SET and UNMOUNT writes nothing. */
uint64_t cd_html_host_ops(uint32_t kind);

/* The last host call as text, "<kind> <id> <name> <value>", the value printed by the sink's
 * cdv decoder: null, true, false, numbers as JavaScript's String(number), "string" with
 * \ and " escaped, f64[a,b] for F64S, [a,b], {key:value}. Same return rule as cd_html_write. */
uint32_t cd_html_host_last(char *dst, uint32_t cap);

#endif
