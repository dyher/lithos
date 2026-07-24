#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "../src/std.h"
#include "fiber.h"
#include "gc.h"
#include "lpc/object.h"
#include "../src/interpret.h"
#include "../src/simulate.h"
#include <stdlib.h>
#include <string.h>

#define DEFAULT_FIBER_STACK (64 * 1024)  /* 64KB per fiber */
#define MAX_FIBERS 4096

static lpc_fiber_t *ready_queue_head = NULL;
static lpc_fiber_t *all_fibers = NULL;   /* for GC root scanning */
static lpc_fiber_t *current_fiber = NULL;
static int num_fibers = 0;

/* Main thread context (for switching back from fibers) */
static fiber_ctx_t main_ctx = {0};
static int main_ctx_valid = 0;

void fiber_subsystem_init(void) {
    ready_queue_head = NULL;
    all_fibers = NULL;
    current_fiber = NULL;
    num_fibers = 0;
    main_ctx_valid = 0;
}

lpc_fiber_t *fiber_create(void (*entry_fn)(void *), void *arg, size_t stack_size) {
    if (num_fibers >= MAX_FIBERS) return NULL;
    if (stack_size == 0) stack_size = DEFAULT_FIBER_STACK;

    lpc_fiber_t *fib = (lpc_fiber_t *)calloc(1, sizeof(lpc_fiber_t));
    if (!fib) return NULL;

    if (!fiber_stack_alloc(&fib->ctx, stack_size)) {
        free(fib);
        return NULL;
    }

    fib->state = FIBER_STATE_READY;
    fib->entry_fn = entry_fn;
    fib->entry_arg = arg;
    fib->saved_sp = NULL;
    fib->saved_fp = NULL;
    fib->saved_csp = NULL;
    fib->saved_this_player = NULL;
    fib->saved_eval_cost = 0;

    /* Add to all_fibers list (for GC) */
    fib->next = all_fibers;
    all_fibers = fib;
    num_fibers++;

    /* Prepare fiber context with entry trampoline */
    fiber_make(&fib->ctx, entry_fn, arg);

    return fib;
}

void fiber_yield(void) {
    if (!current_fiber) return;  /* not in a fiber */

    /* Save VM state */
    extern svalue_t *sp, *fp;
    extern control_stack_t *csp;
    extern object_t *current_object;

    current_fiber->saved_sp = sp;
    current_fiber->saved_fp = fp;
    current_fiber->saved_csp = csp;
    current_fiber->saved_this_player = current_object;
    current_fiber->state = FIBER_STATE_SUSPENDED;

    /* Switch back to main or next fiber */
    current_fiber = NULL;
    if (main_ctx_valid) {
        fiber_switch(current_fiber ? &current_fiber->ctx : &main_ctx, &main_ctx);
    }
}

void fiber_resume(lpc_fiber_t *fib) {
    if (!fib || (fib->state != FIBER_STATE_SUSPENDED && fib->state != FIBER_STATE_READY))
        return;

    /* Save main context if not already done */
    if (!main_ctx_valid) {
        main_ctx.sp = NULL;
        main_ctx_valid = 1;
    }

    lpc_fiber_t *prev = current_fiber;
    current_fiber = fib;
    fib->state = FIBER_STATE_RUNNING;

    /* Switch to fiber */
    fiber_ctx_t *from_ctx = prev ? &prev->ctx : &main_ctx;
    fiber_switch(from_ctx, &fib->ctx);

    /* Returned from fiber (it yielded or finished) */
    if (current_fiber && current_fiber->state == FIBER_STATE_RUNNING) {
        current_fiber->state = FIBER_STATE_SUSPENDED;
    }
}

void fiber_destroy(lpc_fiber_t *fib) {
    if (!fib) return;

    /* Remove from all_fibers list */
    if (all_fibers == fib) {
        all_fibers = fib->next;
    } else {
        lpc_fiber_t *prev = all_fibers;
        while (prev && prev->next != fib) prev = prev->next;
        if (prev) prev->next = fib->next;
    }

    fiber_stack_free(&fib->ctx);
    free(fib);
    num_fibers--;
}

void fiber_scheduler_tick(int max_fibers) {
    /* Simple round-robin: resume ready/suspended fibers */
    int ran = 0;
    lpc_fiber_t *fib = all_fibers;
    while (fib && ran < max_fibers) {
        if (fib->state == FIBER_STATE_READY || fib->state == FIBER_STATE_SUSPENDED) {
            fiber_resume(fib);
            ran++;
        }
        fib = fib->next;
    }
}

lpc_fiber_t *fiber_current(void) {
    return current_fiber;
}

int fiber_count(void) {
    return num_fibers;
}

/* === GC Root Integration === */
/* Called from gc_mark_roots() to mark all suspended fiber stacks */
void gc_mark_fiber_roots(void) {
    lpc_fiber_t *fib = all_fibers;
    while (fib) {
        if (fib->state == FIBER_STATE_SUSPENDED && fib->saved_sp && fib->saved_fp) {
            /* Mark saved eval stack svalues */
            svalue_t *sv = fib->saved_fp;
            svalue_t *end = fib->saved_sp;
            if (sv && end && sv <= end) {
                while (sv <= end) {
                    /* Inline mark_sv logic to avoid circular deps */
                    switch (sv->type) {
                    case T_ARRAY:
                        if (sv->u.arr) gc_gray_push(sv->u.arr, 1);
                        break;
                    case T_MAPPING:
                        if (sv->u.map) gc_gray_push(sv->u.map, 2);
                        break;
                    case T_OBJECT:
                        if (sv->u.ob && !(sv->u.ob->flags & O_DESTRUCTED))
                            gc_gray_push(sv->u.ob, 3);
                        break;
                    default:
                        break;
                    }
                    sv++;
                }
            }
            /* Mark saved this_player */
            if (fib->saved_this_player) {
                object_t *ob = (object_t *)fib->saved_this_player;
                if (!(ob->flags & O_DESTRUCTED))
                    gc_gray_push(ob, 3);
            }
        }
        fib = fib->next;
    }
}
