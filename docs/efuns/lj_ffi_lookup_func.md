# lj_ffi_lookup_func()
## NAME
**lj_ffi_lookup_func** - look up a registered FFI function by name

## SYNOPSIS
~~~cxx
int lj_ffi_lookup_func( string name );
~~~

## DESCRIPTION
Look up a previously defined or bound FFI function by **name**. Returns a non-negative function index on success, or -1 if not found. The returned index can be used with the FFI call mechanism.

## SEE ALSO
[lj_ffi_bind()](lj_ffi_bind.md), [lj_ffi_cdef()](lj_ffi_cdef.md)
