// test_ffi.c — LJ-FFI Efun Validation
// Called from master.c during preload to verify FFI chain

void test_ffi() {
    int result;
    int sz;

    write("=== LJ-FFI Efun Test ===\n");

    // Test 1: ffi_cdef — parse a simple struct
    result = ffi_cdef(@"
        typedef struct { int x; int y; int z; } vec3_t;
        int abs(int);
    ");
    write("ffi_cdef result: " + result + "\n");

    // Test 2: ffi_sizeof — check struct size
    sz = ffi_sizeof("vec3_t");
    write("ffi_sizeof(vec3_t): " + sz + " (expected 12)\n");

    // Test 3: ffi_sizeof — primitive type
    sz = ffi_sizeof("int");
    write("ffi_sizeof(int): " + sz + " (expected 4)\n");

    // Test 4: ffi_bind — bind libc abs()
    result = ffi_bind("abs", "abs");
    write("ffi_bind(abs, abs): " + result + "\n");

    // Test 5: ffi_call — call bound abs(-42)
    result = ffi_call("abs", -42);
    write("ffi_call(abs, -42): " + result + "\n");

    write("=== LJ-FFI Test Complete ===\n");
}
