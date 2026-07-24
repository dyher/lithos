/* Lithos Fiber Context Switch - AArch64 AAPCS64 ABI
 * Callee-saved: x19-x28, x29(FP), x30(LR), d8-d15
 * Stack must be 16-byte aligned.
 */

.text
.globl fiber_switch
.type fiber_switch, %function
fiber_switch:
    /* Save callee-saved regs onto current stack */
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

    /* from->sp = sp */
    mov x2, sp
    str x2, [x0]

    /* sp = to->sp */
    ldr x2, [x1]
    mov sp, x2

    /* Restore callee-saved regs */
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
.size fiber_switch, .-fiber_switch

.globl fiber_make
.type fiber_make, %function
fiber_make:
    /* ctx=x0, entry_fn=x1, arg=x2 */
    /* Compute stack top: stack_base + stack_size, align 16 */
    ldr x3, [x0, #8]   /* stack_base */
    ldr x4, [x0, #16]  /* stack_size */
    add x3, x3, x4
    and x3, x3, #-16   /* align 16 */

    /* Push trampoline args: entry_fn then arg */
    stp x1, x2, [x3, #-16]!

    /* Push zeroed callee-saved regs (10 pairs = 160 bytes) */
    mov x4, #0
    stp x4, x4, [x3, #-16]!  /* x29, x30 */
    stp x4, x4, [x3, #-16]!  /* x27, x28 */
    stp x4, x4, [x3, #-16]!  /* x25, x26 */
    stp x4, x4, [x3, #-16]!  /* x23, x24 */
    stp x4, x4, [x3, #-16]!  /* x21, x22 */
    stp x4, x4, [x3, #-16]!  /* x19, x20 */
    stp x4, x4, [x3, #-16]!  /* d8, d9 */
    stp x4, x4, [x3, #-16]!  /* d10, d11 */
    stp x4, x4, [x3, #-16]!  /* d12, d13 */
    stp x4, x4, [x3, #-16]!  /* d14, d15 */

    /* Push trampoline address as LR (x30 position already set above) */
    /* Actually we need to overwrite the x30 slot with trampoline addr */
    /* x30 is at offset +8 from the x29,x30 pair we just pushed */
    adr x4, fiber_trampoline
    str x4, [x3, #8]   /* overwrite x30 slot with trampoline */

    /* ctx->sp = prepared stack */
    str x3, [x0]
    ret
.size fiber_make, .-fiber_make

/* Trampoline: x19=entry_fn, x20=arg (loaded from stack by fiber_switch restore) */
fiber_trampoline:
    /* After fiber_switch restores regs, x19=entry_fn, x20=arg */
    mov x0, x20
    br  x19
