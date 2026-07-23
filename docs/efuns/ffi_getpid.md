# ffi_getpid()
## NAME
**ffi_getpid** - get the current process ID via FFI

## SYNOPSIS
~~~cxx
int ffi_getpid();
~~~

## DESCRIPTION
Return the current process ID. This is a built-in FFI function demonstrating zero-argument C function binding. Equivalent to calling `getpid()` in C.

## SEE ALSO
[ffi_time()](ffi_time.md), [ffi_abs()](ffi_abs.md), [lj_ffi_init()](lj_ffi_init.md)
