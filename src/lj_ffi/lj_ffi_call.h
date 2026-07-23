#pragma once

/*
 * lj_ffi_call.h — Dynamic FFI Call Engine
 *
 * IMPORTANT: Include lj_ffi_cdef.h BEFORE this header to get
 * the full lj_ffi_func_t definition.
 */

#include <ffi.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Requires lj_ffi_func_t to be fully defined (from lj_ffi_cdef.h) */

/*
 * Dynamically call a registered + bound C function.
 * Builds/retrieves cached ffi_cif, calls ffi_call(), stores result.
 * Returns 0 on success, -1 on error.
 */
int lj_ffi_call_func(lj_ffi_func_t *func, void **args, void *ret_buf);

/* Get or lazily build the ffi_cif for a function (cached after first build) */
ffi_cif *lj_ffi_get_cif(lj_ffi_func_t *func);

#ifdef __cplusplus
}
#endif
