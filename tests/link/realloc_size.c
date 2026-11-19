/* realloc shares malloc's body rather than carrying a copy of it: built with -DREALLOC the app grows a
 * block with cd_realloc, without it takes a new one with cd_alloc (run.sh bounds the gap). Nothing else
 * here reaches realloc (no cd_on, whose tables grow with it). */
#include "../../src/cdom.h"
/* The sizes are read at run time, so neither build knows them: both carry the small path and the large one. */
static volatile uint32_t size = 1u << 20;
static void *volatile kept;
void cd_main(void) {
#ifdef REALLOC
  kept = cd_realloc(cd_alloc(16), size);
#else
  kept = cd_alloc(size);
#endif
  cd_open("div"); cd_text_num((uint32_t)(uintptr_t)kept); cd_close();
}
