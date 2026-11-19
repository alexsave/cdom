/* The runtime clang 23's -wasm-enable-sjlj lowering calls, for cdom_image_sjlj.c's CDOM_IMAGE_SJLJ_EH build (cdom.mk
 * links this file only then). After wasi-libc's (libc-top-half/musl/src/setjmp/wasm32/rt.c): __wasm_setjmp records
 * the setjmp's label and its function invocation's id in the jmp_buf; __wasm_setjmp_test answers the label when a
 * longjmp's buffer is this invocation's (else 0, and the catch rethrows); __wasm_longjmp throws the C longjmp tag
 * with a pointer to the buffer's own (env, val) pair, which outlives the frame that throws. Every value lives in
 * the jmp_buf: no global. The tag itself, __c_longjmp, is defined here, in assembly (the lowering only names it). */
#include <stdint.h>

typedef struct { void *id; uint32_t label; struct { void *env; int val; } arg; } jmp_impl;

__asm__(".globl __c_longjmp\n.tagtype __c_longjmp i32\n__c_longjmp:\n");

void __wasm_setjmp(void *env, uint32_t label, void *id) {
  jmp_impl *jb = env;
  if (!label || !id) __builtin_trap();
  jb->id = id;
  jb->label = label;
}
uint32_t __wasm_setjmp_test(void *env, void *id) {
  jmp_impl *jb = env;
  return jb->id == id ? jb->label : 0;
}
void __wasm_longjmp(void *env, int val) {
  jmp_impl *jb = env;
  jb->arg.env = env;
  jb->arg.val = val ? val : 1;
  __builtin_wasm_throw(1, &jb->arg);   /* tag 1: the C longjmp tag */
}
