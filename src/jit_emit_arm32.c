
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "jit_emit.h"
#include "interpret.h"
#include <stdio.h>
#include <string.h>

#if defined(__arm__) && !defined(__aarch64__)

#define SVALUE_SIZE     16
#define SVALUE_TYPE_OFF 0
#define SVALUE_NUM_OFF  8
#define T_NUMBER_VAL    0x2

/* ARM32 (ARM mode, not Thumb) instruction encoding
 * All instructions are 32-bit fixed width.
 * Condition field AL (0xE) for unconditional execution.
 */
static inline uint32_t arm_ldr_imm(uint32_t rt, uint32_t rn, int32_t imm) {
    /* LDR Rt, [Rn, #imm] (unsigned offset, U=1) */
    uint32_t u = (imm >= 0) ? (1 << 23) : 0;
    uint32_t abs_imm = (imm >= 0) ? imm : -imm;
    return 0xE5900000 | u | (rn << 16) | (rt << 12) | (abs_imm & 0xFFF);
}
static inline uint32_t arm_str_imm(uint32_t rt, uint32_t rn, int32_t imm) {
    uint32_t u = (imm >= 0) ? (1 << 23) : 0;
    uint32_t abs_imm = (imm >= 0) ? imm : -imm;
    return 0xE5800000 | u | (rn << 16) | (rt << 12) | (abs_imm & 0xFFF);
}
static inline uint32_t arm_strh_imm(uint32_t rt, uint32_t rn, int32_t imm) {
    /* STRH Rt, [Rn, #imm] */
    uint32_t imm_lo = imm & 0xF;
    uint32_t imm_hi = (imm >> 4) & 0xF;
    return 0xE1C000B0 | (rn << 16) | (rt << 12) | (imm_hi << 8) | imm_lo;
}
static inline uint32_t arm_mov_imm(uint32_t rd, uint32_t imm) {
    /* MOV Rd, #imm (8-bit rotated, simplified for small values) */
    return 0xE3A00000 | (rd << 12) | (imm & 0xFF);
}
static inline uint32_t arm_add_imm(uint32_t rd, uint32_t rn, uint32_t imm) {
    return 0xE2800000 | (rn << 16) | (rd << 12) | (imm & 0xFF);
}
static inline uint32_t arm_bx_lr(void) {
    return 0xE12FFF1E; /* BX LR */
}
static inline uint32_t arm_nop(void) {
    return 0xE1A00000; /* MOV R0, R0 */
}

void jit_emit_init(void) {
    printf("JIT: ARM32 code emitter initialized\n");
}

/* F_CONST0: uses literal pool after BX LR */
void *jit_emit_const0(void) {
    if (!jit_enabled()) return NULL;
    size_t n_words = 12;
    size_t code_size = n_words * sizeof(uint32_t);
    uint32_t *code = (uint32_t *)jit_alloc_code(code_size);
    if (!code) return NULL;

    /* Literal at word 10 (after BX LR at 8, NOP at 9) */
    /* LDR Rn, [PC, #offset] : offset = (literal_addr - PC - 8) */
    /* PC during LDR at word 0 = &code[0] + 8 (ARM pipeline) */
    /* literal at &code[10], offset = (10-0)*4 - 8 = 32 */
    code[0] = arm_ldr_imm(9, 15, 32);       /* LDR R9, [PC, #32] → &sp */
    code[1] = arm_ldr_imm(10, 9, 0);        /* LDR R10, [R9] → sp */
    code[2] = arm_add_imm(10, 10, SVALUE_SIZE); /* ADD R10, R10, #16 */
    code[3] = arm_str_imm(10, 9, 0);        /* STR R10, [R9] → store sp */
    code[4] = arm_mov_imm(11, T_NUMBER_VAL);/* MOV R11, #2 */
    code[5] = arm_strh_imm(11, 10, 0);      /* STRH R11, [R10, #0] → type */
    /* Store 0 to sp->u.number (two 32-bit stores for 64-bit zero) */
    code[6] = arm_mov_imm(11, 0);           /* MOV R11, #0 */
    code[7] = arm_str_imm(11, 10, SVALUE_NUM_OFF);     /* STR R11, [R10, #8] */
    code[8] = arm_str_imm(11, 10, SVALUE_NUM_OFF + 4); /* STR R11, [R10, #12] */
    code[9] = arm_bx_lr();
    code[10] = (uint32_t)(uintptr_t)&sp;    /* literal: &sp (32-bit addr) */
    code[11] = arm_nop();                   /* pad */

    printf("JIT: emitted F_CONST0 ARM32 native code (%zu bytes)\n", code_size);
    return (void *)code;
}

