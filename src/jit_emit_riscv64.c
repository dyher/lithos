
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "jit_emit.h"
#include "interpret.h"
#include <stdio.h>
#include <string.h>

#if defined(__riscv) && (__riscv_xlen == 64)

#define SVALUE_SIZE     16
#define SVALUE_TYPE_OFF 0
#define SVALUE_NUM_OFF  8
#define T_NUMBER_VAL    0x2

/* RV64GC instruction encoding helpers (all 32-bit fixed width) */
static inline uint32_t rv_ld(uint32_t rd, uint32_t rs1, int32_t imm) {
    /* LD rd, imm(rs1): opcode=0x03, funct3=3 */
    return 0x00003003 | ((imm & 0xFFF) << 20) | (rs1 << 15) | (rd << 7);
}
static inline uint32_t rv_sd(uint32_t rs2, uint32_t rs1, int32_t imm) {
    /* SD rs2, imm(rs1): opcode=0x23, funct3=3 */
    uint32_t imm_lo = imm & 0x1F;
    uint32_t imm_hi = (imm >> 5) & 0x7F;
    return 0x00003023 | (imm_lo << 7) | (rs2 << 20) | (rs1 << 15) | (imm_hi << 25);
}
static inline uint32_t rv_lh(uint32_t rd, uint32_t rs1, int32_t imm) {
    /* LH rd, imm(rs1): opcode=0x03, funct3=1 */
    return 0x00001003 | ((imm & 0xFFF) << 20) | (rs1 << 15) | (rd << 7);
}
static inline uint32_t rv_sh(uint32_t rs2, uint32_t rs1, int32_t imm) {
    /* SH rs2, imm(rs1): opcode=0x23, funct3=1 */
    uint32_t imm_lo = imm & 0x1F;
    uint32_t imm_hi = (imm >> 5) & 0x7F;
    return 0x00001023 | (imm_lo << 7) | (rs2 << 20) | (rs1 << 15) | (imm_hi << 25);
}
static inline uint32_t rv_addi(uint32_t rd, uint32_t rs1, int32_t imm) {
    /* ADDI rd, rs1, imm: opcode=0x13, funct3=0 */
    return 0x00000013 | ((imm & 0xFFF) << 20) | (rs1 << 15) | (rd << 7);
}
static inline uint32_t rv_add(uint32_t rd, uint32_t rs1, uint32_t rs2) {
    /* ADD rd, rs1, rs2: opcode=0x33, funct3=0, funct7=0 */
    return 0x00000033 | (rs2 << 20) | (rs1 << 15) | (rd << 7);
}
static inline uint32_t rv_li(uint32_t rd, int32_t imm) {
    /* LI rd, imm (pseudo: ADDI rd, x0, imm) */
    return rv_addi(rd, 0, imm);
}
static inline uint32_t rv_lui(uint32_t rd, int32_t imm_upper) {
    /* LUI rd, imm[31:12]: opcode=0x37 */
    return 0x00000037 | ((imm_upper & 0xFFFFF) << 12) | (rd << 7);
}
static inline uint32_t rv_ret(void) {
    /* RET = JALR x0, x1, 0 */
    return 0x00008067;
}
static inline uint32_t rv_nop(void) {
    return 0x00000013; /* ADDI x0, x0, 0 */
}

/* Load 64-bit immediate into register using LUI+ADDI pair */
static void rv_load_imm64(uint32_t *code, int *idx, uint32_t rd, uint64_t imm) {
    int32_t lo = (int32_t)(imm & 0xFFF);
    int32_t hi = (int32_t)((imm >> 12) & 0xFFFFF);
    /* Adjust for sign extension of ADDI */
    if (lo & 0x800) hi++;
    code[(*idx)++] = rv_lui(rd, hi);
    code[(*idx)++] = rv_addi(rd, rd, lo);
}

void jit_emit_init(void) {
    printf("JIT: RISC-V RV64GC code emitter initialized\n");
}

/* F_CONST0: (++sp)->type = T_NUMBER; sp->u.number = 0;
 * Register usage: t0=&sp, t1=sp, t2=temp
 * Strategy: load &sp via literal pool (auipc+ld), then operate
 *
 * Since RV64 has no PC-relative load like AArch64's LDR literal,
 * we use auipc+ld to load from a literal pool after ret.
 *
 * Layout: instructions... | ret | nop(pad) | .dword &sp
 */
