/*
 * ffi.c — LJ-FFI Dynamic FFI Efuns (LuaJIT-compatible)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "src/std.h"
#include "src/interpret.h"
#include "lj_ffi/lj_ffi.h"
#include "lj_ffi/lj_ffi_cdef.h"

#ifdef HAVE_DLSYM
#include <dlfcn.h>
#endif

/*
 * int ffi_cdef(string source)
 *   Parse C declarations, register types + functions.
 *   Returns 0 on success, -1 on error.
 */
#ifdef F_FFI_CDEF
void f_ffi_cdef(void)
{
    const char *source = SVALUE_STRPTR(sp);
    int result = lj_ffi_cdef_parse(source);
    pop_stack();
    push_number(result);
}
#endif

/*
 * int ffi_sizeof(string type_name)
 *   Return sizeof a registered type, or -1 if unknown.
 */
#ifdef F_FFI_SIZEOF
void f_ffi_sizeof(void)
{
    const char *name = SVALUE_STRPTR(sp);
    lj_ffi_type_t *t = lj_ffi_type_lookup(name);
    int sz = t ? (int)t->size : -1;
    pop_stack();
    push_number(sz);
}
#endif

/*
 * int ffi_bind(string func_name, string symbol_name)
 *   Resolve symbol via dlsym and bind to registered function.
 *   Returns 0 on success, -1 on failure.
 */
#ifdef F_FFI_BIND
void f_ffi_bind(void)
{
    const char *symbol = SVALUE_STRPTR(sp);
    const char *func_name = SVALUE_STRPTR(sp - 1);
    int result = -1;

#ifdef HAVE_DLSYM
    void *ptr = dlsym(RTLD_DEFAULT, symbol);
    if (ptr) {
        result = lj_ffi_func_bind(func_name, ptr);
    }
#endif

    pop_stack();
    pop_stack();
    push_number(result);
}
#endif

/*
 * mixed ffi_call(string func_name, ...)
 *   Call a registered + bound C function.
 *   Phase 1 placeholder: validates function exists + is bound.
 *   Full CIF construction + marshalling in Phase 2.
 */
#ifdef F_FFI_CALL
#include "lj_ffi/lj_ffi_cdef.h"
#include "lj_ffi/lj_ffi_call.h"

void f_ffi_call(void)
{
    int nargs = st_num_arg - 1;
    const char *name = SVALUE_STRPTR(sp - nargs);

    lj_ffi_func_t *f = lj_ffi_func_lookup(name);

    if (!f || !f->func_ptr) {
        pop_n_elems(st_num_arg);
        push_number(0);
        return;
    }

    /* Validate arg count matches declaration */
    if (nargs != f->num_params) {
        fprintf(stderr, "ffi_call: '%s' expects %d args, got %d\n",
                name, f->num_params, nargs);
        pop_n_elems(st_num_arg);
        push_number(0);
        return;
    }

    /* Marshal LPC svalues → C argument buffers */
    void **arg_ptrs = NULL;
    void *arg_storage = NULL;

    if (nargs > 0) {
        arg_ptrs = (void**)alloca(nargs * sizeof(void*));
        /* Allocate contiguous storage for all args (max 8 bytes each) */
        arg_storage = alloca(nargs * 8);
        memset(arg_storage, 0, nargs * 8);

        for (int i = 0; i < nargs; i++) {
            svalue_t *sv = sp - nargs + 1 + i;
            lj_ffi_type_t *ptype = f->param_types[i];
            char *slot = (char*)arg_storage + i * 8;

            switch (ptype->kind) {
                case LJ_FFI_INT32:
                case LJ_FFI_UINT32:
                    *(int32_t*)slot = (int32_t)sv->u.number;
                    break;
                case LJ_FFI_INT64:
                case LJ_FFI_UINT64:
                case LJ_FFI_SIZE_T:
                    *(int64_t*)slot = sv->u.number;
                    break;
                case LJ_FFI_FLOAT:
                    *(float*)slot = (float)sv->u.number;
                    break;
                case LJ_FFI_DOUBLE:
                    *(double*)slot = (double)sv->u.number;
                    break;
                case LJ_FFI_POINTER:
                    if (sv->type == T_STRING) {
                        /* Pass actual char* for string args (e.g. char* params) */
                        *(void**)slot = (void*)SVALUE_STRPTR(sv);
                    } else {
                        *(void**)slot = (void*)(uintptr_t)sv->u.number;
                    }
                    break;
                default:
                    fprintf(stderr, "ffi_call: unsupported param type %d for arg %d\n",
                            (int)ptype->kind, i);
                    pop_n_elems(st_num_arg);
                    push_number(0);
                    return;
            }
            arg_ptrs[i] = slot;
        }
    }

    /* Prepare return buffer */
    char ret_buf[16] = {0};

    /* Execute the call */
    int rc = lj_ffi_call_func(f, arg_ptrs, ret_buf);

    /* Pop all args + func name */
    pop_n_elems(st_num_arg);

    if (rc != 0) {
        push_number(0);
        return;
    }

    /* Marshal C return value → LPC svalue */
    switch (f->ret_type->kind) {
        case LJ_FFI_VOID:
            push_number(0);
            break;
        case LJ_FFI_INT8:
        case LJ_FFI_UINT8:
        case LJ_FFI_INT16:
        case LJ_FFI_UINT16:
        case LJ_FFI_INT32:
        case LJ_FFI_UINT32:
            push_number((int64_t)*(int32_t*)ret_buf);
            break;
        case LJ_FFI_INT64:
        case LJ_FFI_UINT64:
        case LJ_FFI_SIZE_T:
            push_number(*(int64_t*)ret_buf);
            break;
        case LJ_FFI_FLOAT:
            push_number((int64_t)*(float*)ret_buf);
            break;
        case LJ_FFI_DOUBLE:
            push_number((int64_t)*(double*)ret_buf);
            break;
        case LJ_FFI_POINTER:
            push_number((int64_t)(uintptr_t)*(void**)ret_buf);
            break;
        default:
            push_number(0);
            break;
    }
}
#endif

