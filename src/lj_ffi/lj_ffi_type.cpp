/*
 * lj_ffi_type.cpp — LJ-FFI Dynamic Type System Implementation
 *
 * Provides C type representation, registration, and libffi bridge
 * for the dynamic FFI efun layer.
 */

#include "lj_ffi.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <unordered_map>
#include <string>

/* ============================================================
 * Internal State
 * ============================================================ */

std::unordered_map<std::string, lj_ffi_type_t*> g_type_registry;
static bool g_initialized = false;

/* Pre-built primitive ffi_type mappings */
static ffi_type s_ffi_void   = { 0, 0, FFI_TYPE_VOID, nullptr };
static ffi_type s_ffi_int8   = { sizeof(int8_t),   0, FFI_TYPE_SINT8, nullptr };
static ffi_type s_ffi_uint8  = { sizeof(uint8_t),  0, FFI_TYPE_UINT8, nullptr };
static ffi_type s_ffi_int16  = { sizeof(int16_t),  0, FFI_TYPE_SINT16, nullptr };
static ffi_type s_ffi_uint16 = { sizeof(uint16_t), 0, FFI_TYPE_UINT16, nullptr };
static ffi_type s_ffi_int32  = { sizeof(int32_t),  0, FFI_TYPE_SINT32, nullptr };
static ffi_type s_ffi_uint32 = { sizeof(uint32_t), 0, FFI_TYPE_UINT32, nullptr };
static ffi_type s_ffi_int64  = { sizeof(int64_t),  0, FFI_TYPE_SINT64, nullptr };
static ffi_type s_ffi_uint64 = { sizeof(uint64_t), 0, FFI_TYPE_UINT64, nullptr };
static ffi_type s_ffi_float  = { sizeof(float),    0, FFI_TYPE_FLOAT, nullptr };
static ffi_type s_ffi_double = { sizeof(double),   0, FFI_TYPE_DOUBLE, nullptr };
static ffi_type s_ffi_pointer= { sizeof(void*),    0, FFI_TYPE_POINTER, nullptr };

/* ============================================================
 * Primitive Type Bootstrap
 * ============================================================ */

static void register_primitive(const char *name, lj_ffi_type_kind_t kind,
                               size_t size, size_t align)
{
    lj_ffi_type_t *t = lj_ffi_type_alloc(kind);
    t->name = strdup(name);
    t->size = size;
    t->alignment = align;
    lj_ffi_type_register(name, t);
}

/* ============================================================
 * Public API Implementation
 * ============================================================ */

void lj_ffi_type_init(void)
{
    if (g_initialized) return;

    /* Register all primitive types */
    register_primitive("void",    LJ_FFI_VOID,    0,               0);
    register_primitive("int8",    LJ_FFI_INT8,    sizeof(int8_t),  1);
    register_primitive("uint8",   LJ_FFI_UINT8,   sizeof(uint8_t), 1);
    register_primitive("int16",   LJ_FFI_INT16,   sizeof(int16_t), 2);
    register_primitive("uint16",  LJ_FFI_UINT16,  sizeof(uint16_t), 2);
    register_primitive("int32",   LJ_FFI_INT32,   sizeof(int32_t), 4);
    register_primitive("uint32",  LJ_FFI_UINT32,  sizeof(uint32_t), 4);
    register_primitive("int",     LJ_FFI_INT32,   sizeof(int32_t), 4);
    register_primitive("int64",   LJ_FFI_INT64,   sizeof(int64_t), 8);
    register_primitive("uint64",  LJ_FFI_UINT64,  sizeof(uint64_t), 8);
    register_primitive("float",   LJ_FFI_FLOAT,   sizeof(float),   4);
    register_primitive("double",  LJ_FFI_DOUBLE,  sizeof(double),  8);
    register_primitive("pointer", LJ_FFI_POINTER, sizeof(void*),   sizeof(void*));
    register_primitive("size_t",  LJ_FFI_SIZE_T,  sizeof(size_t),  sizeof(size_t));

    /* Common aliases */
    register_primitive("char",    LJ_FFI_INT8,    sizeof(char),    1);
    register_primitive("short",   LJ_FFI_INT16,   sizeof(short),   2);
    register_primitive("long",    LJ_FFI_INT64,   sizeof(long),    sizeof(long));
    register_primitive("bool",    LJ_FFI_UINT8,   1,               1);

    g_initialized = true;
    fprintf(stderr, "LJ-FFI: type system initialized (%zu primitives)\n",
            g_type_registry.size());
}

lj_ffi_type_t *lj_ffi_type_alloc(lj_ffi_type_kind_t kind)
{
    lj_ffi_type_t *t = (lj_ffi_type_t*)calloc(1, sizeof(lj_ffi_type_t));
    if (!t) return nullptr;
    t->kind = kind;
    t->array_length = -1;
    return t;
}

