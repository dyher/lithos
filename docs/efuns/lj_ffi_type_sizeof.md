# lj_ffi_type_sizeof()
## NAME
**lj_ffi_type_sizeof** - get the size of an FFI type

## SYNOPSIS
~~~cxx
int lj_ffi_type_sizeof( string type_name );
~~~

## DESCRIPTION
Return the size in bytes of the FFI type identified by **type_name**. The type must be a registered primitive or previously defined via `lj_ffi_cdef()`. Returns -1 if the type is unknown.

## EXAMPLE
~~~cxx
write("sizeof(float) = " + lj_ffi_type_sizeof("float") + "\n");   // 4
write("sizeof(vec3_t) = " + lj_ffi_type_sizeof("vec3_t") + "\n"); // 12
~~~

## SEE ALSO
[lj_ffi_cdef()](lj_ffi_cdef.md), [lj_ffi_init()](lj_ffi_init.md)
