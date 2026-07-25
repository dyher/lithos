
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "jit_trace_compile.h"
#include "jit.h"
#include "interpret.h"
#include <stdio.h>
#include <string.h>

#if defined(__x86_64__) || defined(_M_X64)

#define SVALUE_SIZE     16
#define T_NUMBER_VAL    0x2

typedef struct {
    uint8_t *buf;
    size_t   len;
    size_t   cap;
    int8_t   slot_to_reg[32];
    int      next_vreg;
    int      sp_slot;
} x86_tc_t;

static inline void tc_emit(x86_tc_t *tc, uint8_t b) {
    if (tc->len < tc->cap) tc->buf[tc->len++] = b;
}
static inline void tc_emit_le32(x86_tc_t *tc, uint32_t v) {
    tc_emit(tc, v & 0xFF); tc_emit(tc, (v>>8) & 0xFF);
    tc_emit(tc, (v>>16) & 0xFF); tc_emit(tc, (v>>24) & 0xFF);
}
static inline void tc_emit_le64(x86_tc_t *tc, uint64_t v) {
    tc_emit_le32(tc, (uint32_t)v); tc_emit_le32(tc, (uint32_t)(v>>32));
}

/* x86_64 encoding helpers */
static void emit_movabs_r64(x86_tc_t *tc, uint8_t reg, uint64_t imm) {
    tc_emit(tc, 0x48); tc_emit(tc, 0xB8 + reg); tc_emit_le64(tc, imm);
}
static void emit_mov_r64_mem(x86_tc_t *tc, uint8_t dst, uint8_t base, int32_t off) {
    tc_emit(tc, 0x48); tc_emit(tc, 0x8B);
    uint8_t mod = (off == 0) ? 0x00 : (off >= -128 && off <= 127) ? 0x40 : 0x80;
    tc_emit(tc, mod | (dst << 3) | base);
    if (mod == 0x40) tc_emit(tc, (uint8_t)off);
    else if (mod == 0x80) tc_emit_le32(tc, (uint32_t)off);
}
static void emit_mov_mem_r64(x86_tc_t *tc, uint8_t base, int32_t off, uint8_t src) {
    tc_emit(tc, 0x48); tc_emit(tc, 0x89);
    uint8_t mod = (off == 0) ? 0x00 : (off >= -128 && off <= 127) ? 0x40 : 0x80;
    tc_emit(tc, mod | (src << 3) | base);
    if (mod == 0x40) tc_emit(tc, (uint8_t)off);
    else if (mod == 0x80) tc_emit_le32(tc, (uint32_t)off);
}
static void emit_mov_w_imm_mem(x86_tc_t *tc, uint8_t base, int32_t off, uint16_t val) {
    tc_emit(tc, 0x66); tc_emit(tc, 0xC7);
    uint8_t mod = (off == 0) ? 0x00 : (off >= -128 && off <= 127) ? 0x40 : 0x80;
    tc_emit(tc, mod | (0 << 3) | base);
    if (mod == 0x40) tc_emit(tc, (uint8_t)off);
    else if (mod == 0x80) tc_emit_le32(tc, (uint32_t)off);
    tc_emit(tc, val & 0xFF); tc_emit(tc, (val >> 8) & 0xFF);
}
static void emit_add_r64(x86_tc_t *tc, uint8_t dst, uint8_t src) {
    tc_emit(tc, 0x48); tc_emit(tc, 0x01); tc_emit(tc, 0xC0 | (src << 3) | dst);
}
static void emit_cmp_r64(x86_tc_t *tc, uint8_t r1, uint8_t r2) {
    tc_emit(tc, 0x48); tc_emit(tc, 0x39); tc_emit(tc, 0xC0 | (r2 << 3) | r1);
}
static void emit_setcc(x86_tc_t *tc, uint8_t cond, uint8_t dst) {
    tc_emit(tc, 0x0F); tc_emit(tc, 0x90 | cond); tc_emit(tc, 0xC0 | dst);
}
static void emit_movzx_r64_r8(x86_tc_t *tc, uint8_t dst, uint8_t src) {
    tc_emit(tc, 0x48); tc_emit(tc, 0x0F); tc_emit(tc, 0xB6); tc_emit(tc, 0xC0 | (dst << 3) | src);
}
static void emit_ret(x86_tc_t *tc) { tc_emit(tc, 0xC3); }

/* Register mapping: vreg 0-7 → RAX,RCX,RDX,RBX,RSI,RDI,R8,R9 */
static const uint8_t vreg_map[] = { 0, 1, 2, 3, 6, 7, 8, 9 };

static int alloc_vreg_x86(x86_tc_t *tc, int slot) {
    if (tc->slot_to_reg[slot] >= 0) return tc->slot_to_reg[slot];
    if (tc->next_vreg > 7) return -1;
    int reg = tc->next_vreg++;
    tc->slot_to_reg[slot] = reg;
    return reg;
}