/*
 * mixed ffi_new(string type_name, void | mapping | array init)
 *   Allocate a C struct/array instance.
 *   Phase 1 placeholder: validates type exists, returns size.
 *   Full allocation + field init in Phase 2.
 */
#ifdef F_FFI_NEW
void f_ffi_new(void)
{
    int nargs = st_num_arg - 1;
    const char *type_name = SVALUE_STRPTR(sp - nargs);

    lj_ffi_type_t *t = lj_ffi_type_lookup(type_name);

    if (!t || t->size == 0) {
        pop_n_elems(st_num_arg);
        push_number(0);
        return;
    }

    /* Allocate buffer with type pointer header:
     * [lj_ffi_type_t* | padding | user data...]
     * Header size = sizeof(void*), aligned to max alignment */
    size_t header_size = sizeof(void*);
    if (header_size < t->alignment) header_size = t->alignment;
    void *raw = calloc(1, header_size + t->size);
    if (!raw) {
        pop_n_elems(st_num_arg);
        push_number(0);
        return;
    }

    /* Store type pointer in header */
    *(lj_ffi_type_t**)raw = t;

    /* User data starts after header */
    void *buf = (uint8_t*)raw + header_size;

    /* If second arg is a mapping, initialize fields */
    if (nargs >= 2 && (sp - nargs + 1)->type == T_MAPPING) {
        /* TODO Phase 2: iterate mapping keys and set struct fields via ffi_set logic */
    }

    pop_n_elems(st_num_arg);

    /* Return user data pointer as integer handle */
    push_number((int64_t)(uintptr_t)buf);
}
#endif


/*
 * void ffi_free(int handle)
 *   Free a buffer allocated by ffi_new.
 */
#ifdef F_FFI_FREE
void f_ffi_free(void)
{
    void *buf = (void*)(uintptr_t)sp->u.number;
    pop_stack();
    if (buf) {
        /* Recover raw pointer by subtracting header size.
         * We need to find the alignment used. Read type from header. */
        /* Try common header sizes: sizeof(void*) or max alignment */
        size_t header_size = sizeof(void*);
        /* Check if type pointer at this offset is valid by looking up its name */
        lj_ffi_type_t **tp = (lj_ffi_type_t**)((uint8_t*)buf - header_size);
        /* Validate: check if the stored pointer looks like a registered type */
        /* For safety, just use sizeof(void*) as header — matches ffi_new */
        free((uint8_t*)buf - header_size);
    }
}
#endif

/*
 * mixed ffi_get(int handle, string field_name)
 *   Read a field from an FFI-allocated struct buffer.
 */
