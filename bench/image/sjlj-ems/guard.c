/* The spike's third way out of a libjpeg error, emscripten's lowering (-mllvm -enable-emscripten-sjlj), built only by
 * bench/image/sjlj-ems/build.sh for bench/image/sjlj.mjs: never part of the module. */
#include <stdint.h>
#include "cdom_image_int.h"
typedef struct { uint32_t a[6]; } jb_t[1];
int setjmp(jb_t) __attribute__((returns_twice));
_Noreturn void longjmp(jb_t, int);
int cd_image_guard(cd_image_body body, void *ctx, void **slot) { jb_t jb; *slot = jb; if (setjmp(jb)) return 1; body(ctx); return 0; }
_Noreturn void cd_image_throw(void *g) { longjmp(g, 1); }
