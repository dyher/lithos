# lj_ffi_init()
## NAME
**lj_ffi_init** - initialize the LuaJIT-style FFI type system

## SYNOPSIS
~~~cxx
void lj_ffi_init();
~~~

## DESCRIPTION
Initialize the LuaJIT-compatible FFI subsystem, including the primitive type registry (18 built-in types) and the function binding registry. This is called automatically during driver startup and does not need to be invoked manually.

The type system supports: `void`, `bool`, `int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32`, `int64`, `uint64`, `float`, `double`, `size_t`, `ssize_t`, `intptr_t`, `uintptr_t`, `char`, `short`.

## SEE ALSO
[lj_ffi_cdef()](lj_ffi_cdef.md), [lj_ffi_bind()](lj_ffi_bind.md), [lj_ffi_type_sizeof()](lj_ffi_type_sizeof.md)
