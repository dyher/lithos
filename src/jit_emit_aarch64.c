
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "jit_emit.h"
#include <stdio.h>
#include <string.h>
#include "interpret.h"

/* Only compile on AArch64 */
#if defined(__aarch64__)

/* svalue_t layout (verified):
 *   offset 0: short type (2 bytes)
 *   offset 8: union u (8 bytes, int64_t number at +8)
 *   total size: 16 bytes
 * T_NUMBER = 0x2
 */
#define SVALUE_SIZE     16
#define SVALUE_TYPE_OFF 0
#define SVALUE_NUM_OFF  8
#define T_NUMBER_VAL    0x2

/* AArch64 instruction encoding helpers */
static inline uint32_t encode_ldr_x(uint32_t rt, uint32_t rn, int32_t imm) {
    /* LDR Xt, [Xn, #imm] (unsigned offset, scaled by 8) */
    return 0xF9400000 | ((imm / 8) << 10) | (rn << 5) | rt;
}

static inline uint32_t encode_str_x(uint32_t rt, uint32_t rn, int32_t imm) {
    /* STR Xt, [Xn, #imm] */
    return 0xF9000000 | ((imm / 8) << 10) | (rn << 5) | rt;
}

static inline uint32_t encode_strh_w(uint32_t rt, uint32_t rn, int32_t imm) {
    /* STRH Wt, [Xn, #imm] (unsigned offset, scaled by 2) */
    return 0x79000000 | ((imm / 2) << 10) | (rn << 5) | rt;
}

static inline uint32_t encode_add_x_imm(uint32_t rd, uint32_t rn, uint32_t imm) {
    /* ADD Xd, Xn, #imm (12-bit immediate) */
    return 0x91000000 | (imm << 10) | (rn << 5) | rd;
}

static inline uint32_t encode_mov_w_imm(uint32_t rd, uint32_t imm) {
    /* MOVZ Wd, #imm (16-bit immediate) */
    return 0x52800000 | (imm << 5) | rd;
}

static inline uint32_t encode_ret(void) {
    return 0xD65F03C0; /* RET (LR) */
}

/* sp is declared as extern svalue_t *sp in interpret.h */

void jit_emit_init(void) {
    printf("JIT: AArch64 code emitter initialized\n");
}

/* Emit F_CONST0: (++sp)->type = T_NUMBER; sp->u.number = 0;
 *
 * Calling convention: template functions are called with no args.
 * They access global `sp` directly. We load &sp from GOT/literal pool.
 *
 * Strategy: use a literal pool embedded after the code.
 *   ldr x9, [pc, #offset_to_literal]  // load &sp
 *   ldr x10, [x9]                      // x10 = sp
 *   add x10, x10, #16                  // ++sp
 *   str x10, [x9]                      // store back
 *   mov w11, #2                        // T_NUMBER
 *   strh w11, [x10, #0]               // sp->type = T_NUMBER
 *   str xzr, [x10, #8]                // sp->u.number = 0
 *   ret
 *   .quad sp                           // literal pool
 */
void *jit_emit_const0(void) {
    if (!jit_enabled()) return NULL;

    /* 8 instructions + 1 literal = 9 * 8 = 72 bytes */
    size_t code_size = 9 * sizeof(uint32_t);
    uint32_t *code = (uint32_t *)jit_alloc_code(code_size);
    if (!code) return NULL;

    /* Offset from PC to literal: after 8 instructions = 32 bytes */
    /* LDR X9, [PC, #32] => imm = 32/4 = 8 (in words for LDR literal) */
    /* LDR literal encoding: 0x58000000 | (imm19 << 5) | Rt */
    uint32_t ldr_literal = 0x58000000 | (8 << 5) | 9; /* LDR X9, [PC, #32] */

    code[0] = ldr_literal;                              /* LDR X9, [PC, #32] (&sp) */
    code[1] = encode_ldr_x(10, 9, 0);                   /* LDR X10, [X9] (sp value) */
    code[2] = encode_add_x_imm(10, 10, SVALUE_SIZE);    /* ADD X10, X10, #16 */
    code[3] = encode_str_x(10, 9, 0);                   /* STR X10, [X9] (store sp) */
    code[4] = encode_mov_w_imm(11, T_NUMBER_VAL);       /* MOVZ W11, #2 */
    code[5] = encode_strh_w(11, 10, SVALUE_TYPE_OFF);   /* STRH W11, [X10, #0] */
    code[6] = encode_str_x(31, 10, SVALUE_NUM_OFF);     /* STR XZR, [X10, #8] */
    code[7] = encode_ret();                             /* RET */
    /* Literal pool: address of global sp */
    *(uint64_t *)&code[8] = (uint64_t)&sp;              /* &sp */

    printf("JIT: emitted F_CONST0 native code (%zu bytes)\n", code_size);
    return (void *)code;
}

/* Emit F_CONST1: (++sp)->type = T_NUMBER; sp->u.number = 1; */
void *jit_emit_const1(void) {
    if (!jit_enabled()) return NULL;

    size_t code_size = 10 * sizeof(uint32_t); /* extra MOV for constant 1 */
    uint32_t *code = (uint32_t *)jit_alloc_code(code_size);
    if (!code) return NULL;

    uint32_t ldr_literal = 0x58000000 | (9 << 5) | 9; /* LDR X9, [PC, #36] */

    code[0] = ldr_literal;                              /* LDR X9, [PC, #36] (&sp) */
    code[1] = encode_ldr_x(10, 9, 0);                   /* LDR X10, [X9] */
    code[2] = encode_add_x_imm(10, 10, SVALUE_SIZE);    /* ADD X10, X10, #16 */
    code[3] = encode_str_x(10, 9, 0);                   /* STR X10, [X9] */
    code[4] = encode_mov_w_imm(11, T_NUMBER_VAL);       /* MOVZ W11, #2 */
    code[5] = encode_strh_w(11, 10, SVALUE_TYPE_OFF);   /* STRH W11, [X10, #0] */
    code[6] = encode_mov_w_imm(11, 1);                  /* MOVZ W11, #1 */
    code[7] = encode_str_x(11, 10, SVALUE_NUM_OFF);     /* STR X11, [X10, #8] */
    code[8] = encode_ret();                             /* RET */
    *(uint64_t *)&code[9] = (uint64_t)&sp;              /* &sp */

    printf("JIT: emitted F_CONST1 native code (%zu bytes)\n", code_size);
    return (void *)code;
}

#else /* !__aarch64__ */

void jit_emit_init(void) {
    printf("JIT: no native emitter for this platform\n");
}
void *jit_emit_const0(void) { return NULL; }
void *jit_emit_const1(void) { return NULL; }

#endif /* __aarch64__ */
