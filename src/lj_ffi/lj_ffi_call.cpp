/*
 * lj_ffi_call.cpp — Dynamic FFI Call Engine
 *
 * Builds ffi_cif on demand, caches it, and executes ffi_call().
 * This is the heart of LJ-FFI's dynamic calling capability.
 */

#include "lj_ffi.h"
#include "lj_ffi_cdef.h"
#include "lj_ffi_call.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>

/* ============================================================
 * CIF Cache
 * ============================================================ */

ffi_cif *lj_ffi_get_cif(lj_ffi_func_t *func)
{
    if (!func) return nullptr;

    /* Return cached CIF if already built */
    if (func->cached_cif) return func->cached_cif;

    /* Build ffi_cif from function metadata */
    ffi_cif *cif = (ffi_cif*)calloc(1, sizeof(ffi_cif));
    if (!cif) return nullptr;

    /* Get return type's ffi_type* */
    ffi_type *ret_ffi = lj_ffi_type_to_ffi_type(func->ret_type);
    if (!ret_ffi) {
        free(cif);
        return nullptr;
    }

    /* Build argument type array */
    int nargs = func->num_params;
    ffi_type **arg_types = nullptr;

    if (nargs > 0) {
        arg_types = (ffi_type**)calloc(nargs, sizeof(ffi_type*));
        if (!arg_types) {
            free(cif);
            return nullptr;
        }

        for (int i = 0; i < nargs; i++) {
            arg_types[i] = lj_ffi_type_to_ffi_type(func->param_types[i]);
            if (!arg_types[i]) {
                fprintf(stderr, "LJ-FFI call: failed to resolve param type %d for '%s'\n",
                        i, func->name);
                free(arg_types);
                free(cif);
                return nullptr;
            }
        }
    }

    /* Prepare the CIF */
    ffi_status status = ffi_prep_cif(cif, FFI_DEFAULT_ABI, nargs, ret_ffi, arg_types);
    if (status != FFI_OK) {
        fprintf(stderr, "LJ-FFI call: ffi_prep_cif failed for '%s' (status=%d)\n",
                func->name, (int)status);
        free(arg_types);
        free(cif);
        return nullptr;
    }

    /* Cache and return */
    func->cached_cif = cif;

    fprintf(stderr, "LJ-FFI call: built CIF for '%s' (%d args, ret=%d)\n",
            func->name, nargs, (int)ret_ffi->type);

    return cif;
}

/* ============================================================
 * Dynamic Call
 * ============================================================ */

int lj_ffi_call_func(lj_ffi_func_t *func, void **args, void *ret_buf)
{
    if (!func || !func->func_ptr) {
        fprintf(stderr, "LJ-FFI call: function not bound\n");
        return -1;
    }

    ffi_cif *cif = lj_ffi_get_cif(func);
    if (!cif) {
        fprintf(stderr, "LJ-FFI call: failed to build CIF for '%s'\n", func->name);
        return -1;
    }

    /* Execute the call via libffi */
    ffi_call(cif, (void (*)(void))func->func_ptr, ret_buf, args);

    return 0;
}
