/*
 * ffi_efun.c - LuaJIT-style FFI for Neolith LPC VM
 */

#include <stdbool.h>
#ifndef HAVE_STDBOOL_H
#define HAVE_STDBOOL_H
#endif
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
  #include <process.h>
  #include <windows.h>
  #define FFI_GETPID _getpid
#else
  #include <unistd.h>
  #include <dlfcn.h>
  #define FFI_GETPID getpid
#endif
#include <time.h>
#include <ffi.h>

#include "lpc/types.h"
#include "lpc/svalue.h"
#include "lj_ffi/lj_ffi.h"
#include "lj_ffi/lj_ffi_cdef.h"

/* Forward declarations - C linkage to match Neolith core */
extern "C" {
    extern void error(const char *, ...);
    extern void debug_message(const char *, ...);
    extern void push_number(long);
    extern svalue_t *sp;
}

/* ---- FFI function registry ---- */

typedef struct {
    const char *name;
    void       *func_ptr;
    ffi_cif     cif;
    ffi_type   *arg_types[8];
    int         nargs;
    ffi_type   *ret_type;
} ffi_entry_t;

#define MAX_FFI_ENTRIES 64
static ffi_entry_t ffi_table[MAX_FFI_ENTRIES];
static int num_ffi_entries = 0;

static ffi_type *resolve_ffi_type(const char *type_name)
{
    if (strcmp(type_name, "int") == 0)       return &ffi_type_sint;
    if (strcmp(type_name, "long") == 0)      return &ffi_type_slong;
    if (strcmp(type_name, "void") == 0)      return &ffi_type_void;
    if (strcmp(type_name, "double") == 0)    return &ffi_type_double;
    if (strcmp(type_name, "pointer") == 0)   return &ffi_type_pointer;
    if (strcmp(type_name, "size_t") == 0)    return &ffi_type_uint;
    return &ffi_type_sint;
}

extern "C" int register_ffi_function(const char *name, void *func_ptr,
                          const char *ret_type,
                          const char **arg_type_names, int nargs)
{
    int i;
    ffi_status status;
    ffi_entry_t *e;

    if (num_ffi_entries >= MAX_FFI_ENTRIES) return -1;

    e = &ffi_table[num_ffi_entries];
    e->name = name;
    e->func_ptr = func_ptr;
    e->nargs = nargs;
    e->ret_type = resolve_ffi_type(ret_type);

    for (i = 0; i < nargs && i < 8; i++) {
        e->arg_types[i] = resolve_ffi_type(arg_type_names[i]);
    }

    status = ffi_prep_cif(&e->cif, FFI_DEFAULT_ABI,
                          nargs, e->ret_type, e->arg_types);
    if (status != FFI_OK) {
        fprintf(stderr, "FFI: ffi_prep_cif failed for %s (status=%d)\n", name, (int)status);
        return -1;
    }

    num_ffi_entries++;
    fprintf(stderr, "FFI: registered '%s' (idx=%d, nargs=%d)\n",
            name, num_ffi_entries - 1, nargs);
    return num_ffi_entries - 1;
}

extern "C" void call_ffi_efun(int ffi_idx, int nargs)
{
    int i;
    ffi_entry_t *e;
    void *arg_values[8];
    long arg_storage[8];
    long ret_val;

    if (ffi_idx < 0 || ffi_idx >= num_ffi_entries) {
        error("*FFI: invalid function index %d\n", ffi_idx);
        return;
    }

    e = &ffi_table[ffi_idx];

    if (nargs != e->nargs) {
        error("*FFI: '%s' expects %d args, got %d\n", e->name, e->nargs, nargs);
        return;
    }

    for (i = 0; i < nargs; i++) {
        svalue_t *sv = sp - nargs + 1 + i;
        if (sv->type == T_NUMBER) {
            arg_storage[i] = sv->u.number;
            arg_values[i] = &arg_storage[i];
        } else if (sv->type == T_STRING) {
            arg_values[i] = (void *)SVALUE_STRPTR(sv);
        } else {
            arg_storage[i] = 0;
            arg_values[i] = &arg_storage[i];
        }
    }

    ret_val = 0;
    ffi_call(&e->cif, FFI_FN(e->func_ptr), &ret_val, arg_values);

    sp -= nargs;

    if (e->ret_type == &ffi_type_void) {
        push_number(0);
    } else {
        push_number((long)ret_val);
    }
}