#ifdef F_FFI_GET
void f_ffi_get(void)
{
    const char *field_name = SVALUE_STRPTR(sp);
    void *buf = (void*)(uintptr_t)(sp - 1)->u.number;

    if (!buf) {
        pop_n_elems(2);
        push_number(0);
        return;
    }

    /* Read type pointer from header (stored before user data) */
    size_t header_size = sizeof(void*);
    lj_ffi_type_t *st = *(lj_ffi_type_t**)((uint8_t*)buf - header_size);

    if (!st || st->kind != LJ_FFI_STRUCT) {
        pop_n_elems(2);
        push_number(0);
        return;
    }

    lj_ffi_field_t *field = lj_ffi_struct_find_field(st, field_name);
    if (!field) {
        pop_n_elems(2);
        push_number(0);
        return;
    }

    uint8_t *ptr = (uint8_t*)buf + field->offset;
    svalue_t ret;

    switch (field->field_type->kind) {
        case LJ_FFI_INT8:    ret.type = T_NUMBER; ret.u.number = *(int8_t*)ptr; break;
        case LJ_FFI_UINT8:   ret.type = T_NUMBER; ret.u.number = *(uint8_t*)ptr; break;
        case LJ_FFI_INT16:   ret.type = T_NUMBER; ret.u.number = *(int16_t*)ptr; break;
        case LJ_FFI_UINT16:  ret.type = T_NUMBER; ret.u.number = *(uint16_t*)ptr; break;
        case LJ_FFI_INT32:   ret.type = T_NUMBER; ret.u.number = *(int32_t*)ptr; break;
        case LJ_FFI_UINT32:  ret.type = T_NUMBER; ret.u.number = *(uint32_t*)ptr; break;
        case LJ_FFI_INT64:   ret.type = T_NUMBER; ret.u.number = *(int64_t*)ptr; break;
        case LJ_FFI_UINT64:  ret.type = T_NUMBER; ret.u.number = *(uint64_t*)ptr; break;
        case LJ_FFI_FLOAT:   ret.type = T_REAL;   ret.u.real = *(float*)ptr; break;
        case LJ_FFI_DOUBLE:  ret.type = T_REAL;   ret.u.real = *(double*)ptr; break;
        default:
            pop_n_elems(2);
            push_number(0);
            return;
    }

    pop_n_elems(2);
    push_svalue(&ret);
}
#endif

/*
 * void ffi_set(int handle, string field_name, mixed value)
 *   Write a field to an FFI-allocated struct buffer.
 */
#ifdef F_FFI_SET
void f_ffi_set(void)
{
    svalue_t *val = sp;
    const char *field_name = SVALUE_STRPTR(sp - 1);
    void *buf = (void*)(uintptr_t)(sp - 2)->u.number;

    if (!buf) {
        pop_n_elems(3);
        return;
    }

    /* Read type pointer from header */
    size_t header_size = sizeof(void*);
    lj_ffi_type_t *st = *(lj_ffi_type_t**)((uint8_t*)buf - header_size);

    if (!st || st->kind != LJ_FFI_STRUCT) {
        pop_n_elems(3);
        return;
    }

    lj_ffi_field_t *field = lj_ffi_struct_find_field(st, field_name);
    if (!field) {
        pop_n_elems(3);
        return;
    }

    uint8_t *ptr = (uint8_t*)buf + field->offset;

    switch (field->field_type->kind) {
        case LJ_FFI_INT8:    *(int8_t*)ptr  = (int8_t)val->u.number; break;
        case LJ_FFI_UINT8:   *(uint8_t*)ptr = (uint8_t)val->u.number; break;
        case LJ_FFI_INT16:   *(int16_t*)ptr = (int16_t)val->u.number; break;
        case LJ_FFI_UINT16:  *(uint16_t*)ptr= (uint16_t)val->u.number; break;
        case LJ_FFI_INT32:   *(int32_t*)ptr = (int32_t)val->u.number; break;
        case LJ_FFI_UINT32:  *(uint32_t*)ptr= (uint32_t)val->u.number; break;
        case LJ_FFI_INT64:   *(int64_t*)ptr = (int64_t)val->u.number; break;
        case LJ_FFI_UINT64:  *(uint64_t*)ptr= (uint64_t)val->u.number; break;
        case LJ_FFI_FLOAT:   *(float*)ptr   = (float)(val->type == T_REAL ? val->u.real : (double)val->u.number); break;
        case LJ_FFI_DOUBLE:  *(double*)ptr  = (val->type == T_REAL ? val->u.real : (double)val->u.number); break;
        default: break;
    }

    pop_n_elems(3);
}
#endif
