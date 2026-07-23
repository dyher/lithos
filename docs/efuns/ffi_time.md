# ffi_time()
## NAME
**ffi_time** - get current Unix timestamp via FFI

## SYNOPSIS
~~~cxx
int ffi_time();
~~~

## DESCRIPTION
Return the current Unix epoch time in seconds. This is a built-in FFI function bound directly to the C `time()` function.

## SEE ALSO
[ffi_getpid()](ffi_getpid.md), [time()](time.md)
