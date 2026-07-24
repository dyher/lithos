/* Lithos Fiber Context Switch - AArch64 Windows ABI
 * Same callee-saved regs as AAPCS64: x19-x28, x29(FP), x30(LR), d8-d15
 * Args: x0=from, x1=to (fiber_switch) / x0=ctx, x1=entry_fn, x2=arg (fiber_make)
 * Note: Register convention identical to AAPCS64 for our use case.
 */

.text
.globl fiber_switch
fiber_switch:
    stp x29, x30, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp d8,  d9,  [sp, #-16]!
    stp d10, d11, [sp, #-16]!
    stp d12, d13, [sp, #-16]!
    stp d14, d15, [sp, #-16]!
    mov x2, sp
    str x2, [x0]
    ldr x2, [x1]
    mov sp, x2
    ldp d14, d15, [sp], #16
    ldp d12, d13, [sp], #16
    ldp d10, d11, [sp], #16
    ldp d8,  d9,  [sp], #16
    ldp x19, x20, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x27, x28, [sp], #16
    ldp x29, x30, [sp], #16
    ret

.globl fiber_make
fiber_make:
    ldr x3, [x0, #8]
    ldr x4, [x0, #16]
    add x3, x3, x4
    and x3, x3, #-16
    stp x1, x2, [x3, #-16]!
    mov x4, #0
    stp x4, x4, [x3, #-16]!
    stp x4, x4, [x3, #-16]!
    stp x4, x4, [x3, #-16]!
    stp x4, x4, [x3, #-16]!
    stp x4, x4, [x3, #-16]!
    stp x4, x4, [x3, #-16]!
    stp x4, x4, [x3, #-16]!
    stp x4, x4, [x3, #-16]!
    stp x4, x4, [x3, #-16]!
    stp x4, x4, [x3, #-16]!
    adr x4, fiber_trampoline
    str x4, [x3, #8]
    str x3, [x0]
    ret

fiber_trampoline:
    mov x0, x20
    br  x19
