#ifndef LITHOS_FIBER_H
#define LITHOS_FIBER_H

#include "port/fiber_port.h"
#include "lpc/svalue.h"

typedef enum {
    FIBER_STATE_READY = 0,
    FIBER_STATE_RUNNING = 1,
    FIBER_STATE_SUSPENDED = 2,
    FIBER_STATE_DEAD = 3
} fiber_state_t;

typedef struct lpc_fiber_s {
    fiber_ctx_t ctx;           /* platform context (sp, stack_base, stack_size) */
    fiber_state_t state;
    struct lpc_fiber_s *next;  /* scheduler queue link */

    /* Saved VM state (valid when SUSPENDED) */
    svalue_t *saved_sp;
    svalue_t *saved_fp;
    void *saved_csp;           /* control_stack_t* but avoid circular include */
    void *saved_this_player;   /* object_t* */
    int saved_eval_cost;

    /* Fiber entry point info */
    void (*entry_fn)(void *);
    void *entry_arg;
} lpc_fiber_t;

/* Initialize fiber subsystem */
void fiber_subsystem_init(void);

/* Create a new fiber. Returns NULL on failure. */
lpc_fiber_t *fiber_create(void (*entry_fn)(void *), void *arg, size_t stack_size);

/* Yield current fiber, switch to next ready fiber or main */
void fiber_yield(void);

/* Resume a suspended fiber */
void fiber_resume(lpc_fiber_t *fib);

/* Destroy a fiber and free its stack */
void fiber_destroy(lpc_fiber_t *fib);

/* Scheduler: run up to max_fibers fibers this tick. Called from heartbeat. */
void fiber_scheduler_tick(int max_fibers);

/* Get current running fiber (NULL if on main thread) */
lpc_fiber_t *fiber_current(void);

/* Count of live fibers */
int fiber_count(void);

#endif /* LITHOS_FIBER_H */
