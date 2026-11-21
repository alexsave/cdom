/* The module's exports for the browser host (host.js): the renderer of render.h, a batch of rows at a time. */
#include "render.h"
#define EXPORT(n) __attribute__((export_name(n)))
EXPORT("rs_begin") void rs_begin(int page, int state) { rd_begin(page, state); }
EXPORT("rs_rows") int rs_rows(int n) { return rd_rows(n); }
EXPORT("rs_out") int rs_out(void) { return (int)rd_out; }
EXPORT("rs_nout") int rs_nout(void) { return rd_nout; }
EXPORT("rs_at") void rs_at(int y) { rd_at(y); }
EXPORT("rs_y") int rs_y(void) { return rd_y(); }
EXPORT("rs_dirty") int rs_dirty(int page) { return rd_dirty(page); }
EXPORT("rs_ramp") void rs_ramp(int shift) { rd_ramp_shift = shift; }
