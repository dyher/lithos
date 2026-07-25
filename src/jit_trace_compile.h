#ifndef LITHOS_JIT_TRACE_COMPILE_H
#define LITHOS_JIT_TRACE_COMPILE_H

#include "jit_trace.h"

/* Compile a recorded trace into native code.
 * Returns 0 on success, -1 on failure. */
int jit_trace_compile(jit_trace_t *trace);

#endif /* LITHOS_JIT_TRACE_COMPILE_H */
