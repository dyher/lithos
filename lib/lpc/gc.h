#ifndef LITHOS_GC_H
#define LITHOS_GC_H

#include <stdint.h>

/* GC color constants */
#define GC_WHITE  0
#define GC_GRAY   1
#define GC_BLACK  2

/*
 * Phase 1a: Write barrier stub (no-op).
 * Future phases will implement tri-color incremental marking here.
 * The compiler will optimize this away completely.
 */
static inline void gc_write_barrier(void *src, void *dst) {
    (void)src;
    (void)dst;
    /* TODO Phase 1b: check src->gc_color == BLACK && dst->gc_color == WHITE */
}

#endif /* LITHOS_GC_H */
