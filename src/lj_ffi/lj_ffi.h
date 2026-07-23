#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <ffi.h>

/* ============================================================
 * LJ-FFI Type System — Dynamic C Type Representation
 * ============================================================ */

typedef enum {
    LJ_FFI_VOID,
    LJ_FFI_INT8, LJ_FFI_UINT8,
    LJ_FFI_INT16, LJ_FFI_UINT16,
    LJ_FFI_INT32, LJ_FFI_UINT32,
    LJ_FFI_INT64, LJ_FFI_UINT64,
    LJ_FFI_FLOAT, LJ_FFI_DOUBLE,
    LJ_FFI_POINTER,
    LJ_FFI_SIZE_T,
    LJ_FFI_STRUCT,
    LJ_FFI_ARRAY,
    LJ_FFI_FUNC_PTR
} lj_ffi_type_kind_t;

/* Struct field descriptor */
typedef struct {
    const char         *name;
    struct lj_ffi_type_s *field_type;
    size_t              offset;
} lj_ffi_field_t;

/* Core type representation */
typedef struct lj_ffi_type_s {
    lj_ffi_type_kind_t  kind;
    const char         *name;       /* registered name, NULL for anonymous */
    size_t              size;
    size_t              alignment;

    /* For STRUCT */
    lj_ffi_field_t     *fields;
    int                 num_fields;

    /* For ARRAY */
    struct lj_ffi_type_s *element_type;
    int                   array_length;  /* -1 = unsized pointer-like */

    /* For FUNC_PTR */
    struct lj_ffi_type_s *ret_type;
    struct lj_ffi_type_s **param_types;
    int                   num_params;

    /* Cached libffi ffi_type* (built lazily) */
    ffi_type           *cached_ffi_type;
} lj_ffi_type_t;

/* ---- Public API ---- */

/* Initialize the type system (call once at driver startup) */
void lj_ffi_type_init(void);

/* Register a named type */
int lj_ffi_type_register(const char *name, lj_ffi_type_t *type);

/* Lookup a type by name */
lj_ffi_type_t *lj_ffi_type_lookup(const char *name);

/* Get/create the corresponding libffi ffi_type* */
ffi_type *lj_ffi_type_to_ffi_type(lj_ffi_type_t *type);

/* Get sizeof a type */
size_t lj_ffi_type_sizeof(lj_ffi_type_t *type);

/* Find a struct field by name, returns NULL if not found */
lj_ffi_field_t *lj_ffi_struct_find_field(lj_ffi_type_t *st, const char *field_name);

/* Allocate a new lj_ffi_type_t (caller fills in details) */
lj_ffi_type_t *lj_ffi_type_alloc(lj_ffi_type_kind_t kind);

/* Free a type and its sub-types (recursive) */
void lj_ffi_type_free(lj_ffi_type_t *type);

#ifdef __cplusplus
}
#endif

/* Internal C++ helpers — MUST be outside extern "C" */
#ifdef __cplusplus
#include <unordered_map>
#include <string>
extern std::unordered_map<std::string, lj_ffi_type_t*> g_type_registry;
inline void g_type_registry_ref_hack(const std::string &name, lj_ffi_type_t *type) {
    g_type_registry[name] = type;
}
#endif
