/* The runtime emscripten's lowering calls (seven symbols and an import), for the spike only (guard.c). */
#include <stdint.h>
typedef struct { void *id; uint32_t label; } jmp_impl;
uintptr_t __THREW__; int __threwValue; static int temp_ret0;
int getTempRet0(void) { return temp_ret0; }
void setTempRet0(int v) { temp_ret0 = v; }
void __wasm_setjmp(void *env, uint32_t label, void *id) { jmp_impl *jb = env; jb->id = id; jb->label = label; }
uint32_t __wasm_setjmp_test(void *env, void *id) { jmp_impl *jb = env; return jb->id == id ? jb->label : 0; }
__attribute__((import_module("env"), import_name("ems_throw"))) _Noreturn void ems_throw(void);
void emscripten_longjmp(uintptr_t env, int val) { __THREW__ = env; __threwValue = val; ems_throw(); }
