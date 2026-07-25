
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

/* F_BRANCH: pc += *(int16_t*)pc;
 * Reads 2-byte signed offset from pc, advances pc by that offset.
 * Note: this replaces the interpreter's COPY_SHORT + pc += offset.
 * The native code must read the offset from the CURRENT pc position
 * (which points to the 2-byte offset after the F_BRANCH opcode byte).
 *
 * Code:
 *   ldr x9, [pc, #lit_pc]      // &pc
 *   ldr x10, [x9]              // pc value (points to offset bytes)
 *   ldrsh w11, [x10]           // sign-extend 16-bit offset
 *   add x10, x10, x11          // pc += offset (but need to also skip the 2 offset bytes!)
 *   Wait - interpreter does: COPY_SHORT(&offset, pc); pc += offset;
 *   After COPY_SHORT, pc has already moved past the 2 bytes? No!
 *   Actually in interpreter: pc points to offset bytes when F_BRANCH fires.
 *   COPY_SHORT reads 2 bytes at pc, then pc += offset.
 *   But pc is NOT advanced past the offset bytes first!
 *   So: new_pc = pc + offset (where pc still points to offset bytes)
 *   Hmm, that means offset is relative to the offset field itself.
 *   Let me re-check...
 *
 *   Actually looking at interpreter more carefully:
 *     case F_BRANCH:
 *       COPY_SHORT(&offset, pc);  // reads 2 bytes at current pc
 *       pc += offset;             // adds offset to pc (which still points to offset bytes)
 *   So new_pc = old_pc + offset, where old_pc points to the 2-byte offset.
 *   This means offset=0 would loop forever (stay at offset bytes).
 *   In practice, compiler generates offset relative to AFTER the offset field.
 *   Let me check if there's a pc += 2 somewhere... No, there isn't.
 *   So the LPC compiler must account for this: offset includes the 2-byte skip.
 *
 *   For JIT: we just replicate exactly what interpreter does.
 *   pc currently points to the 2-byte offset (after opcode fetch did pc++).
 *   Read signed short, add to pc. Done.
 */
void *jit_emit_branch(void) {
    if (!jit_enabled()) return NULL;
    extern const char *pc;
    size_t n_words = 10; /* 7 instr + 1 pad + 2 literal words */
    size_t code_size = n_words * sizeof(uint32_t);
    uint32_t *code = (uint32_t *)jit_alloc_code(code_size);
    if (!code) return NULL;

    /* Literal &pc at word 8 */
    code[0] = 0x58000000 | (8 << 5) | 9;    /* LDR X9, [PC, #32] &pc */
    code[1] = encode_ldr_x(10, 9, 0);         /* LDR X10, [X9] pc */
    /* LDRSH W11, [X10]: sign-extend halfword load */
    /* LDRSH Xt, [Xn]: 0x79800000 | (Rn << 5) | Rt (unsigned offset 0) */
    /* Actually LDRSH (signed halfword) unscaled: LDURSH */
    /* LDURSH Xt, [Xn, #0]: 0x79C00000 | (Rn << 5) | Rt */
    /* Or scaled: LDRSH Wt, [Xn, #0]: 0x79800000 | (0 << 10) | (Rn << 5) | Rt */
    code[2] = 0x7980014B;                     /* LDRSH W11, [X10, #0] */
    /* Sign-extend W11 to X11 for addition */
    code[3] = 0x93407D6B;                     /* SXTW X11, W11 */
    code[4] = 0x8B0B014A;                     /* ADD X10, X10, X11 */
    code[5] = encode_str_x(10, 9, 0);         /* STR X10, [X9] store pc */
    code[6] = encode_ret();
    code[7] = 0xD503201F;                     /* NOP pad */
    *(uint64_t *)&code[8] = (uint64_t)&pc;

    printf("JIT: emitted F_BRANCH native code (%zu bytes)\n", code_size);
    return (void *)code;
}