/* Forward declarations for LJ-FFI bridge functions */
extern "C" void bridge_lj_ffi_init(void);
extern "C" int bridge_lj_ffi_cdef(const char *source);
extern "C" int bridge_lj_ffi_lookup_func(const char *name);
extern "C" int bridge_lj_ffi_bind(const char *name, void *ptr);
extern "C" int bridge_lj_ffi_type_sizeof(const char *name);

extern "C" void init_ffi_efuns(void)
{
    static const char *abs_args[] = {"int"};

    register_ffi_function("ffi_getpid", (void *)(uintptr_t)FFI_GETPID, "int", NULL, 0);
    register_ffi_function("ffi_time", (void *)(uintptr_t)time, "long", NULL, 0);
    register_ffi_function("ffi_abs", (void *)(uintptr_t)(int(*)(int))abs, "int", abs_args, 1);

    /* Register LJ-FFI bridge functions */
    register_ffi_function("lj_ffi_init", (void *)(uintptr_t)bridge_lj_ffi_init, "void", NULL, 0);

    static const char *cdef_args[] = {"string"};
    register_ffi_function("lj_ffi_cdef", (void *)(uintptr_t)bridge_lj_ffi_cdef, "int", cdef_args, 1);

    static const char *lookup_args[] = {"string"};
    register_ffi_function("lj_ffi_lookup_func", (void *)(uintptr_t)bridge_lj_ffi_lookup_func, "int", lookup_args, 1);

    static const char *bind_args[] = {"string", "pointer"};
    register_ffi_function("lj_ffi_bind", (void *)(uintptr_t)bridge_lj_ffi_bind, "int", bind_args, 2);

    static const char *sizeof_args[] = {"string"};
    register_ffi_function("lj_ffi_type_sizeof", (void *)(uintptr_t)bridge_lj_ffi_type_sizeof, "int", sizeof_args, 1);

    /* Auto-initialize LJ-FFI type system at startup */
    lj_ffi_type_init();
    lj_ffi_func_init();

    fprintf(stderr, "FFI: initialized %d built-in functions (+5 LJ-FFI bridges)\n", num_ffi_entries);
}

extern "C" int find_ffi_function(const char *name)
{
    int i;
    for (i = 0; i < num_ffi_entries; i++) {
        if (strcmp(ffi_table[i].name, name) == 0) return i;
    }
    return -1;
}

/* ============================================================
 * LJ-FFI Bridge — Exposes dynamic FFI to LPC via static FFI slots
 * ============================================================ */

/* Bridge slot 0: lj_ffi_init() — initialize type system + func registry */
extern "C" void bridge_lj_ffi_init(void)
{
    lj_ffi_type_init();
    lj_ffi_func_init();
}

/* Bridge slot 1: lj_ffi_cdef(const char *source) — parse cdef string */
extern "C" int bridge_lj_ffi_cdef(const char *source)
{
    return lj_ffi_cdef_parse(source);
}

/* Bridge slot 2: lj_ffi_lookup_func(const char *name) — check if func exists */
extern "C" int bridge_lj_ffi_lookup_func(const char *name)
{
    lj_ffi_func_t *f = lj_ffi_func_lookup(name);
    return f ? 1 : 0;
}

/* Bridge slot 3: lj_ffi_bind(const char *name, void *ptr) — bind func pointer */
extern "C" int bridge_lj_ffi_bind(const char *name, void *ptr)
{
    return lj_ffi_func_bind(name, ptr);
}

/* Bridge slot 4: lj_ffi_type_sizeof(const char *name) — get type size */
extern "C" int bridge_lj_ffi_type_sizeof(const char *name)
{
    lj_ffi_type_t *t = lj_ffi_type_lookup(name);
    return t ? (int)t->size : -1;
}
