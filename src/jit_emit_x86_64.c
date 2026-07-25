
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "jit_emit.h"
#include "interpret.h"
#include <stdio.h>
#include <string.h>

#if defined(__x86_64__) || defined(_M_X64)

#define SVALUE_SIZE     16
#define SVALUE_TYPE_OFF 0
#define SVALUE_NUM_OFF  8
#define T_NUMBER_VAL    0x2

/* x86_64 code emission helpers */
typedef struct {
    uint8_t *buf;
    size_t   len;
    size_t   cap;
} x86_emitter_t;

static inline void emit_byte(x86_emitter_t *e, uint8_t b) {
    if (e->len < e->cap) e->buf[e->len++] = b;
}
static inline void emit_le32(x86_emitter_t *e, uint32_t v) {
    emit_byte(e, v & 0xFF);
    emit_byte(e, (v >> 8) & 0xFF);
    emit_byte(e, (v >> 16) & 0xFF);
    emit_byte(e, (v >> 24) & 0xFF);
}
static inline void emit_le64(x86_emitter_t *e, uint64_t v) {
    emit_le32(e, (uint32_t)v);
    emit_le32(e, (uint32_t)(v >> 32));
}

void jit_emit_init(void) {
    printf("JIT: x86_64 code emitter initialized\n");
}

/* F_CONST0: (++sp)->type = T_NUMBER; sp->u.number = 0;
 *
 * Strategy: use MOVABS to load &sp into RAX (simplest, no RIP-relative complexity)
 *   movabs rax, <&sp>           ; 10 bytes: REX.W B8 <imm64>
 *   lea    rax, [rax+16]        ; 4 bytes:  REX.W 8D 40 10
 *   movabs rcx, <&sp>           ; 10 bytes
 *   mov    [rcx], rax           ; 3 bytes:  REX.W 89 01
 *   mov    word [rax], 0x2      ; 5 bytes:  66 C7 00 02 00
 *   mov    qword [rax+8], 0     ; 8 bytes:  REX.W C7 40 08 00 00 00 00
 *   ret                         ; 1 byte:   C3
 * Total: ~41 bytes
 */
void *jit_emit_const0(void) {
    if (!jit_enabled()) return NULL;
    size_t cap = 64;
    uint8_t *code = (uint8_t *)jit_alloc_code(cap);
    if (!code) return NULL;
    x86_emitter_t e = { code, 0, cap };
    uint64_t sp_addr = (uint64_t)&sp;

    /* movabs rax, sp_addr */
    emit_byte(&e, 0x48); emit_byte(&e, 0xB8); emit_le64(&e, sp_addr);
    /* lea rax, [rax+16] */
    emit_byte(&e, 0x48); emit_byte(&e, 0x8D); emit_byte(&e, 0x40); emit_byte(&e, SVALUE_SIZE);
    /* movabs rcx, sp_addr */
    emit_byte(&e, 0x48); emit_byte(&e, 0xB9); emit_le64(&e, sp_addr);
    /* mov [rcx], rax */
    emit_byte(&e, 0x48); emit_byte(&e, 0x89); emit_byte(&e, 0x01);
    /* mov word [rax], T_NUMBER */
    emit_byte(&e, 0x66); emit_byte(&e, 0xC7); emit_byte(&e, 0x00);
    emit_byte(&e, T_NUMBER_VAL); emit_byte(&e, 0x00);
    /* mov qword [rax+8], 0 */
    emit_byte(&e, 0x48); emit_byte(&e, 0xC7); emit_byte(&e, 0x40);
    emit_byte(&e, SVALUE_NUM_OFF); emit_le32(&e, 0);
    /* ret */
    emit_byte(&e, 0xC3);

    printf("JIT: emitted F_CONST0 x86_64 native code (%zu bytes)\n", e.len);
    return (void *)code;
}

/* F_CONST1: same as CONST0 but store 1 instead of 0 */
void *jit_emit_const1(void) {
    if (!jit_enabled()) return NULL;
    size_t cap = 64;
    uint8_t *code = (uint8_t *)jit_alloc_code(cap);
    if (!code) return NULL;
    x86_emitter_t e = { code, 0, cap };
    uint64_t sp_addr = (uint64_t)&sp;

    emit_byte(&e, 0x48); emit_byte(&e, 0xB8); emit_le64(&e, sp_addr);
    emit_byte(&e, 0x48); emit_byte(&e, 0x8D); emit_byte(&e, 0x40); emit_byte(&e, SVALUE_SIZE);
    emit_byte(&e, 0x48); emit_byte(&e, 0xB9); emit_le64(&e, sp_addr);
    emit_byte(&e, 0x48); emit_byte(&e, 0x89); emit_byte(&e, 0x01);
    emit_byte(&e, 0x66); emit_byte(&e, 0xC7); emit_byte(&e, 0x00);
    emit_byte(&e, T_NUMBER_VAL); emit_byte(&e, 0x00);
    /* mov qword [rax+8], 1 */
    emit_byte(&e, 0x48); emit_byte(&e, 0xC7); emit_byte(&e, 0x40);
    emit_byte(&e, SVALUE_NUM_OFF); emit_le32(&e, 1);
    emit_byte(&e, 0xC3);

    printf("JIT: emitted F_CONST1 x86_64 native code (%zu bytes)\n", e.len);
    return (void *)code;
}

