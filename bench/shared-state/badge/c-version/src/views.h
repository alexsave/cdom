#pragma once
typedef void (*view_fn)(void);
void view_add(view_fn fn);  /* call while the view's region builds; it is dropped when the region goes */
void views_changed(void);   /* some state changed: every live view runs once, at the flush */
