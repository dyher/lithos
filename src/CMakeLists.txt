# src/CMakeLists.txt
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFI REQUIRED libffi)


# The "stem" target is the collection of object files (CMake object library)
# that includes everything we need to build the LPMud driver. It can be linked
# to an executable entry function (main.c) to build the final neolith server binary.
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
add_executable(neolith main.c ${neolith_SOURCES})
# Create interface target for FFI so all stem consumers get it
add_library(ffi_interface INTERFACE)
target_link_libraries(ffi_interface INTERFACE ${FFI_LIBRARIES})
target_include_directories(ffi_interface INTERFACE ${FFI_INCLUDE_DIRS})

target_link_libraries(neolith PRIVATE stem ffi_interface)
