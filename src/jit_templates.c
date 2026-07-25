
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "jit_templates.h"
#include "jit_emit.h"
#include "interpret.h"
#include "../lib/lpc/svalue.h"
#include "../lib/lpc/types.h"
#include <string.h>
#include <stdio.h>

/* We need the auto-generated opcode values */
#include "efuns_opcode.h"

/* === Template Implementation Strategy ===
 *
 * Each template is a small C function that implements one opcode.
 * The JIT compiler will later inline/concatenate these into native code.
 * For J1b, we register them as callable templates and verify correctness.
 *
 * All templates operate on global sp/fp/pc directly.
 */

/* --- F_CONST0 (opcode 15): push integer 0 --- */
static void tmpl_const0(void) {
    (++sp)->type = T_NUMBER;
    sp->u.number = 0;
}

/* --- F_CONST1 (opcode 16): push integer 1 --- */
static void tmpl_const1(void) {
    (++sp)->type = T_NUMBER;
    sp->u.number = 1;
}

/* --- F_LOCAL (opcode 61): push local variable (1-byte index follows) --- */
static void tmpl_local(void) {
    unsigned char idx = (unsigned char)(*pc++);
    svalue_t *src = fp + idx;
    (++sp)->type = src->type;
    sp->u = src->u;
    /* Note: for refcounted types, real impl needs assign_svalue_no_free.
     * This simplified version works for T_NUMBER which is most common. */
}

/* --- F_GLOBAL (opcode 63): push global variable (2-byte index follows) --- */
static void tmpl_global(void) {
    unsigned short idx = (unsigned char)pc[0] | ((unsigned char)pc[1] << 8);
    pc += 2;
    /* Global access requires current_prog->variable_index - too complex for template.
     * Skip for J1b, handle in full JIT compiler later. */
    (void)idx;
}

/* --- F_RETURN (opcode 46): return top of stack --- */
static void tmpl_return(void) {
    /* Simplified: just signal return. Real impl manipulates csp/frame. */
    /* For J1b verification, we just mark it as available. */
}

/* --- F_RETURN_ZERO (opcode 47): return 0 --- */
static void tmpl_return_zero(void) {
    /* Push 0 and return */
    (++sp)->type = T_NUMBER;
    sp->u.number = 0;
}

/* --- F_BRANCH (opcode 21): unconditional jump (signed 2-byte offset) --- */
static void tmpl_branch(void) {
    short offset = (unsigned char)pc[0] | ((unsigned char)pc[1] << 8);
    pc += offset;
}

/* --- F_ADD_INT: add two integers on stack --- */
/* Find the actual opcode value first - may not exist as separate opcode */

/* === Registration === */

/* Helper: copy function code bytes into JIT pool as a template.
 * Since we can't easily extract C function machine code portably,
 * we store the function pointer and call it via indirect call.
 * J1c/J2 will replace this with true inline native code emission. */

typedef void (*template_fn_t)(void);

typedef struct {
    uint8_t opcode;
    template_fn_t fn;
    const char *name;
    int stack_delta;
    int has_side_effect;
} template_def_t;

static template_def_t template_defs[] = {
    { 15, tmpl_const0,      "F_CONST0",      +1, 0 },
    { 16, tmpl_const1,      "F_CONST1",      +1, 0 },
    { 61, tmpl_local,       "F_LOCAL",       +1, 0 },
    { 63, tmpl_global,      "F_GLOBAL",      +1, 0 },
    { 46, tmpl_return,      "F_RETURN",       0, 1 },
    { 47, tmpl_return_zero, "F_RETURN_ZERO", +1, 1 },
    { 21, tmpl_branch,      "F_BRANCH",       0, 0 },
};

#define NUM_TEMPLATE_DEFS (sizeof(template_defs) / sizeof(template_defs[0]))

void jit_init_templates(void) {
    if (!jit_enabled()) return;

    jit_emit_init();

    int registered = 0;

    /* J2: Emit true native code for CONST0 and CONST1 */
    void *const0_code = jit_emit_const0();
    if (const0_code) {
        jit_register_template(15, (uint8_t *)const0_code, 72, +1, 0);
        registered++;
    }

    void *const1_code = jit_emit_const1();
    if (const1_code) {
        jit_register_template(16, (uint8_t *)const1_code, 80, +1, 0);
        registered++;
    }

    void *ret0_code = jit_emit_return_zero();
    if (ret0_code) {
        jit_register_template(47, (uint8_t *)ret0_code, 36, +1, 0);
        registered++;
    }

    void *local_code = jit_emit_local();
    if (local_code) {
        jit_register_template(61, (uint8_t *)local_code, 88, +1, 0);
        registered++;
    }

    /* F_ADD integer fast path - registered with special opcode marker */
    void *add_int_code = jit_emit_add_int_fast();
    if (add_int_code) {
        /* Use opcode 255 as internal marker for ADD_INT_FAST */
        jit_register_template(255, (uint8_t *)add_int_code, 48, -1, 0);
        registered++;
    }

    /* F_BRANCH: unconditional relative jump */
    void *branch_code = jit_emit_branch();
    if (branch_code) {
        jit_register_template(21, (uint8_t *)branch_code, 40, 0, 0);
        registered++;
    }

    /* F_BRANCH_WHEN_ZERO fast path (internal marker 254) */
    void *bwz_code = jit_emit_branch_when_zero_fast();
    if (bwz_code) {
        jit_register_template(254, (uint8_t *)bwz_code, 88, -1, 0);
        registered++;
    }

    /* F_LT integer fast path (internal marker 253) */
    void *lt_code = jit_emit_lt_int_fast();
    if (lt_code) {
        jit_register_template(253, (uint8_t *)lt_code, 56, -1, 0);
        registered++;
    }

    /* F_EQ integer fast path (internal marker 252) */
    void *eq_code = jit_emit_eq_int_fast();
    if (eq_code) {
        jit_register_template(252, (uint8_t *)eq_code, 56, -1, 0);
        registered++;
    }

    /* Remaining opcodes still use function pointer templates */
    for (size_t i = 0; i < NUM_TEMPLATE_DEFS; i++) {
        template_def_t *td = &template_defs[i];
        /* Skip CONST0/CONST1 - already registered with native code */
        if (td->opcode == 15 || td->opcode == 16 || td->opcode == 47 || td->opcode == 61) continue;
        jit_register_template(
            td->opcode,
            (uint8_t *)(uintptr_t)td->fn,
            sizeof(template_fn_t),
            td->stack_delta,
            td->has_side_effect
        );
        registered++;
    }

    printf("JIT: registered %d opcode templates (native: CONST0/1, RET0, LOCAL, ADD, BR, BWZ, LT, EQ)\n", registered);
}