void *jit_emit_const1(void) {
    if (!jit_enabled()) return NULL;
    size_t n_words = 12;
    size_t code_size = n_words * sizeof(uint32_t);
    uint32_t *code = (uint32_t *)jit_alloc_code(code_size);
    if (!code) return NULL;

    code[0] = arm_ldr_imm(9, 15, 32);
    code[1] = arm_ldr_imm(10, 9, 0);
    code[2] = arm_add_imm(10, 10, SVALUE_SIZE);
    code[3] = arm_str_imm(10, 9, 0);
    code[4] = arm_mov_imm(11, T_NUMBER_VAL);
    code[5] = arm_strh_imm(11, 10, 0);
    code[6] = arm_mov_imm(11, 1);
    code[7] = arm_str_imm(11, 10, SVALUE_NUM_OFF);
    code[8] = arm_mov_imm(11, 0);
    code[9] = arm_str_imm(11, 10, SVALUE_NUM_OFF + 4);
    code[10] = arm_bx_lr();
    code[11] = (uint32_t)(uintptr_t)&sp;

    printf("JIT: emitted F_CONST1 ARM32 native code (%zu bytes)\n", code_size);
    return (void *)code;
}

void *jit_emit_return_zero(void) { return jit_emit_const0(); }

void *jit_emit_local(void) {
    /* ARM32 F_LOCAL: deferred to J3 (complexity with 32-bit addressing) */
    printf("JIT: ARM32 F_LOCAL deferred (use interpreter fallback)\n");
    return NULL;
}

void *jit_emit_add_int_fast(void) {
    if (!jit_enabled()) return NULL;
    size_t n_words = 14;
    size_t code_size = n_words * sizeof(uint32_t);
    uint32_t *code = (uint32_t *)jit_alloc_code(code_size);
    if (!code) return NULL;

    /* Literal at word 12 */
    code[0] = arm_ldr_imm(9, 15, 40);       /* LDR R9, [PC, #40] → &sp */
    code[1] = arm_ldr_imm(10, 9, 0);        /* LDR R10, [R9] → sp */
    /* Load sp->u.number (64-bit = two 32-bit loads) */
    code[2] = arm_ldr_imm(11, 10, SVALUE_NUM_OFF);     /* lo */
    code[3] = arm_ldr_imm(12, 10, SVALUE_NUM_OFF + 4); /* hi */
    /* Load (sp-1)->u.number */
    code[4] = arm_ldr_imm(14, 10, -8);      /* lo (sp-8) */
    /* Note: ARM LDR negative offset needs U=0, handled by arm_ldr_imm */
    code[5] = arm_ldr_imm(8, 10, -4);       /* hi (sp-4) - WRONG, should be sp-12+4=sp-8+4 */
    /* Actually (sp-1) is at sp-16, so (sp-1)->u.number is at sp-16+8 = sp-8 */
    /* 64-bit value at sp-8: lo at sp-8, hi at sp-4 */
    /* Fix: code[4] loads sp-8 (lo), code[5] should load sp-4 (hi) */
    code[5] = arm_ldr_imm(8, 10, -4);       /* hi at sp-4 */
    /* ADD: result_lo = R14 + R11, result_hi = R8 + R12 + carry */
    code[6] = 0xE09BEC0E;                    /* ADDS LR, R11, R14 (lo, set carry) */
    code[7] = 0xE0A8C00C;                    /* ADC R12, R12, R8 (hi + carry) */
    /* sp-- */
    code[8] = 0xE24AA010;                    /* SUB R10, R10, #16 */
    code[9] = arm_str_imm(10, 9, 0);        /* store sp */
    /* Store result to sp->u.number */
    code[10] = arm_str_imm(14, 10, SVALUE_NUM_OFF);     /* lo */
    code[11] = arm_str_imm(12, 10, SVALUE_NUM_OFF + 4); /* hi */
    code[12] = arm_bx_lr();
    code[13] = (uint32_t)(uintptr_t)&sp;

    printf("JIT: emitted F_ADD_INT_FAST ARM32 native code (%zu bytes)\n", code_size);
    return (void *)code;
}

#else /* !__arm__ */
void jit_emit_init(void) { printf("JIT: no ARM32 emitter on this platform\n"); }
void *jit_emit_const0(void) { return NULL; }
void *jit_emit_const1(void) { return NULL; }
void *jit_emit_return_zero(void) { return NULL; }
void *jit_emit_local(void) { return NULL; }
void *jit_emit_add_int_fast(void) { return NULL; }
#endif
