# src/CMakeLists.txt
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFI REQUIRED libffi)


# The "stem" target is the collection of object files (CMake object library)
# that includes everything we need to build the LPMud driver. It can be linked
# to an executable entry function (main.c)
# to an executable entry function (main.c)
#
# Alternatively, the "stem" object files can be linked to other frontends,
# such as a unit-test executable or a custom command line interface that functions
# as a LPC opcode compiler or other tools.

set(stem_SOURCES
    addr_resolver.cpp
    apply.cpp
    backend.c
    call_out.cpp
    comm.c
    command.c
    ed.c
    error_context.cpp
    frame.cpp
    interpret.c
    ffi_efun.cpp
    spatial_efun.c
    aoi_efun.c
    lj_ffi/lj_ffi_type.cpp
    lj_ffi/lj_ffi_cdef.cpp
    lj_ffi/lj_ffi_call.cpp
    dxalloc.cpp
    outbuf.c
    simul_efun.c
    simulate.c
    stack.c
    stem.cpp
    stralloc.c
)
add_library(stem OBJECT ${stem_SOURCES})
target_link_libraries(stem PUBLIC port logger misc lpc efuns rc socket curl async neolith_spatial)
if(TARGET c-ares::cares)
	target_compile_definitions(stem PRIVATE CARES_NO_DEPRECATED)
    target_link_libraries(stem PUBLIC c-ares::cares)
endif()
if(HAVE_LIBM)
    target_link_libraries(stem PUBLIC m)
endif()
target_include_directories(stem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}) # for unit-tests

# ================================================================
# build neolith executable
add_executable(neolith main.c
    jit.c
    jit_templates.c
    jit_trace.c
    jit_trace_compile_aarch64.c

    ${CMAKE_SOURCE_DIR}/tests/test_fiber_stress.c ${neolith_SOURCES})
# Create interface target for FFI so all stem consumers get it
add_library(ffi_interface INTERFACE)
target_link_libraries(ffi_interface INTERFACE ${FFI_LIBRARIES})
target_include_directories(ffi_interface INTERFACE ${FFI_INCLUDE_DIRS})

target_link_libraries(neolith PRIVATE stem ffi_interface lpc fiber_port)

# Platform-specific JIT emitter (only compile for current architecture)
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    target_sources(neolith PRIVATE jit_emit_aarch64.c)
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
    target_sources(neolith PRIVATE jit_emit_x86_64.c)
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv64")
    target_sources(neolith PRIVATE jit_emit_riscv64.c)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm" AND NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    target_sources(neolith PRIVATE jit_emit_arm32.c)
endif()