/* F_RETURN_ZERO: identical to CONST0 */
void *jit_emit_return_zero(void) {
    return jit_emit_const0();
}

/* F_LOCAL: (++sp) = fp[*pc++];
 *   movabs rax, <&sp>
 *   movabs rbx, <&fp>
 *   movabs rcx, <&pc>
 *   mov    rdx, [rax]          ; sp
 *   mov    rsi, [rbx]          ; fp
 *   mov    rdi, [rcx]          ; pc
 *   movzx  r8d, byte [rdi]     ; idx = *pc
 *   inc    rdi                 ; pc++
 *   mov    [rcx], rdi          ; store pc
 *   shl    r8, 4               ; idx * 16
 *   add    rsi, r8             ; fp + idx*16
 *   lea    rdx, [rdx+16]       ; ++sp
 *   mov    [rax], rdx          ; store sp
 *   movups xmm0, [rsi]         ; load 16 bytes from fp[idx]
 *   movups [rdx], xmm0         ; store 16 bytes to sp
 *   ret
 */
void *jit_emit_local(void) {
    if (!jit_enabled()) return NULL;
    extern const char *pc;
    size_t cap = 128;
    uint8_t *code = (uint8_t *)jit_alloc_code(cap);
    if (!code) return NULL;
    x86_emitter_t e = { code, 0, cap };
    uint64_t sp_addr = (uint64_t)&sp;
    uint64_t fp_addr = (uint64_t)&fp;
    uint64_t pc_addr = (uint64_t)&pc;

    /* movabs rax, &sp */
    emit_byte(&e, 0x48); emit_byte(&e, 0xB8); emit_le64(&e, sp_addr);
    /* movabs rbx, &fp */
    emit_byte(&e, 0x48); emit_byte(&e, 0xBB); emit_le64(&e, fp_addr);
    /* movabs rcx, &pc */
    emit_byte(&e, 0x48); emit_byte(&e, 0xB9); emit_le64(&e, pc_addr);
    /* mov rdx, [rax] (sp) */
    emit_byte(&e, 0x48); emit_byte(&e, 0x8B); emit_byte(&e, 0x10);
    /* mov rsi, [rbx] (fp) */
    emit_byte(&e, 0x48); emit_byte(&e, 0x8B); emit_byte(&e, 0x33);
    /* mov rdi, [rcx] (pc) */
    emit_byte(&e, 0x48); emit_byte(&e, 0x8B); emit_byte(&e, 0x39);
    /* movzx r8d, byte [rdi] (idx) */
    emit_byte(&e, 0x44); emit_byte(&e, 0x0F); emit_byte(&e, 0xB6); emit_byte(&e, 0x07);
    /* inc rdi (pc++) */
    emit_byte(&e, 0x48); emit_byte(&e, 0xFF); emit_byte(&e, 0xC7);
    /* mov [rcx], rdi (store pc) */
    emit_byte(&e, 0x48); emit_byte(&e, 0x89); emit_byte(&e, 0x39);
    /* shl r8, 4 (idx*16) */
    emit_byte(&e, 0x49); emit_byte(&e, 0xC1); emit_byte(&e, 0xE0); emit_byte(&e, 0x04);
    /* add rsi, r8 (fp + idx*16) */
    emit_byte(&e, 0x4C); emit_byte(&e, 0x01); emit_byte(&e, 0xC6);
    /* lea rdx, [rdx+16] (++sp) */
    emit_byte(&e, 0x48); emit_byte(&e, 0x8D); emit_byte(&e, 0x52); emit_byte(&e, SVALUE_SIZE);
    /* mov [rax], rdx (store sp) */
    emit_byte(&e, 0x48); emit_byte(&e, 0x89); emit_byte(&e, 0x10);
    /* movups xmm0, [rsi] (load 16 bytes) */
    emit_byte(&e, 0x0F); emit_byte(&e, 0x10); emit_byte(&e, 0x06);
    /* movups [rdx], xmm0 (store 16 bytes) */
    emit_byte(&e, 0x0F); emit_byte(&e, 0x11); emit_byte(&e, 0x02);
    /* ret */
    emit_byte(&e, 0xC3);

    printf("JIT: emitted F_LOCAL x86_64 native code (%zu bytes)\n", e.len);
    return (void *)code;
}

#else /* !__x86_64__ */
void jit_emit_init(void) { printf("JIT: no x86_64 emitter on this platform\n"); }
void *jit_emit_const0(void) { return NULL; }
void *jit_emit_const1(void) { return NULL; }
void *jit_emit_return_zero(void) { return NULL; }
void *jit_emit_local(void) { return NULL; }
#endif
