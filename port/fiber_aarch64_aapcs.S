/* Lithos Fiber Context Switch - AArch64 AAPCS64 ABI */

.text
.globl fiber_switch
.type fiber_switch, %function
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
.size fiber_switch, .-fiber_switch

.globl fiber_make
.type fiber_make, %function
fiber_make:
    /* x0=ctx, x1=entry_fn, x2=arg */
    ldr x3, [x0, #8]    /* stack_base */
    ldr x4, [x0, #16]   /* stack_size */
    add x3, x3, x4      /* stack top */
    and x3, x3, #-16    /* align 16 */

    /* Push in SAME order as fiber_switch saves (so restore matches).
     * fiber_switch saves: x29/x30 first (highest addr), d14/d15 last (lowest).
     * We push from top down: x29/x30, then x27/x28, ..., d14/d15.
     * Then entry_fn/arg below everything. */

    /* Reserve space for entry_fn + arg (will be consumed by trampoline) */
    sub x3, x3, #16
    stp x1, x2, [x3]    /* entry_fn at [x3], arg at [x3+8] */

    /* Now push callee-saved regs in save-order (top to bottom) */
    mov x4, #0
    /* x29(FP)=0, x30(LR)=trampoline address */
    sub x3, x3, #16
    str x4, [x3]            /* x29 = 0 */
    adr x5, fiber_entry_trampoline
    str x5, [x3, #8]        /* x30 = trampoline (LR for ret) */

    stp x4, x4, [x3, #-16]! /* x27, x28 */
    stp x4, x4, [x3, #-16]! /* x25, x26 */
    stp x4, x4, [x3, #-16]! /* x23, x24 */
    stp x4, x4, [x3, #-16]! /* x21, x22 */
    stp x4, x4, [x3, #-16]! /* x19, x20 */
    stp x4, x4, [x3, #-16]! /* d8, d9 */
    stp x4, x4, [x3, #-16]! /* d10, d11 */
    stp x4, x4, [x3, #-16]! /* d12, d13 */
    stp x4, x4, [x3, #-16]! /* d14, d15 <- sp will point here after make */

    /* ctx->sp = final sp (points to d14,d15 slot) */
    str x3, [x0]
    ret
.size fiber_make, .-fiber_make

/* Trampoline: first thing fiber runs after fiber_switch restores regs.
 * x30 (LR) was set to here, so if entry_fn returns, we land here too.
 * On first entry: load entry_fn+arg from stack, call it.
 * On return from entry_fn: fall through to fiber_on_exit. */
fiber_entry_trampoline:
    /* entry_fn and arg are at [sp] and [sp+8] (pushed by fiber_make above regs) */
    ldp x19, x20, [sp], #16  /* x19=entry_fn, x20=arg; sp now past them */
    mov x0, x20
    blr x19                  /* call entry_fn(arg); if it returns, fall through */
    /* entry_fn returned -> safe exit */
    b fiber_on_exit
