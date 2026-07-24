#ifndef LITHOS_FIBER_PORT_H
#define LITHOS_FIBER_PORT_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    void *sp;
    void *stack_base;
    size_t stack_size;
} fiber_ctx_t;
int fiber_stack_alloc(fiber_ctx_t *ctx, size_t size);
void fiber_stack_free(fiber_ctx_t *ctx);
void fiber_switch(fiber_ctx_t *from, fiber_ctx_t *to);
void fiber_make(fiber_ctx_t *ctx, void (*entry_fn)(void *), void *arg);
#endif