/* F_BRANCH_WHEN_ZERO fast path:
 * Precondition: sp->type == T_NUMBER (checked by caller).
 * If sp->u.number == 0: sp--, pc += *(int16_t*)pc
 * Else: sp--, pc += 2 (skip offset bytes)
 *
 * Code:
 *   ldr x9, [pc, #lit_sp]
 *   ldr x10, [pc, #lit_pc]
 *   ldr x11, [x9]              // sp
 *   ldr x12, [x11, #8]         // sp->u.number
 *   cbnz x12, nonzero          // if number != 0, goto nonzero
 *   // Zero path: pc += *(short*)pc
 *   ldr x13, [x10]             // pc
 *   ldrsh w14, [x13]
 *   sxtw x14, w14
 *   add x13, x13, x14
 *   str x13, [x10]             // store pc
 *   b done
 * nonzero:
 *   // Non-zero path: pc += 2
 *   ldr x13, [x10]
 *   add x13, x13, #2
 *   str x13, [x10]
 * done:
 *   sub x11, x11, #16          // sp--
 *   str x11, [x9]              // store sp
 *   ret
 *   .quad &sp
 *   .quad &pc
 */
void *jit_emit_branch_when_zero_fast(void) {
    if (!jit_enabled()) return NULL;
    extern const char *pc;
    size_t n_words = 22; /* instructions + 2 literals */
    size_t code_size = n_words * sizeof(uint32_t);
    uint32_t *code = (uint32_t *)jit_alloc_code(code_size);
    if (!code) return NULL;

    /* Literals at words 18 (&sp) and 20 (&pc) */
    /* From instr[0]: lit_sp at word 18, offset = 18*4 = 72, imm19 = 18 */
    /* From instr[1]: lit_pc at word 20, offset = 19*4 = 76, imm19 = 19 */
    code[0]  = 0x58000000 | (18 << 5) | 9;   /* LDR X9, [PC,#72] &sp */
    code[1]  = 0x58000000 | (19 << 5) | 10;  /* LDR X10,[PC,#76] &pc */
    code[2]  = encode_ldr_x(11, 9, 0);        /* LDR X11, [X9] sp */
    code[3]  = encode_ldr_x(12, 11, 8);       /* LDR X12, [X11,#8] number */
    /* CBNZ X12, #offset_to_nonzero */
    /* nonzero starts at instruction 9, current is 4, offset = (9-4)*4 = 20 bytes */
    /* CBNZ: 0xB5000000 | (imm19 << 5) | Rt, imm19 = offset/4 = 5 */
    code[4]  = 0xB5000000 | (5 << 5) | 12;   /* CBNZ X12, #+20 (nonzero) */
    /* === Zero path === */
    code[5]  = encode_ldr_x(13, 10, 0);       /* LDR X13, [X10] pc */
    code[6]  = 0x798001AE;                     /* LDRSH W14, [X13] */
    code[7]  = 0x93407DCE;                     /* SXTW X14, W14 */
    code[8]  = 0x8B0E01AD;                     /* ADD X13, X13, X14 */
    /* Branch to done (instruction 15): offset = (15-9)*4 = 24, imm19 = 6 */
    code[9]  = 0x14000000 | 6;                 /* B #+24 (done) */
    /* === Nonzero path (instruction 10) === */
    code[10] = encode_ldr_x(13, 10, 0);       /* LDR X13, [X10] pc */
    code[11] = encode_add_x_imm(13, 13, 2);   /* ADD X13, X13, #2 */
    /* Fall through to done */
    /* === Done (instruction 12) === */
    code[12] = encode_str_x(13, 10, 0);       /* STR X13, [X10] store pc */
    code[13] = 0xD100416B;                     /* SUB X11, X11, #16 (sp--) */
    code[14] = encode_str_x(11, 9, 0);        /* STR X11, [X9] store sp */
    code[15] = encode_ret();
    /* Padding */
    code[16] = 0xD503201F;                     /* NOP */
    code[17] = 0xD503201F;                     /* NOP */
    /* Literals */
    *(uint64_t *)&code[18] = (uint64_t)&sp;
    *(uint64_t *)&code[20] = (uint64_t)&pc;

    printf("JIT: emitted F_BRANCH_WHEN_ZERO fast path (%zu bytes)\n", code_size);
    return (void *)code;
}

/* Comparison helper: both do sp--, load two numbers, compare, store result.
 * Only difference is the comparison instruction.
 * Pattern:
 *   ldr x9, [pc, #lit_sp]      // &sp
 *   ldr x10, [x9]              // sp
 *   ldr x11, [x10, #8]         // sp->u.number (rhs)
 *   ldur x12, [x10, #-8]       // (sp-1)->u.number (lhs)
 *   cmp x12, x11               // lhs vs rhs
 *   cset w13, COND             // 1 if condition true, 0 otherwise
 *   sub x10, x10, #16          // sp--
 *   str x10, [x9]              // store sp
 *   mov w14, #T_NUMBER
 *   strh w14, [x10, #0]        // sp->type = T_NUMBER
 *   str x13, [x10, #8]         // sp->u.number = result (0 or 1)
 *   ret
 *   .quad &sp
 */
