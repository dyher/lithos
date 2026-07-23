# lj_ffi_bind()
## NAME
**lj_ffi_bind** - bind a C function pointer to an FFI function name

## SYNOPSIS
~~~cxx
int lj_ffi_bind( string name, mixed ptr );
~~~

## DESCRIPTION
Bind a C function pointer **ptr** to the given **name** in the FFI function registry. The function signature must have been previously declared via `lj_ffi_cdef()`. Returns 0 on success, non-zero on failure.

This enables dynamic binding of shared library functions at runtime without recompiling the driver.

## EXAMPLE
~~~cxx
lj_ffi_cdef("int sqlite3_open(const char *filename, void **db);");
// After loading libsqlite3 via dlopen:
lj_ffi_bind("sqlite3_open", func_ptr);
~~~

## SEE ALSO
[lj_ffi_cdef()](lj_ffi_cdef.md), [lj_ffi_lookup_func()](lj_ffi_lookup_func.md), [lj_ffi_init()](lj_ffi_init.md)
