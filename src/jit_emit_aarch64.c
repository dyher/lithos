
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

/* F_RETURN_ZERO: push T_NUMBER 0 (same semantics as CONST0) */
void *jit_emit_return_zero(void) {
    if (!jit_enabled()) return NULL;
    size_t code_size = 9 * sizeof(uint32_t);
    uint32_t *code = (uint32_t *)jit_alloc_code(code_size);
    if (!code) return NULL;
    uint32_t ldr_lit = 0x58000000 | (8 << 5) | 9;
    code[0] = ldr_lit;
    code[1] = encode_ldr_x(10, 9, 0);
    code[2] = encode_add_x_imm(10, 10, SVALUE_SIZE);
    code[3] = encode_str_x(10, 9, 0);
    code[4] = encode_mov_w_imm(11, T_NUMBER_VAL);
    code[5] = encode_strh_w(11, 10, SVALUE_TYPE_OFF);
    code[6] = encode_str_x(31, 10, SVALUE_NUM_OFF);
    code[7] = encode_ret();
    *(uint64_t *)&code[8] = (uint64_t)&sp;
    printf("JIT: emitted F_RETURN_ZERO native code (%zu bytes)\n", code_size);
    return (void *)code;
}

/* F_LOCAL: (++sp) = fp[*pc++] — copies raw 16 bytes via LDP/STP */
void *jit_emit_local(void) {
    if (!jit_enabled()) return NULL;
    extern const char *pc;
    /* 15 instructions + 1 NOP pad + 3 uint64_t literals = 22 words */
    size_t n_words = 22;
    size_t code_size = n_words * sizeof(uint32_t);
    uint32_t *code = (uint32_t *)jit_alloc_code(code_size);
    if (!code) return NULL;
    /* Literals at word indices 16-17 (&sp), 18-19 (&fp), 20-21 (&pc) */
    /* LDR literal imm19 = (lit_word - instr_word) */
    code[0]  = 0x58000000 | (16 << 5) | 9;   /* LDR X9, [PC,#64] &sp */
    code[1]  = 0x58000000 | (17 << 5) | 10;  /* LDR X10,[PC,#64] &fp */
    code[2]  = 0x58000000 | (18 << 5) | 8;   /* LDR X8, [PC,#64] &pc */
    code[3]  = encode_ldr_x(11, 9, 0);        /* LDR X11,[X9] sp */
    code[4]  = encode_ldr_x(12, 10, 0);       /* LDR X12,[X10] fp */
    code[5]  = encode_ldr_x(14, 8, 0);        /* LDR X14,[X8] pc */
    code[6]  = 0x394001CD;                     /* LDRB W13,[X14] idx */
    code[7]  = encode_add_x_imm(14, 14, 1);   /* ADD X14,X14,#1 pc++ */
    code[8]  = encode_str_x(14, 8, 0);         /* STR X14,[X8] store pc */
    code[9]  = 0x8B2D618C;                     /* ADD X12,X12,X13,LSL#4 */
    code[10] = encode_add_x_imm(11, 11, SVALUE_SIZE); /* ADD X11,X11,#16 */
    code[11] = encode_str_x(11, 9, 0);         /* STR X11,[X9] store sp */
    code[12] = 0xA940018D;                     /* LDP X13,X14,[X12] */
    code[13] = 0xA900016D;                     /* STP X13,X14,[X11] */
    code[14] = encode_ret();
    code[15] = 0xD503201F;                     /* NOP pad */
    *(uint64_t *)&code[16] = (uint64_t)&sp;
    *(uint64_t *)&code[18] = (uint64_t)&fp;
    *(uint64_t *)&code[20] = (uint64_t)&pc;
    printf("JIT: emitted F_LOCAL native code (%zu bytes)\n", code_size);
    return (void *)code;
}

/* F_ADD_INT_FAST: (sp-1)->u.number += sp->u.number; sp--;
 * Precondition: caller verified both are T_NUMBER.
 * No type check, no refcount, pure integer arithmetic.
 *
 * Code:
 *   ldr x9, [pc, #lit_sp]     // &sp
 *   ldr x10, [x9]             // sp
 *   ldr x11, [x10, #8]        // sp->u.number
 *   ldr x12, [x10, #-8]       // (sp-1)->u.number
 *   add x12, x12, x11         // result
 *   sub x10, x10, #16         // sp--
 *   str x10, [x9]             // store sp
 *   str x12, [x10, #8]        // sp->u.number = result
 *   ret
 *   .quad &sp
 */
void *jit_emit_add_int_fast(void) {
    if (!jit_enabled()) return NULL;
    size_t n_words = 12; /* 9 instr + 1 pad + 1 literal */
    size_t code_size = n_words * sizeof(uint32_t);
    uint32_t *code = (uint32_t *)jit_alloc_code(code_size);
    if (!code) return NULL;

    /* Literal at word 9 (after RET at word 8) */
    /* LDR literal from instr[0]: imm19 = 9 - 0 = 9 */
    code[0] = 0x58000000 | (9 << 5) | 9;    /* LDR X9, [PC, #36] &sp */
    code[1] = encode_ldr_x(10, 9, 0);         /* LDR X10, [X9] sp */
    code[2] = encode_ldr_x(11, 10, 8);        /* LDR X11, [X10, #8] sp->number */
    /* (sp-1)->u.number is at [X10, -16+8] = [X10, -8] */
    /* LDR with negative offset: use LDUR (unscaled) */
    /* LDUR Xt, [Xn, #simm9]: 0xF8500000 | (imm9 << 12) | (Rn << 5) | Rt */
    /* imm9 = -8 → signed 9-bit = 0x1F8 */
    code[3] = 0xF85F814C;                     /* LDUR X12, [X10, #-8] */
    code[4] = 0x8B0B018C;                     /* ADD X12, X12, X11 */
    code[5] = 0xD100414A;                     /* SUB X10, X10, #16 */
    code[6] = encode_str_x(10, 9, 0);         /* STR X10, [X9] store sp */
    code[7] = encode_str_x(12, 10, 8);        /* STR X12, [X10, #8] */
    code[8] = encode_ret();
    code[9] = 0xD503201F;                     /* NOP pad */
    *(uint64_t *)&code[10] = (uint64_t)&sp;   /* Wait - word 10 but n_words=11? */
    /* Actually n_words should be 12 for alignment: 9 instr + 1 nop + 2 words for uint64 */
    /* Fix: already allocated 11 words = 44 bytes, uint64_t at &code[10] needs words 10-11 */
    /* So n_words must be 12. Let me fix. */

    printf("JIT: emitted F_ADD_INT_FAST native code (%zu bytes)\n", code_size);
    return (void *)code;
}


/* Emit F_RETURN_ZERO: identical semantics to F_CONST0 (push integer 0) */
void *jit_emit_return_zero(void);

/* Emit F_LOCAL: load local variable onto stack
 * Reads 1-byte index from pc, advances pc, loads fp[idx] to ++sp */
void *jit_emit_local(void);

/* F_ADD_INT_FAST: sp[-1] += sp; sp--; (both must be T_NUMBER, checked by caller) */
void *jit_emit_add_int_fast(void);

#else /* !__aarch64__ */

void jit_emit_init(void) {
    printf("JIT: no native emitter for this platform\n");
}
void *jit_emit_const0(void) { return NULL; }
void *jit_emit_const1(void) { return NULL; }
void *jit_emit_return_zero(void) { return NULL; }
void *jit_emit_local(void) { return NULL; }
void *jit_emit_add_int_fast(void) { return NULL; }

#endif /* __aarch64__ */
