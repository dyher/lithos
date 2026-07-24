.text
.globl fiber_switch
.type fiber_switch, @function
fiber_switch:
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    mov  %rsp, (%rdi)
    mov  (%rsi), %rsp
    pop  %r15
    pop  %r14
    pop  %r13
    pop  %r12
    pop  %rbx
    pop  %rbp
    ret
.size fiber_switch, .-fiber_switch

.globl fiber_make
.type fiber_make, @function
fiber_make:
    mov  %rdx, %rax
    mov  16(%rdi), %rcx
    add  24(%rdi), %rcx
    and  $-16, %rcx
    sub  $8, %rcx
    mov  %rsi, (%rcx)
    sub  $8, %rcx
    mov  %rax, (%rcx)
    xor  %rax, %rax
    sub  $8, %rcx; mov %rax, (%rcx)
    sub  $8, %rcx; mov %rax, (%rcx)
    sub  $8, %rcx; mov %rax, (%rcx)
    sub  $8, %rcx; mov %rax, (%rcx)
    sub  $8, %rcx; mov %rax, (%rcx)
    sub  $8, %rcx; mov %rax, (%rcx)
    lea  fiber_trampoline(%rip), %rax
    sub  $8, %rcx
    mov  %rax, (%rcx)
    mov  %rcx, (%rdi)
    ret
.size fiber_make, .-fiber_make

fiber_trampoline:
    pop  %rdi
    pop  %rsi
    jmp  *%rsi
