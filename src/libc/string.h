/* string.h for a cdom app's program. A libc belongs to the final program, never to a library: in a
 * cdom app the program is the app's wasm build, and clang's wasm32 target has no sysroot, so cdom
 * provides what the program needs. Put this directory on the include path of every file the program
 * builds (-isystem path/to/cdom/src/libc), and a library that includes <string.h> (a client
 * written for any C target) compiles as it is.
 *
 * It offers these five and size_t and NULL, nothing more; no other function or header of libc.
 *   memcpy, memmove, memset: sent to the compiler's builtins, so a call is a memory.copy or a
 *     memory.fill instruction in place (wasm bulk memory, which every cdom build has), even under
 *     -ffreestanding, where clang would otherwise emit a call to a function no one defines. Weak real
 *     functions in mini.c cover what the macros cannot: a name not followed by "(", as in &memcpy or
 *     (memcpy)(...), and a file built without this header.
 *   memcmp, strlen: real functions, weak, in mini.c (link it), so a program with its own libc keeps
 *     its own. */
#ifndef CDOM_LIBC_STRING_H
#define CDOM_LIBC_STRING_H
#define __need_size_t
#define __need_NULL
#include <stddef.h>

void *memcpy(void *restrict dst, const void *restrict src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);

#define memcpy(dst, src, n) __builtin_memcpy((dst), (src), (n))
#define memmove(dst, src, n) __builtin_memmove((dst), (src), (n))
#define memset(dst, c, n) __builtin_memset((dst), (c), (n))
#endif
