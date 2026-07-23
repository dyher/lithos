#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Parse a C-style declaration string and register types/functions.
 *
 * Supported subset:
 *   - typedef struct { field_t name; ... } alias_t;
 *   - typedef primitive_t alias_t;
 *   - ret_t func_name(arg1_t arg1, arg2_t arg2);
 *
 * Returns 0 on success, -1 on parse error (message printed to stderr).
 */
int lj_ffi_cdef_parse(const char *source);

/*
 * Register a callable C function by name + signature.
 * Called internally by cdef parser when it encounters a function decl.
 * The actual function pointer is resolved later via dlsym or explicit binding.
 */
int lj_ffi_func_register(const char *name, lj_ffi_type_t *ret_type,
                         lj_ffi_type_t **param_types, int num_params);

/*
 * Lookup a registered function's metadata.
 */
typedef struct {
    const char       *name;
    lj_ffi_type_t    *ret_type;
    lj_ffi_type_t   **param_types;
    int               num_params;
    void             *func_ptr;      /* resolved address, NULL until bound */
    ffi_cif          *cached_cif;    /* lazily built */
} lj_ffi_func_t;

lj_ffi_func_t *lj_ffi_func_lookup(const char *name);

/*
 * Bind a function name to an actual C function pointer.
 * Used after dlsym or manual registration.
 */
int lj_ffi_func_bind(const char *name, void *ptr);

/* Initialize function registry */
void lj_ffi_func_init(void);

#ifdef __cplusplus
}
#endif
