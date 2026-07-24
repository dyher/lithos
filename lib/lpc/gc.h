#ifndef LITHOS_GC_H
#define LITHOS_GC_H

#include <stdint.h>
#include <stddef.h>

/* Forward declarations */
struct array_s;
struct mapping_s;
struct object_s;

/* GC color constants */
#define GC_WHITE  0
#define GC_GRAY   1
#define GC_BLACK  2

/* GC tuning parameters */
#define GC_MARK_STEPS    512   /* max gray objects to process per tick */
#define GC_SWEEP_STEPS   256   /* max white objects to reclaim per alloc */

/*
 * Gray list node - intrusive, uses gc_color field as next pointer tag.
 * We store the next pointer separately to avoid polluting existing structs.
 */
typedef struct gc_gray_node {
    void *obj;                  /* pointer to array_t/mapping_t/object_t */
    uint8_t obj_type;           /* T_ARRAY, T_MAPPING, T_OBJECT */
    struct gc_gray_node *next;
} gc_gray_node_t;

/* Object type tags for gray list */
#define GC_OBJ_ARRAY    1
#define GC_OBJ_MAPPING  2
#define GC_OBJ_OBJECT   3

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Phase 1a: Write barrier stub (no-op).
 * Phase 1b will activate this.
 */
static inline void gc_write_barrier(void *src, void *dst) {
    (void)src; (void)dst;
    /* No-op: precise Yuasa barrier is in svalue.cpp assign_svalue_no_free.
     * This stub exists for API compatibility with other call sites. */
}

/* Gray list operations */
void gc_gray_list_init(void);
void gc_gray_push(void *obj, uint8_t obj_type);
gc_gray_node_t *gc_gray_pop(void);
int gc_gray_list_empty(void);

/* Incremental marker */
void gc_incremental_mark(int steps);

/* Mark children of a gray object (pushes new grays) */
void gc_mark_children(void *obj, uint8_t obj_type);

/* Root set marking - call at start of each GC cycle */
void gc_mark_roots(void);

/* Stats */
#ifdef GC_STATS
extern int gc_total_marked;
extern int gc_total_cycles;
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* LITHOS_GC_H */
