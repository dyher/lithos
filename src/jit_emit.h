#ifndef LITHOS_JIT_EMIT_H
#define LITHOS_JIT_EMIT_H

#include "jit.h"

/* Emit native code for F_CONST0 into JIT code pool.
 * Returns pointer to emitted code, or NULL on failure. */
void *jit_emit_const0(void);

/* Emit native code for F_CONST1 into JIT code pool. */
void *jit_emit_const1(void);

void *jit_emit_return_zero(void);
void *jit_emit_local(void);

/* Initialize platform-specific emitter. */
void jit_emit_init(void);

#endif /* LITHOS_JIT_EMIT_H */
