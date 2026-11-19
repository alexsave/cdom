/* heap-regress probe: cdom.c of the checkout named by CDOM_SRC, plus one export that reports the heap.
 * cd_heap_probe(out) writes 8 words: heap_lo, heap_ptr, heap_end, the bytes on the small free lists, the
 * bytes in free large runs (0 before #43), the number of runs, the largest run, the blocks on the free lists. */
#include CDOM_SRC
EXPORT("cd_heap_probe") void cd_heap_probe(uint32_t *out) {
  uint32_t small = 0, nsmall = 0, big = 0, nbig = 0, top = 0;
  for (uint32_t c = 0; c < 32; c++)
    for (void **p = free_lists[c]; p; p = *p) small += 1u << c, nsmall++;
#ifdef HEAP_BIG
  for (heap_run *r = runs; r; r = r->next) { big += r->size; nbig++; if (r->size > top) top = r->size; }
#endif
  out[0] = heap_lo; out[1] = heap_ptr; out[2] = heap_end; out[3] = small;
  out[4] = big; out[5] = nbig; out[6] = top; out[7] = nsmall;
}
