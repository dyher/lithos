#ifndef LITHOS_JIT_H
#define LITHOS_JIT_H

#include "std.h"
#include <stdint.h>
#include <stddef.h>

/* === JIT Configuration === */
#define JIT_CODE_POOL_SIZE   (4 * 1024 * 1024)  /* 4MB native code pool */
#define JIT_MAX_FUNCTIONS    8192                /* max JIT-compiled functions */
#define JIT_HOT_THRESHOLD    100                 /* call count before JIT trigger */
#define JIT_TEMPLATE_MAX     128                 /* max opcodes with templates */

/* === Code Buffer === */
typedef struct jit_code_buffer_s {
    uint8_t *base;          /* mmap'd RWX region */
    size_t   capacity;      /* total size */
    size_t   offset;        /* next free byte */
} jit_code_buffer_t;

/* === JIT Template (per-opcode native code snippet) === */
typedef struct jit_template_s {
    uint8_t  opcode;        /* F_xxx opcode this template handles */
    uint8_t *code;          /* pointer into code_buffer */
    size_t   code_size;     /* bytes of native code */
    int      stack_delta;   /* net svalue_t stack change (+/-) */
    int      has_side_effect; /* 1 if may call efun / error */
} jit_template_t;

/* === JIT-compiled Function === */
typedef struct jit_function_s {
    void       *entry;      /* native code entry point (in code_buffer) */
    const char *prog_name;  /* source program name (for debug) */
    int         func_index; /* function index in program */
    uint32_t    call_count; /* invocation counter */
    size_t      code_size;  /* bytes of native code */
} jit_function_t;

/* === Public API === */

/* Initialize JIT subsystem. Call once at startup. */
void jit_init(void);

/* Shutdown JIT subsystem. Free all native code. */
void jit_shutdown(void);

/* Allocate n bytes from JIT code pool. Returns NULL if exhausted. */
void *jit_alloc_code(size_t n);

/* Get current code pool usage stats. */
size_t jit_code_used(void);
size_t jit_code_capacity(void);

/* Register a template for an opcode. */
void jit_register_template(uint8_t opcode, uint8_t *code, size_t size,
                           int stack_delta, int has_side_effect);

/* Lookup template for opcode. Returns NULL if not JIT'd. */
jit_template_t *jit_get_template(uint8_t opcode);

/* Register a compiled function. Returns slot index or -1. */
int jit_register_function(void *entry, const char *prog_name,
                          int func_index, size_t code_size);

/* Lookup compiled function by prog+index. Returns NULL if not JIT'd. */
jit_function_t *jit_get_function(const char *prog_name, int func_index);

/* Increment call count; returns new count. */
uint32_t jit_record_call(const char *prog_name, int func_index);

/* Check if JIT is enabled (can be disabled via config). */
int jit_enabled(void);

#endif /* LITHOS_JIT_H */
