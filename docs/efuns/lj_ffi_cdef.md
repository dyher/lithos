# lj_ffi_cdef()
## NAME
**lj_ffi_cdef** - define C types and function signatures

## SYNOPSIS
~~~cxx
int lj_ffi_cdef( string source );
~~~

## DESCRIPTION
Parse a C declaration string and register the described types and function signatures in the FFI type system. Returns 0 on success, non-zero on parse error.

The syntax follows LuaJIT FFI conventions for struct, union, enum, typedef, and function prototype declarations.

## EXAMPLE
~~~cxx
lj_ffi_cdef("typedef struct { float x; float y; float z; } vec3_t;");
lj_ffi_cdef("int my_func(int a, const char *b);");
~~~

## SEE ALSO
[lj_ffi_init()](lj_ffi_init.md), [lj_ffi_bind()](lj_ffi_bind.md), [lj_ffi_lookup_func()](lj_ffi_lookup_func.md)
