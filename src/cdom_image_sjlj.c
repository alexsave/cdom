/* cdom_image's way out of a fatal libjpeg error (cdom_image_int.h, cd_image_guard and cd_image_throw): libjpeg
 * calls its error exit, which must not return, from deep inside the decoder; the guard is where it lands.
 * The only file of cdom that needs this, so the only one built with the flags each way needs (cdom.mk).
 *
 *   native            the C library's setjmp and longjmp (tests, fuzz)
 *   CDOM_IMAGE_SJLJ_JS  (cdom.mk's CDOM_IMAGE_SJLJ := js) a try and catch in the shim: the guard calls the import
 *                     image_call, which calls the export cd_image_run (body(ctx)) inside a JavaScript try; the throw
 *                     calls the import image_throw, which throws. Plain wasm (any browser cdom runs in, and wasm2js);
 *                     the guard puts back the stack pointer the unwound frames left behind.
 *   CDOM_IMAGE_SJLJ_EH  (CDOM_IMAGE_SJLJ := eh) setjmp and longjmp lowered to wasm exception handling by clang
 *                     (-mexception-handling -mllvm -wasm-enable-sjlj -mllvm -wasm-use-legacy-eh=true on this
 *                     file and cdom_image_sjlj_eh.c, the runtime that lowering calls). Needs Chrome 95, Firefox 100,
 *                     Safari 15.2; no wasm2js. */
#include <stdint.h>
#include "cdom_image_int.h"

#if !defined(__wasm__)
#include <setjmp.h>
int cd_image_guard(cd_image_body body, void *ctx, void **slot) {
  jmp_buf jb;
  *slot = &jb;
  if (setjmp(jb)) return 1;
  body(ctx);
  return 0;
}
_Noreturn void cd_image_throw(void *guard) { longjmp(*(jmp_buf *)guard, 1); }

#elif defined(CDOM_IMAGE_SJLJ_JS)
#define IMPORT(n) __attribute__((import_module("cdom"), import_name(n)))
IMPORT("image_call") uint32_t j_image_call(cd_image_body body, void *ctx);
IMPORT("image_throw") _Noreturn void j_image_throw(void);

static inline uintptr_t sp_get(void) {
  uintptr_t sp;
  __asm__ volatile(".globaltype __stack_pointer, i32\n\tglobal.get __stack_pointer\n\tlocal.set %0" : "=r"(sp));
  return sp;
}
static inline void sp_set(uintptr_t sp) {
  __asm__ volatile(".globaltype __stack_pointer, i32\n\tlocal.get %0\n\tglobal.set __stack_pointer" : : "r"(sp));
}
/* What image_call runs inside its try. */
__attribute__((export_name("cd_image_run"))) void cd_image_run(cd_image_body body, void *ctx) { body(ctx); }

int cd_image_guard(cd_image_body body, void *ctx, void **slot) {
  *slot = 0;
  uintptr_t sp = sp_get();
  uint32_t thrown = j_image_call(body, ctx);
  sp_set(sp);                                   /* the frames the throw unwound never put theirs back */
  return thrown != 0;
}
_Noreturn void cd_image_throw(void *guard) { (void)guard; j_image_throw(); }

#elif defined(CDOM_IMAGE_SJLJ_EH)
/* clang lowers the setjmp and longjmp below and calls the runtime in cdom_image_sjlj_eh.c, which must be a file of
 * its own (the lowering renames a runtime it finds in the file it lowers). */
typedef struct { void *id; uint32_t label; struct { void *env; int val; } arg; } cd_image_jmp_buf[1];
int setjmp(cd_image_jmp_buf env) __attribute__((returns_twice));
_Noreturn void longjmp(cd_image_jmp_buf env, int val);

int cd_image_guard(cd_image_body body, void *ctx, void **slot) {
  cd_image_jmp_buf jb;
  *slot = jb;
  if (setjmp(jb)) return 1;
  body(ctx);
  return 0;
}
_Noreturn void cd_image_throw(void *guard) { longjmp(guard, 1); }

#else
#error "cdom_image_sjlj.c: a wasm build defines CDOM_IMAGE_SJLJ_JS or CDOM_IMAGE_SJLJ_EH (cdom.mk's CDOM_IMAGE_SJLJ)"
#endif
