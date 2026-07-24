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

/* GC cycle state machine */
typedef enum {
    GC_PHASE_IDLE = 0,     /* No GC in progress */
    GC_PHASE_MARKING = 1,  /* Incremental mark in progress */
    GC_PHASE_SWEEPING = 2  /* Lazy sweep in progress */
} gc_phase_t;

extern gc_phase_t gc_current_phase;

/* Call at start of a new GC cycle (from heartbeat when idle) */
void gc_begin_cycle(void);
void gc_tick(void);

/* Lazy sweep: reclaim white objects during alloc. Returns bytes freed. */
size_t gc_lazy_sweep(int steps);

/* Check if GC is active (for write barrier fast-path) */
static inline int gc_is_active(void) {
    return gc_current_phase != GC_PHASE_IDLE;
}

/* === Generational Nursery === */
#define GC_NURSERY_SIZE (4 * 1024 * 1024)  /* 4MB young gen */
#define GC_MINOR_GC_INTERVAL 5             /* minor GC every N ticks */

typedef struct {
    uint8_t *base;          /* nursery memory base */
    uint8_t *top;           /* next allocation point (bump ptr) */
    uint8_t *limit;         /* end of nursery */
    int minor_gc_count;     /* tick counter for minor GC trigger */
    size_t total_allocated; /* lifetime bytes allocated in nursery */
    size_t total_promoted;  /* lifetime bytes promoted to old gen */
} gc_nursery_t;

extern gc_nursery_t gc_nursery;

/* Initialize nursery (call once at startup) */
void gc_nursery_init(void);

/* Allocate from nursery. Returns NULL if nursery full (caller falls back to malloc). */
void *gc_nursery_alloc(size_t size);

/* Check if pointer is inside nursery */
static inline int gc_in_nursery(const void *p) {
    const uint8_t *ptr = (const uint8_t *)p;
    return ptr >= gc_nursery.base && ptr < gc_nursery.top;
}

/* Run minor GC: scan nursery, promote survivors, reset nursery */
void gc_minor_collect(void);

/* Called from gc_tick to check minor GC schedule */
void gc_nursery_tick(void);

#endif /* LITHOS_GC_H */