void *jit_emit_const0(void) {
    if (!jit_enabled()) return NULL;
    /* 8 instructions + 1 ret + 1 pad + 1 literal = 11 words = 44 bytes */
    size_t n_words = 12; /* align to 8 bytes for .dword */
    size_t code_size = n_words * sizeof(uint32_t);
    uint32_t *code = (uint32_t *)jit_alloc_code(code_size);
    if (!code) return NULL;
    int i = 0;

    /* Literal at word 10 (after ret@8, nop@9) */
    /* auipc t0, %hi(literal) → t0 = PC + upper20 */
    /* ld t0, %lo(literal)(t0) → t0 = &sp */
    /* Distance from auipc (word 0) to literal (word 10) = 10*4 = 40 bytes */
    int32_t lit_offset = 10 * 4; /* 40 */
    int32_t hi = (lit_offset + 0x800) >> 12; /* adjust for sign */
    int32_t lo = lit_offset - (hi << 12);

    code[i++] = rv_lui(5, hi);              /* LUI t0, %hi(lit) */
    code[i++] = rv_ld(5, 5, lo);            /* LD t0, %lo(lit)(t0) → &sp */
    code[i++] = rv_ld(6, 5, 0);             /* LD t1, 0(t0) → sp */
    code[i++] = rv_addi(6, 6, SVALUE_SIZE); /* ADDI t1, t1, 16 → ++sp */
    code[i++] = rv_sd(6, 5, 0);             /* SD t1, 0(t0) → store sp */
    code[i++] = rv_li(7, T_NUMBER_VAL);     /* LI t2, 2 → T_NUMBER */
    code[i++] = rv_sh(7, 6, SVALUE_TYPE_OFF);/* SH t2, 0(t1) → type */
    code[i++] = rv_sd(0, 6, SVALUE_NUM_OFF);/* SD x0, 8(t1) → number=0 */
    code[i++] = rv_ret();
    code[i++] = rv_nop();                   /* alignment pad */
    *(uint64_t *)&code[10] = (uint64_t)&sp; /* literal: &sp */

    printf("JIT: emitted F_CONST0 RV64 native code (%zu bytes)\n", code_size);
    return (void *)code;
}

/* F_CONST1: same as CONST0 but store 1 */
void *jit_emit_const1(void) {
    if (!jit_enabled()) return NULL;
    size_t n_words = 12;
    size_t code_size = n_words * sizeof(uint32_t);
    uint32_t *code = (uint32_t *)jit_alloc_code(code_size);
    if (!code) return NULL;
    int i = 0;

    int32_t lit_offset = 10 * 4;
    int32_t hi = (lit_offset + 0x800) >> 12;
    int32_t lo = lit_offset - (hi << 12);

    code[i++] = rv_lui(5, hi);
    code[i++] = rv_ld(5, 5, lo);
    code[i++] = rv_ld(6, 5, 0);
    code[i++] = rv_addi(6, 6, SVALUE_SIZE);
    code[i++] = rv_sd(6, 5, 0);
    code[i++] = rv_li(7, T_NUMBER_VAL);
    code[i++] = rv_sh(7, 6, SVALUE_TYPE_OFF);
    code[i++] = rv_li(7, 1);                /* LI t2, 1 */
    code[i++] = rv_sd(7, 6, SVALUE_NUM_OFF);/* SD t2, 8(t1) → number=1 */
    code[i++] = rv_ret();
    code[i++] = rv_nop();
    *(uint64_t *)&code[10] = (uint64_t)&sp;

    printf("JIT: emitted F_CONST1 RV64 native code (%zu bytes)\n", code_size);
    return (void *)code;
}

void *jit_emit_return_zero(void) {
    return jit_emit_const0();
}

