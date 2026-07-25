
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "jit_trace_compile.h"
#include "jit.h"
#include "interpret.h"
#include <stdio.h>
#include <string.h>

#if defined(__aarch64__)

#define SVALUE_SIZE     16
#define T_NUMBER_VAL    0x2

/* AArch64 trace compiler state */
typedef struct {
    uint32_t *code;
    size_t    cap_words;
    size_t    len;
    /* Virtual stack: maps stack slot offset to register */
    int8_t    slot_to_reg[32];  /* slot index → register number, -1=not allocated */
    int       next_vreg;        /* next available virtual register (x0-x7 = regs 0-7) */
    int       sp_slot;          /* current virtual stack pointer (slot index) */
} trace_compiler_t;

/* Instruction encoding helpers (same as jit_emit_aarch64.c) */
static inline uint32_t tc_ldr_x(uint32_t rt, uint32_t rn, int32_t imm) {
    return 0xF9400000 | ((imm / 8) << 10) | (rn << 5) | rt;
}
static inline uint32_t tc_str_x(uint32_t rt, uint32_t rn, int32_t imm) {
    return 0xF9000000 | ((imm / 8) << 10) | (rn << 5) | rt;
}
static inline uint32_t tc_strh_w(uint32_t rt, uint32_t rn, int32_t imm) {
    return 0x79000000 | ((imm / 2) << 10) | (rn << 5) | rt;
}
static inline uint32_t tc_add_x(uint32_t rd, uint32_t rn, uint32_t rm) {
    return 0x8B000000 | (rm << 16) | (rn << 5) | rd;
}
static inline uint32_t tc_sub_x_imm(uint32_t rd, uint32_t rn, uint32_t imm) {
    return 0xD1000000 | (imm << 10) | (rn << 5) | rd;
}
static inline uint32_t tc_add_x_imm(uint32_t rd, uint32_t rn, uint32_t imm) {
    return 0x91000000 | (imm << 10) | (rn << 5) | rd;
}
static inline uint32_t tc_mov_w_imm(uint32_t rd, uint32_t imm) {
    return 0x52800000 | (imm << 5) | rd;
}
static inline uint32_t tc_cmp_x(uint32_t rn, uint32_t rm) {
    return 0xEB00001F | (rm << 16) | (rn << 5); /* SUBS XZR, Rn, Rm */
}
static inline uint32_t tc_cset_w(uint32_t rd, uint32_t cond) {
    return 0x9A9F07E0 | (cond << 12) | rd;
}
static inline uint32_t tc_cbnz_x(uint32_t rt, int32_t offset_words) {
    return 0xB5000000 | ((offset_words & 0x7FFFF) << 5) | rt;
}
static inline uint32_t tc_ret(void) { return 0xD65F03C0; }
static inline uint32_t tc_nop(void) { return 0xD503201F; }

static void emit(trace_compiler_t *tc, uint32_t insn) {
    if (tc->len < tc->cap_words) tc->code[tc->len++] = insn;
}

/* Allocate a virtual register for a stack slot */
static int alloc_vreg(trace_compiler_t *tc, int slot) {
    if (tc->slot_to_reg[slot] >= 0) return tc->slot_to_reg[slot];
    if (tc->next_vreg > 7) return -1; /* out of vregs */
    int reg = tc->next_vreg++;
    tc->slot_to_reg[slot] = reg;
    return reg;
}