int lj_ffi_type_register(const char *name, lj_ffi_type_t *type)
{
    if (!name || !type) return -1;
    type->name = strdup(name);
    g_type_registry[std::string(name)] = type;
    return 0;
}

lj_ffi_type_t *lj_ffi_type_lookup(const char *name)
{
    if (!name) return nullptr;
    auto it = g_type_registry.find(std::string(name));
    if (it == g_type_registry.end()) return nullptr;
    return it->second;
}

size_t lj_ffi_type_sizeof(lj_ffi_type_t *type)
{
    if (!type) return 0;
    return type->size;
}

lj_ffi_field_t *lj_ffi_struct_find_field(lj_ffi_type_t *st, const char *field_name)
{
    if (!st || st->kind != LJ_FFI_STRUCT || !field_name) return nullptr;
    for (int i = 0; i < st->num_fields; i++) {
        if (strcmp(st->fields[i].name, field_name) == 0) {
            return &st->fields[i];
        }
    }
    return nullptr;
}

/* ============================================================
 * libffi Bridge: lj_ffi_type_t → ffi_type*
 * ============================================================ */

static ffi_type *build_struct_ffi_type(lj_ffi_type_t *st)
{
    /* Build ffi_type.elements array for struct */
    int n = st->num_fields;
    ffi_type **elements = (ffi_type**)calloc(n + 1, sizeof(ffi_type*));
    if (!elements) return nullptr;

    for (int i = 0; i < n; i++) {
        elements[i] = lj_ffi_type_to_ffi_type(st->fields[i].field_type);
    }
    elements[n] = nullptr; /* null-terminated */

    ffi_type *ft = (ffi_type*)calloc(1, sizeof(ffi_type));
    ft->type = FFI_TYPE_STRUCT;
    ft->size = st->size;
    ft->alignment = st->alignment;
    ft->elements = elements;
    return ft;
}

ffi_type *lj_ffi_type_to_ffi_type(lj_ffi_type_t *type)
{
    if (!type) return &s_ffi_void;

    /* Return cached result if available */
    if (type->cached_ffi_type) return type->cached_ffi_type;

    ffi_type *result = nullptr;

    switch (type->kind) {
        case LJ_FFI_VOID:    result = &s_ffi_void;    break;
        case LJ_FFI_INT8:    result = &s_ffi_int8;    break;
        case LJ_FFI_UINT8:   result = &s_ffi_uint8;   break;
        case LJ_FFI_INT16:   result = &s_ffi_int16;   break;
        case LJ_FFI_UINT16:  result = &s_ffi_uint16;  break;
        case LJ_FFI_INT32:   result = &s_ffi_int32;   break;
        case LJ_FFI_UINT32:  result = &s_ffi_uint32;  break;
        case LJ_FFI_INT64:   result = &s_ffi_int64;   break;
        case LJ_FFI_UINT64:  result = &s_ffi_uint64;  break;
        case LJ_FFI_FLOAT:   result = &s_ffi_float;   break;
        case LJ_FFI_DOUBLE:  result = &s_ffi_double;  break;
        case LJ_FFI_POINTER: result = &s_ffi_pointer; break;
        case LJ_FFI_SIZE_T:
            result = (sizeof(size_t) == 8) ? &s_ffi_uint64 : &s_ffi_uint32;
            break;

        case LJ_FFI_STRUCT:
            result = build_struct_ffi_type(type);
            break;

        case LJ_FFI_ARRAY:
            /* Arrays decay to pointers in FFI calls */
            result = &s_ffi_pointer;
            break;

        case LJ_FFI_FUNC_PTR:
            /* Function pointers are just pointers at the ABI level */
            result = &s_ffi_pointer;
            break;
    }

    /* Cache for future lookups (primitives are static, don't cache them) */
    if (type->kind >= LJ_FFI_STRUCT && result) {
        type->cached_ffi_type = result;
    }

    return result;
}

/* ============================================================
 * Cleanup
 * ============================================================ */

void lj_ffi_type_free(lj_ffi_type_t *type)
{
    if (!type) return;

    if (type->kind == LJ_FFI_STRUCT) {
        for (int i = 0; i < type->num_fields; i++) {
            free((void*)type->fields[i].name);
        }
        free(type->fields);
    } else if (type->kind == LJ_FFI_ARRAY) {
        /* Don't free element_type — it's owned by the registry */
    } else if (type->kind == LJ_FFI_FUNC_PTR) {
        free(type->param_types);
    }

    /* Free cached ffi_type for non-primitives */
    if (type->kind >= LJ_FFI_STRUCT && type->cached_ffi_type) {
        if (type->cached_ffi_type->elements) {
            free(type->cached_ffi_type->elements);
        }
        free(type->cached_ffi_type);
    }

    free((void*)type->name);
    free(type);
}
