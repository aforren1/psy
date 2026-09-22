cmake_minimum_required(VERSION 3.20)
project(psy LANGUAGES C CXX)

# Consumers: add_subdirectory(psy) then target_link_libraries(app PRIVATE psy::psy).
# The target carries the include path plus what an IMPLEMENTATION translation
# unit needs to link: pthreads on POSIX (async workers), setupapi on Windows
# (serial-port enumeration). A header needs nothing else but psy_rt.h, which
# the include path above already resolves.
find_package(Threads REQUIRED)
add_library(psy INTERFACE)
add_library(psy::psy ALIAS psy)
target_include_directories(psy INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(psy INTERFACE Threads::Threads $<$<PLATFORM_ID:Windows>:setupapi>)

# Nothing below applies when psy is pulled in as a subproject.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    return()
endif()

option(PSY_BUILD_EXAMPLES "Build the programs in examples/" ON)
option(PSY_BUILD_TESTS    "Build the compile checks in tests/compile/" ON)
# Off by default and never added to ctest: a loopback test needs two device
# names that are wired together, which CI does not have.
option(PSY_BUILD_LOOPBACK "Build the loopback tests in tests/loopback/ (needs hardware)" OFF)

# --- library registry ---------------------------------------------------------
# This list is the single source for what gets built. To add psy_<name>.h:
# append <name>, set its macro prefix, the platforms it supports, and whether
# it has a PSY<X>_NO_THREADS build. Compile checks come from
# tests/compile/psy_<name>.{c,cpp} and examples from examples/<name>_*.c, both
# found by name and gated by platform. The transport headers include psy_rt.h,
# which the include path above resolves from the repository root, so the
# compiler's own header dependencies rebuild every target after a psy_rt.h
# edit. rt comes first so its compile checks run before its dependents'.
set(PSY_LIBS rt parallel serial)

set(PSY_PREFIX_rt          PSYRT)
set(PSY_PLATFORMS_rt       Windows Linux Darwin)
set(PSY_THREADS_rt         ON)

set(PSY_PREFIX_parallel    PSYP)
set(PSY_PLATFORMS_parallel Windows Linux)
set(PSY_THREADS_parallel   ON)

set(PSY_PREFIX_serial      PSYS)
set(PSY_PLATFORMS_serial   Windows Linux Darwin)
set(PSY_THREADS_serial     ON)

if(MSVC)
    set(PSY_WARNINGS /W4 /WX)
else()
    set(PSY_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Werror)
endif()

function(psy_add_program target src)
    add_executable(${target} ${src})
    target_link_libraries(${target} PRIVATE psy::psy)
    target_compile_options(${target} PRIVATE ${PSY_WARNINGS})
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
endfunction()

if(PSY_BUILD_TESTS)
    enable_testing()
endif()

foreach(lib IN LISTS PSY_LIBS)
    if(NOT CMAKE_SYSTEM_NAME IN_LIST PSY_PLATFORMS_${lib})
        message(STATUS "psy_${lib}.h: not supported on ${CMAKE_SYSTEM_NAME}, skipped")
        continue()
    endif()

    # --- compile checks: every header must build clean as C11, as C++17, and
    #     (when it has threading) with it disabled.
    if(PSY_BUILD_TESTS)
        set(variants c cxx)
        psy_add_program(compile_psy_${lib}_c   tests/compile/psy_${lib}.c)
        psy_add_program(compile_psy_${lib}_cxx tests/compile/psy_${lib}.cpp)
        if(PSY_THREADS_${lib})
            psy_add_program(compile_psy_${lib}_nothreads tests/compile/psy_${lib}.c)
            target_compile_definitions(compile_psy_${lib}_nothreads PRIVATE ${PSY_PREFIX_${lib}}_NO_THREADS)
            list(APPEND variants nothreads)
        endif()
        foreach(variant IN LISTS variants)
            add_test(NAME compile_psy_${lib}_${variant} COMMAND compile_psy_${lib}_${variant})
        endforeach()
    endif()

    # --- loopback tests: tests/loopback/psy_<lib>_loopback.c, run by hand with
    #     the device names of a wired pair. Built, never registered with ctest.
    if(PSY_BUILD_LOOPBACK AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/loopback/psy_${lib}_loopback.c)
        psy_add_program(psy_${lib}_loopback tests/loopback/psy_${lib}_loopback.c)
    endif()

    # --- examples: examples/<lib>_*.c, one program each, named after the file.
    if(PSY_BUILD_EXAMPLES)
        file(GLOB lib_examples CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/examples/${lib}_*.c)
        foreach(src IN LISTS lib_examples)
            get_filename_component(name ${src} NAME_WE)
            psy_add_program(${name} ${src})
        endforeach()
    endif()
endforeach()