int jit_trace_compile(jit_trace_t *trace) {
    if (!trace || trace->num_entries == 0) return -1;

    size_t cap = trace->num_entries * 60 + 100;
    uint8_t *code = (uint8_t *)jit_alloc_code(cap);
    if (!code) return -1;

    x86_tc_t tc = { code, 0, cap, {0}, 0, 0 };
    memset(tc.slot_to_reg, -1, sizeof(tc.slot_to_reg));

    /* Prologue: load sp into RBX (callee-saved), fp into RBP */
    /* movabs rbx, &sp; mov rbx, [rbx] */
    emit_movabs_r64(&tc, 3, (uint64_t)&sp);
    emit_mov_r64_mem(&tc, 3, 3, 0);
    /* movabs rbp, &fp; mov rbp, [rbp] */
    emit_movabs_r64(&tc, 5, (uint64_t)&fp);
    emit_mov_r64_mem(&tc, 5, 5, 0);

    /* Trace body */
    for (uint32_t i = 0; i < trace->num_entries; i++) {
        trace_entry_t *e = &trace->entries[i];
        switch (e->opcode) {
        case 15: /* F_CONST0 */
        {
            int slot = tc.sp_slot++;
            int vr = alloc_vreg_x86(&tc, slot);
            if (vr < 0) goto fail;
            uint8_t r = vreg_map[vr];
            emit_mov_w_imm_mem(&tc, 3, slot * SVALUE_SIZE, T_NUMBER_VAL);
            /* store 0: use RAX as temp */
            emit_movabs_r64(&tc, 0, 0);
            emit_mov_mem_r64(&tc, 3, slot * SVALUE_SIZE + 8, 0);
            break;
        }
        case 16: /* F_CONST1 */
        {
            int slot = tc.sp_slot++;
            int vr = alloc_vreg_x86(&tc, slot);
            if (vr < 0) goto fail;
            emit_mov_w_imm_mem(&tc, 3, slot * SVALUE_SIZE, T_NUMBER_VAL);
            emit_movabs_r64(&tc, 0, 1);
            emit_mov_mem_r64(&tc, 3, slot * SVALUE_SIZE + 8, 0);
            break;
        }
        case 61: /* F_LOCAL */
        {
            int local_idx = e->imm & 0xFF;
            int slot = tc.sp_slot++;
            int vr = alloc_vreg_x86(&tc, slot);
            if (vr < 0) goto fail;
            uint8_t r = vreg_map[vr];
            /* Copy type from fp[local_idx] */
            emit_mov_r64_mem(&tc, 0, 5, local_idx * SVALUE_SIZE);
            emit_mov_mem_r64(&tc, 3, slot * SVALUE_SIZE, 0);
            /* Copy number */
            emit_mov_r64_mem(&tc, r, 5, local_idx * SVALUE_SIZE + 8);
            emit_mov_mem_r64(&tc, 3, slot * SVALUE_SIZE + 8, r);
            break;
        }
        case 255: /* ADD_INT_FAST */
        {
            if (tc.sp_slot < 2) goto fail;
            int rhs_slot = tc.sp_slot - 1;
            int lhs_slot = tc.sp_slot - 2;
            int rhs_vr = tc.slot_to_reg[rhs_slot];
            int lhs_vr = tc.slot_to_reg[lhs_slot];
            if (rhs_vr < 0) { rhs_vr = alloc_vreg_x86(&tc, rhs_slot); emit_mov_r64_mem(&tc, vreg_map[rhs_vr], 3, rhs_slot*SVALUE_SIZE+8); }
            if (lhs_vr < 0) { lhs_vr = alloc_vreg_x86(&tc, lhs_slot); emit_mov_r64_mem(&tc, vreg_map[lhs_vr], 3, lhs_slot*SVALUE_SIZE+8); }
            emit_add_r64(&tc, vreg_map[lhs_vr], vreg_map[rhs_vr]);
            emit_mov_mem_r64(&tc, 3, lhs_slot*SVALUE_SIZE+8, vreg_map[lhs_vr]);
            tc.sp_slot--;
            tc.slot_to_reg[rhs_slot] = -1;
            break;
        }
        case 253: /* LT_INT_FAST */
        {
            if (tc.sp_slot < 2) goto fail;
            int rhs_slot = tc.sp_slot - 1;
            int lhs_slot = tc.sp_slot - 2;
            int rhs_vr = tc.slot_to_reg[rhs_slot];
            int lhs_vr = tc.slot_to_reg[lhs_slot];
            if (rhs_vr < 0) { rhs_vr = alloc_vreg_x86(&tc, rhs_slot); emit_mov_r64_mem(&tc, vreg_map[rhs_vr], 3, rhs_slot*SVALUE_SIZE+8); }
            if (lhs_vr < 0) { lhs_vr = alloc_vreg_x86(&tc, lhs_slot); emit_mov_r64_mem(&tc, vreg_map[lhs_vr], 3, lhs_slot*SVALUE_SIZE+8); }
            emit_cmp_r64(&tc, vreg_map[lhs_vr], vreg_map[rhs_vr]);
            emit_setcc(&tc, 0x0C, 0); /* SETL AL */
            emit_movzx_r64_r8(&tc, 0, 0);
            emit_mov_mem_r64(&tc, 3, lhs_slot*SVALUE_SIZE+8, 0);
            emit_mov_w_imm_mem(&tc, 3, lhs_slot*SVALUE_SIZE, T_NUMBER_VAL);
            tc.sp_slot--;
            tc.slot_to_reg[rhs_slot] = -1;
            break;
        }
        default:
            printf("JIT: x86_64 trace compile aborted at opcode %u\n", e->opcode);
            goto fail;
        }
    }

    /* Epilogue */
    emit_ret(&tc);

    trace->native_code = (void *)code;
    trace->code_size = tc.len;
    printf("JIT: compiled trace #%u → %zu bytes x86_64 native (%u opcodes)\n",
           trace->id, trace->code_size, trace->num_entries);
    return 0;

fail:
    printf("JIT: x86_64 trace #%u compilation failed\n", trace->id);
    return -1;
}

#else /* !__x86_64__ */
int jit_trace_compile(jit_trace_t *trace) { (void)trace; return -1; }
#endif