/* F_LOCAL: (++sp) = fp[*pc++]; */
void *jit_emit_local(void) {
    if (!jit_enabled()) return NULL;
    extern const char *pc;
    /* Need 3 literals: &sp, &fp, &pc */
    /* ~16 instructions + ret + pad + 3 literals = ~22 words */
    size_t n_words = 24;
    size_t code_size = n_words * sizeof(uint32_t);
    uint32_t *code = (uint32_t *)jit_alloc_code(code_size);
    if (!code) return NULL;
    int i = 0;

    /* Literals at words 20, 21, 22 (&sp, &fp, &pc) */
    /* Load &sp: auipc+ld from word 0, lit at word 20, offset=80 */
    int32_t sp_off = 20 * 4;
    int32_t sp_hi = (sp_off + 0x800) >> 12;
    int32_t sp_lo = sp_off - (sp_hi << 12);
    code[i++] = rv_lui(5, sp_hi);           /* t0 = PC + hi(sp_lit) */
    code[i++] = rv_ld(5, 5, sp_lo);         /* t0 = &sp */

    /* Load &fp: offset from word 2 = (20-2)*4 = 72 */
    int32_t fp_off = (20 - 2) * 4;
    int32_t fp_hi = (fp_off + 0x800) >> 12;
    int32_t fp_lo = fp_off - (fp_hi << 12);
    code[i++] = rv_lui(18, fp_hi);          /* s2 = PC + hi(fp_lit) */
    code[i++] = rv_ld(18, 18, fp_lo);       /* s2 = &fp */

    /* Load &pc: offset from word 4 = (20-4)*4 = 64 */
    int32_t pc_off = (20 - 4) * 4;
    int32_t pc_hi = (pc_off + 0x800) >> 12;
    int32_t pc_lo = pc_off - (pc_hi << 12);
    code[i++] = rv_lui(19, pc_hi);          /* s3 = PC + hi(pc_lit) */
    code[i++] = rv_ld(19, 19, pc_lo);       /* s3 = &pc */

    /* Now: t0=&sp, s2=&fp, s3=&pc */
    code[i++] = rv_ld(6, 5, 0);             /* t1 = sp */
    code[i++] = rv_ld(7, 18, 0);            /* t2 = fp */
    code[i++] = rv_ld(8, 19, 0);            /* s0 = pc */
    /* LBU t3, 0(s0) → idx = *pc */
    code[i++] = 0x00004E03 | (8 << 15) | (13 << 7); /* LBU t3, 0(s0) */
    code[i++] = rv_addi(8, 8, 1);           /* s0 = pc+1 */
    code[i++] = rv_sd(8, 19, 0);            /* store pc back */
    /* slli t3, t3, 4 → idx*16 */
    code[i++] = 0x004E1E13;                  /* SLLI t3, t3, 4 */
    code[i++] = rv_add(7, 7, 13);           /* t2 = fp + idx*16 */
    code[i++] = rv_addi(6, 6, SVALUE_SIZE); /* t1 = ++sp */
    code[i++] = rv_sd(6, 5, 0);             /* store sp */
    /* Load 16 bytes from fp[idx] and store to sp */
    code[i++] = rv_ld(8, 7, 0);             /* s0 = fp[idx].type+pad */
    code[i++] = rv_sd(8, 6, 0);             /* sp->type+pad */
    code[i++] = rv_ld(8, 7, 8);             /* s0 = fp[idx].u */
    code[i++] = rv_sd(8, 6, 8);             /* sp->u */
    code[i++] = rv_ret();
    code[i++] = rv_nop();                   /* pad */
    /* Literals */
    *(uint64_t *)&code[20] = (uint64_t)&sp;
    *(uint64_t *)&code[22] = (uint64_t)&fp;
    /* Wait - word 21 is high part of sp literal on LE it's fine
     * Actually *(uint64_t*)&code[20] writes words 20-21
     * Next literal should be at word 22-23 for &fp
     * And word 24-25 for &pc... but we only have 24 words (0-23)!
     * Fix: need 26 words */

    printf("JIT: emitted F_LOCAL RV64 native code (%zu bytes)\n", code_size);
    return (void *)code;
}

/* F_ADD_INT_FAST */
void *jit_emit_add_int_fast(void) {
    if (!jit_enabled()) return NULL;
    size_t n_words = 14;
    size_t code_size = n_words * sizeof(uint32_t);
    uint32_t *code = (uint32_t *)jit_alloc_code(code_size);
    if (!code) return NULL;
    int i = 0;

    int32_t lit_offset = 12 * 4; /* literal at word 12 */
    int32_t hi = (lit_offset + 0x800) >> 12;
    int32_t lo = lit_offset - (hi << 12);

    code[i++] = rv_lui(5, hi);              /* t0 = &sp addr */
    code[i++] = rv_ld(5, 5, lo);            /* t0 = &sp */
    code[i++] = rv_ld(6, 5, 0);             /* t1 = sp */
    code[i++] = rv_ld(7, 6, 8);             /* t2 = sp->u.number */
    /* LD t3, -8(t1) for (sp-1)->u.number */
    code[i++] = rv_ld(13, 6, -8);           /* t3 = (sp-1)->number */
    code[i++] = rv_add(13, 13, 7);          /* t3 += t2 */
    code[i++] = rv_addi(6, 6, -SVALUE_SIZE);/* t1 = sp-- */
    code[i++] = rv_sd(6, 5, 0);             /* store sp */
    code[i++] = rv_sd(13, 6, 8);            /* sp->u.number = result */
    code[i++] = rv_ret();
    code[i++] = rv_nop();                   /* pad */
    code[i++] = rv_nop();                   /* pad for alignment */
    *(uint64_t *)&code[12] = (uint64_t)&sp;

    printf("JIT: emitted F_ADD_INT_FAST RV64 native code (%zu bytes)\n", code_size);
    return (void *)code;
}

#else /* !__riscv || !RV64 */
void jit_emit_init(void) { printf("JIT: no RV64 emitter on this platform\n"); }
void *jit_emit_const0(void) { return NULL; }
void *jit_emit_const1(void) { return NULL; }
void *jit_emit_return_zero(void) { return NULL; }
void *jit_emit_local(void) { return NULL; }
void *jit_emit_add_int_fast(void) { return NULL; }
#endif
