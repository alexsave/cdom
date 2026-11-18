/* The build's entry, by its define (build.sh passes one): each file is one way for views to follow state. */
#if defined(ENTRY_A)
#include "entry_a.c"
#elif defined(ENTRY_B)
#include "entry_b.c"
#elif defined(ENTRY_C) || defined(ENTRY_CL)
#include "entry_c.c"
#elif defined(ENTRY_E)
#include "entry_e.c"
#elif defined(ENTRY_L)
#include "entry_l.c"
#else
#error "build with one of -DENTRY_A, -DENTRY_B, -DENTRY_C, -DENTRY_CL, -DENTRY_E, -DENTRY_L"
#endif