static void *emit_compare_fast(int cond_opcode) {
    if (!jit_enabled()) return NULL;
    size_t n_words = 14; /* 11 instr + 1 pad + 2 literal words */
    size_t code_size = n_words * sizeof(uint32_t);
    uint32_t *code = (uint32_t *)jit_alloc_code(code_size);
    if (!code) return NULL;

    /* Literal &sp at word 12 */
    code[0]  = 0x58000000 | (12 << 5) | 9;   /* LDR X9, [PC,#48] &sp */
    code[1]  = encode_ldr_x(10, 9, 0);         /* LDR X10, [X9] sp */
    code[2]  = encode_ldr_x(11, 10, 8);        /* LDR X11, [X10,#8] rhs */
    code[3]  = 0xF85F814C;                     /* LDUR X12, [X10,#-8] lhs */
    code[4]  = 0xEB0B019F;                     /* CMP X12, X11 (SUBS XZR,X12,X11) */
    /* CSET W13, cond: 0x9A9F07ED | (cond << 12) */
    code[5]  = 0x9A9F07ED | (cond_opcode << 12); /* CSET W13, COND */
    code[6]  = 0xD100414A;                     /* SUB X10, X10, #16 (sp--) */
    code[7]  = encode_str_x(10, 9, 0);         /* STR X10, [X9] store sp */
    code[8]  = encode_mov_w_imm(14, T_NUMBER_VAL); /* MOVZ W14, #2 */
    code[9]  = encode_strh_w(14, 10, SVALUE_TYPE_OFF); /* STRH W14, [X10,#0] */
    code[10] = encode_str_x(13, 10, SVALUE_NUM_OFF);   /* STR X13, [X10,#8] */
    code[11] = encode_ret();
    *(uint64_t *)&code[12] = (uint64_t)&sp;

    return (void *)code;
}

/* LT condition code = 0xB (signed less than) */
void *jit_emit_lt_int_fast(void) {
    void *code = emit_compare_fast(0xB);
    if (code) printf("JIT: emitted F_LT_INT_FAST native code\n");
    return code;
}

/* EQ condition code = 0x0 (equal) */
void *jit_emit_eq_int_fast(void) {
    void *code = emit_compare_fast(0x0);
    if (code) printf("JIT: emitted F_EQ_INT_FAST native code\n");
    return code;
}


/* Emit F_RETURN_ZERO: identical semantics to F_CONST0 (push integer 0) */
void *jit_emit_return_zero(void);

/* Emit F_LOCAL: load local variable onto stack
 * Reads 1-byte index from pc, advances pc, loads fp[idx] to ++sp */
void *jit_emit_local(void);

/* F_ADD_INT_FAST: sp[-1] += sp; sp--; (both must be T_NUMBER, checked by caller) */
void *jit_emit_add_int_fast(void);

/* F_BRANCH: pc += *(short*)pc; (unconditional relative jump) */
void *jit_emit_branch(void);

/* F_BRANCH_WHEN_ZERO: if T_NUMBER && val==0 → pc+=offset, else pc+=2
 * Precondition: caller verified sp->type == T_NUMBER. Pops sp. */
void *jit_emit_branch_when_zero_fast(void);

/* F_LT_INT_FAST: sp--; sp->number = sp->number < (sp+1)->number */
void *jit_emit_lt_int_fast(void);

/* F_EQ_INT_FAST: sp--; sp->number = sp->number == (sp+1)->number */
void *jit_emit_eq_int_fast(void);

#else /* !__aarch64__ */

void jit_emit_init(void) {
    printf("JIT: no native emitter for this platform\n");
}
void *jit_emit_const0(void) { return NULL; }
void *jit_emit_const1(void) { return NULL; }
void *jit_emit_return_zero(void) { return NULL; }
void *jit_emit_local(void) { return NULL; }
void *jit_emit_add_int_fast(void) { return NULL; }
void *jit_emit_branch(void) { return NULL; }
void *jit_emit_branch_when_zero_fast(void) { return NULL; }
void *jit_emit_lt_int_fast(void) { return NULL; }
void *jit_emit_eq_int_fast(void) { return NULL; }

#endif /* __aarch64__ */