int jit_trace_compile(jit_trace_t *trace) {
    if (!trace || trace->num_entries == 0) return -1;

    /* Allocate code buffer (generous estimate: 20 instructions per opcode + prologue/epilogue) */
    size_t est_words = trace->num_entries * 20 + 40;
    uint32_t *code = (uint32_t *)jit_alloc_code(est_words * sizeof(uint32_t));
    if (!code) return -1;

    trace_compiler_t tc_state;
    memset(&tc_state, 0, sizeof(tc_state));
    tc_state.code = code;
    tc_state.cap_words = est_words;
    tc_state.len = 0;
    tc_state.next_vreg = 0;
    tc_state.sp_slot = 0;
    memset(tc_state.slot_to_reg, -1, sizeof(tc_state.slot_to_reg));

    /* === PROLOGUE === */
    /* Load global sp into x19, fp into x20 */
    /* We need literal pool for &sp and &fp addresses */
    /* For simplicity, use MOVZ+MOVK to construct addresses inline */
    /* Actually, let's use a simpler approach: pass sp/fp as hidden args */
    /* Or: embed literals at end of code and use PC-relative loads */

    /* Simplified prologue: assume caller sets up x19=sp, x20=fp before calling trace */
    /* This means trace entry is NOT a standalone function but a code fragment */
    /* called via: setup regs; bl trace_entry; */
    /* For J3b prototype, we'll make it a standalone function that loads sp/fp itself */

    /* Literal pool will be at end of code. We'll patch offsets after emission. */
    /* Reserve words 0-1 for LDR literal instructions (patched later) */
    emit(&tc_state, 0); /* placeholder: LDR X19, [PC, #lit_sp] */
    emit(&tc_state, 0); /* placeholder: LDR X20, [PC, #lit_fp] */
    /* Load actual sp/fp values */
    emit(&tc_state, tc_ldr_x(19, 19, 0)); /* X19 = *(&sp) = sp */
    emit(&tc_state, tc_ldr_x(20, 20, 0)); /* X20 = *(&fp) = fp */

    /* Initialize virtual stack pointer: scan trace to find initial sp offset */
    /* For now, assume sp starts at slot 0 */
    tc_state.sp_slot = 0;

    /* === TRACE BODY === */
    for (uint32_t i = 0; i < trace->num_entries; i++) {
        trace_entry_t *e = &trace->entries[i];

        switch (e->opcode) {
        case 15: /* F_CONST0: push integer 0 */
        {
            int slot = tc_state.sp_slot++;
            int reg = alloc_vreg(&tc_state, slot);
            if (reg < 0) goto compile_fail;
            /* Store T_NUMBER type */
            emit(&tc_state, tc_mov_w_imm(8, T_NUMBER_VAL));
            emit(&tc_state, tc_strh_w(8, 19, slot * SVALUE_SIZE));
            /* Store value 0 */
            emit(&tc_state, tc_str_x(31, 19, slot * SVALUE_SIZE + 8)); /* STR XZR */
            break;
        }
        case 16: /* F_CONST1: push integer 1 */
        {
            int slot = tc_state.sp_slot++;
            int reg = alloc_vreg(&tc_state, slot);
            if (reg < 0) goto compile_fail;
            emit(&tc_state, tc_mov_w_imm(8, T_NUMBER_VAL));
            emit(&tc_state, tc_strh_w(8, 19, slot * SVALUE_SIZE));
            emit(&tc_state, tc_mov_w_imm(8, 1));
            emit(&tc_state, tc_str_x(8, 19, slot * SVALUE_SIZE + 8));
            break;
        }
        case 61: /* F_LOCAL: load local variable */
        {
            int local_idx = e->imm & 0xFF;
            int slot = tc_state.sp_slot++;
            int reg = alloc_vreg(&tc_state, slot);
            if (reg < 0) goto compile_fail;
            /* Copy 16 bytes from fp[local_idx] to sp[slot] */
            /* For integer-only trace, just copy the number field */
            emit(&tc_state, tc_ldr_x(reg, 20, local_idx * SVALUE_SIZE + 8));
            /* Also store to memory for consistency at trace exit */
            emit(&tc_state, tc_str_x(reg, 19, slot * SVALUE_SIZE + 8));
            /* Copy type too */
            emit(&tc_state, tc_ldr_x(8, 20, local_idx * SVALUE_SIZE));
            emit(&tc_state, tc_str_x(8, 19, slot * SVALUE_SIZE));
            break;
        }
        case 255: /* ADD_INT_FAST: (sp-1) += sp; sp-- */
        {
            if (tc_state.sp_slot < 2) goto compile_fail;
            int rhs_slot = tc_state.sp_slot - 1;
            int lhs_slot = tc_state.sp_slot - 2;
            int rhs_reg = tc_state.slot_to_reg[rhs_slot];
            int lhs_reg = tc_state.slot_to_reg[lhs_slot];
            /* If regs not allocated, load from memory */
            if (rhs_reg < 0) {
                rhs_reg = alloc_vreg(&tc_state, rhs_slot);
                if (rhs_reg < 0) goto compile_fail;
                emit(&tc_state, tc_ldr_x(rhs_reg, 19, rhs_slot * SVALUE_SIZE + 8));
            }
            if (lhs_reg < 0) {
                lhs_reg = alloc_vreg(&tc_state, lhs_slot);
                if (lhs_reg < 0) goto compile_fail;
                emit(&tc_state, tc_ldr_x(lhs_reg, 19, lhs_slot * SVALUE_SIZE + 8));
            }
            /* add in register */
            emit(&tc_state, tc_add_x(lhs_reg, lhs_reg, rhs_reg));
            /* store result back */
            emit(&tc_state, tc_str_x(lhs_reg, 19, lhs_slot * SVALUE_SIZE + 8));
            /* pop rhs */
            tc_state.sp_slot--;
            tc_state.slot_to_reg[rhs_slot] = -1;
            break;
        }
        case 253: /* LT_INT_FAST */
        {
            if (tc_state.sp_slot < 2) goto compile_fail;
            int rhs_slot = tc_state.sp_slot - 1;
            int lhs_slot = tc_state.sp_slot - 2;
            int rhs_reg = tc_state.slot_to_reg[rhs_slot];
            int lhs_reg = tc_state.slot_to_reg[lhs_slot];
            if (rhs_reg < 0) {
                rhs_reg = alloc_vreg(&tc_state, rhs_slot);
                emit(&tc_state, tc_ldr_x(rhs_reg, 19, rhs_slot * SVALUE_SIZE + 8));
            }
            if (lhs_reg < 0) {
                lhs_reg = alloc_vreg(&tc_state, lhs_slot);
                emit(&tc_state, tc_ldr_x(lhs_reg, 19, lhs_slot * SVALUE_SIZE + 8));
            }
            emit(&tc_state, tc_cmp_x(lhs_reg, rhs_reg));
            emit(&tc_state, tc_cset_w(8, 0xB)); /* CSET W8, LT */
            emit(&tc_state, tc_str_x(8, 19, lhs_slot * SVALUE_SIZE + 8));
            /* Update type */
            emit(&tc_state, tc_mov_w_imm(9, T_NUMBER_VAL));
            emit(&tc_state, tc_strh_w(9, 19, lhs_slot * SVALUE_SIZE));
            tc_state.sp_slot--;
            tc_state.slot_to_reg[rhs_slot] = -1;
            break;
        }
        default:
            /* Unsupported opcode in trace - abort compilation */
            printf("JIT: trace compile aborted at opcode %u (unsupported)\n", e->opcode);
            goto compile_fail;
        }
    }

    /* === EPILOGUE === */
    /* Update global sp to reflect final virtual stack position */
    if (tc_state.sp_slot != 0) {
        emit(&tc_state, tc_add_x_imm(19, 19, tc_state.sp_slot * SVALUE_SIZE));
        /* Store updated sp back to global */
        /* Need &sp address - use x21 as temp, loaded from literal */
        /* For now, skip global sp update (J3c will fix this) */
    }
    emit(&tc_state, tc_ret());

    /* === PATCH LITERAL POOL === */
    /* Append literals at end of code */
    size_t lit_base = tc_state.len;
    /* Align to 8 bytes */
    if (lit_base & 1) { emit(&tc_state, tc_nop()); lit_base = tc_state.len; }
    *(uint64_t *)&tc_state.code[lit_base] = (uint64_t)&sp;
    *(uint64_t *)&tc_state.code[lit_base + 2] = (uint64_t)&fp;

    /* Patch LDR literal instructions at prologue */
    /* LDR X19, [PC, #offset]: offset = (lit_base - 0) * 4 bytes, imm19 = lit_base */
    tc_state.code[0] = 0x58000000 | ((uint32_t)lit_base << 5) | 19;
    /* LDR X20, [PC, #offset]: offset = (lit_base+2 - 1) * 4, imm19 = lit_base+1 */
    tc_state.code[1] = 0x58000000 | (((uint32_t)(lit_base + 1)) << 5) | 20;

    trace->native_code = (void *)code;
    trace->code_size = tc_state.len * sizeof(uint32_t);

    printf("JIT: compiled trace #%u → %zu bytes native (%u opcodes)\n",
           trace->id, trace->code_size, trace->num_entries);
    return 0;

compile_fail:
    printf("JIT: trace #%u compilation failed\n", trace->id);
    return -1;
}

#else /* !__aarch64__ */

int jit_trace_compile(jit_trace_t *trace) {
    (void)trace;
    return -1; /* No trace compiler for this platform yet */
}

#endif /* __aarch64__ */
