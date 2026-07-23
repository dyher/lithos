# ffi_abs()
## NAME
**ffi_abs** - compute absolute value via FFI

## SYNOPSIS
~~~cxx
int ffi_abs( int value );
~~~

## DESCRIPTION
Return the absolute value of **value**. This is a built-in FFI function bound to the C `abs()` function, demonstrating single-argument C function binding.

## SEE ALSO
[ffi_getpid()](ffi_getpid.md), [lj_ffi_bind()](lj_ffi_bind.md)
