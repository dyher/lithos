/* Lithos Fiber Context Switch - x86_64 Windows ABI (GAS syntax for MinGW/clang-cl)
 * Callee-saved: rbx, rbp, rdi, rsi, r12-r15
 * Args: rcx=from, rdx=to (fiber_switch) / rcx=ctx, rdx=entry_fn, r8=arg (fiber_make)
 */

.text
.globl fiber_switch
fiber_switch:
    push %rbp
    push %rbx
    push %rdi
    push %rsi
    push %r12
    push %r13
    push %r14
    push %r15
    mov  %rsp, (%rcx)       /* from->sp = rsp */
    mov  (%rdx), %rsp       /* rsp = to->sp */
    pop  %r15
    pop  %r14
    pop  %r13
    pop  %r12
    pop  %rsi
    pop  %rdi
    pop  %rbx
    pop  %rbp
    ret

.globl fiber_make
fiber_make:
    mov  %r8, %rax          /* save arg */
    mov  16(%rcx), %r9      /* ctx->stack_base */
    add  24(%rcx), %r9      /* + stack_size = top */
    and  $-16, %r9          /* align 16 */
    sub  $8, %r9
    mov  %rdx, (%r9)        /* entry_fn on stack */
    sub  $8, %r9
    mov  %rax, (%r9)        /* arg on stack */
    xor  %rax, %rax
    sub  $8, %r9; mov %rax, (%r9)  /* rbp */
    sub  $8, %r9; mov %rax, (%r9)  /* rbx */
    sub  $8, %r9; mov %rax, (%r9)  /* rdi */
    sub  $8, %r9; mov %rax, (%r9)  /* rsi */
    sub  $8, %r9; mov %rax, (%r9)  /* r12 */
    sub  $8, %r9; mov %rax, (%r9)  /* r13 */
    sub  $8, %r9; mov %rax, (%r9)  /* r14 */
    sub  $8, %r9; mov %rax, (%r9)  /* r15 */
    lea  fiber_trampoline(%rip), %rax
    sub  $8, %r9
    mov  %rax, (%r9)
    mov  %r9, (%rcx)        /* ctx->sp */
    ret

fiber_trampoline:
    pop  %rcx               /* arg -> first param register */
    pop  %rax               /* entry_fn */
    jmp  *%rax
