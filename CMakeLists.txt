cmake_minimum_required(VERSION 3.20)
project(psy LANGUAGES C CXX)

# Consumers: add_subdirectory(psy) then target_link_libraries(app PRIVATE psy::psy).
# The target carries the include path plus what an IMPLEMENTATION translation
# unit needs to link: pthreads on POSIX (async workers), setupapi on Windows
# (serial-port enumeration), libm on Linux (the adaptive-method headers; a
# C driver does not get it for free there). A header needs nothing else but
# psy_rt.h, which the include path above already resolves.
find_package(Threads REQUIRED)
add_library(psy INTERFACE)
add_library(psy::psy ALIAS psy)
target_include_directories(psy INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(psy INTERFACE Threads::Threads
    $<$<PLATFORM_ID:Windows>:setupapi>
    $<$<PLATFORM_ID:Linux>:m>)

# Nothing below applies when psy is pulled in as a subproject.
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    return()
endif()

option(PSY_BUILD_EXAMPLES "Build the programs in examples/" ON)
option(PSY_BUILD_TESTS    "Build the compile checks in tests/compile/" ON)
# Off by default and never added to ctest: a loopback test needs two device
# names that are wired together, which CI does not have.
option(PSY_BUILD_LOOPBACK "Build the loopback tests in tests/loopback/ (needs hardware)" OFF)
# Off by default because it downloads Tracy: builds psy_rt.h's compile check
# with PSYRT_TRACY against the real TracyC.h, so the macro mapping is checked
# against Tracy itself and not against a copy of it.
option(PSY_TRACY_CHECK "Fetch Tracy and build the PSYRT_TRACY compile check" OFF)

# psy_screen.h needs SDL3 (3.2 or later). Without it only its SDL-free test is
# built; point SDL3_DIR at an SDL3 CMake package to build the rest. ANGLE is
# loaded at run time, so the build never needs it; PSY_ANGLE_DIR only copies
# libEGL.dll and libGLESv2.dll beside the programs that open a window.
find_package(SDL3 3.2 CONFIG QUIET)
set(PSY_ANGLE_DIR "" CACHE PATH "ANGLE's libEGL.dll and libGLESv2.dll, copied beside the psy_screen.h programs")

# psy_audio.h needs miniaudio.h (0.11.25, one file). PSY_MINIAUDIO_DIR names a
# directory that holds it; else PSY_FETCH_MINIAUDIO downloads the pinned file,
# checked by its hash. Off by default because it downloads, as
# PSY_TRACY_CHECK is; CI turns it on. Without either, only psy_audio.h's
# miniaudio-free test is built.
set(PSY_MINIAUDIO_DIR "" CACHE PATH "Directory with miniaudio.h for psy_audio.h")
option(PSY_FETCH_MINIAUDIO "Download miniaudio.h 0.11.25 for psy_audio.h" OFF)
if(NOT PSY_MINIAUDIO_DIR AND PSY_FETCH_MINIAUDIO)
    include(FetchContent)
    FetchContent_Declare(miniaudio
        URL      https://raw.githubusercontent.com/mackron/miniaudio/0.11.25/miniaudio.h
        URL_HASH SHA256=ac7af4de748b7e26b777f37e01cee313a308a7296a3eb080e2906b320cc55c89
        DOWNLOAD_NO_EXTRACT TRUE)
    FetchContent_MakeAvailable(miniaudio)
    set(PSY_MINIAUDIO_DIR ${miniaudio_SOURCE_DIR})
endif()
if(PSY_MINIAUDIO_DIR AND EXISTS ${PSY_MINIAUDIO_DIR}/miniaudio.h)
    add_library(psy_miniaudio INTERFACE)
    add_library(psy::miniaudio ALIAS psy_miniaudio)
    target_include_directories(psy_miniaudio INTERFACE ${PSY_MINIAUDIO_DIR})
    target_link_libraries(psy_miniaudio INTERFACE ${CMAKE_DL_LIBS})
else()
    message(STATUS "psy_audio.h: miniaudio.h not found; set PSY_MINIAUDIO_DIR or PSY_FETCH_MINIAUDIO=ON")
endif()

# psy_video.h decodes MPEG-1 with pl_mpeg.h (one MIT file). It has no release
# tags, so the pin is a commit and the file's SHA-256. PSY_PL_MPEG_DIR names a
# directory that holds it; else PSY_FETCH_PL_MPEG downloads it, and qoi.h,
# which psy_video.h's test compares its own QOI encoder with. Off by default
# because it downloads; CI turns it on. Without it psy_video.h builds with
# PSYVID_NO_PL_MPEG: frame sequences only.
set(PSY_PL_MPEG_DIR "" CACHE PATH "Directory with pl_mpeg.h for psy_video.h")
option(PSY_FETCH_PL_MPEG "Download pl_mpeg.h (c871f2b) and qoi.h (ffb2d2c) for psy_video.h" OFF)
if(NOT PSY_PL_MPEG_DIR AND PSY_FETCH_PL_MPEG)
    include(FetchContent)
    FetchContent_Declare(pl_mpeg
        URL      https://raw.githubusercontent.com/phoboslab/pl_mpeg/c871f2be022ece7ef4f64230b4fb8e1fb9eb6023/pl_mpeg.h
        URL_HASH SHA256=3a8cb30c83c2a1147719c30fe0c8b93da2987aa43140077c575b39aaa75fc2c9
        DOWNLOAD_NO_EXTRACT TRUE)
    FetchContent_Declare(qoi
        URL      https://raw.githubusercontent.com/phoboslab/qoi/ffb2d2cb74a1de60819b21b939f7209aa53e91c1/qoi.h
        URL_HASH SHA256=7de6fca1a285b1c20d38f2723dec8b774eb9f144edb9710800a95feeea09375a
        DOWNLOAD_NO_EXTRACT TRUE)
    FetchContent_MakeAvailable(pl_mpeg qoi)
    set(PSY_PL_MPEG_DIR ${pl_mpeg_SOURCE_DIR})
    set(PSY_QOI_DIR ${qoi_SOURCE_DIR})
endif()
if(PSY_PL_MPEG_DIR AND EXISTS ${PSY_PL_MPEG_DIR}/pl_mpeg.h)
    add_library(psy_pl_mpeg INTERFACE)
    add_library(psy::pl_mpeg ALIAS psy_pl_mpeg)
    target_include_directories(psy_pl_mpeg INTERFACE ${PSY_PL_MPEG_DIR})
else()
    message(STATUS "psy_video.h: pl_mpeg.h not found, so no MPEG-1; set PSY_PL_MPEG_DIR or PSY_FETCH_PL_MPEG=ON")
endif()

# --- library registry ---------------------------------------------------------
# This list is the single source for what gets built. To add psy_<name>.h:
# append <name>, set its macro prefix, the platforms it supports, and whether
# it has a PSY<X>_NO_THREADS build. Compile checks come from
# tests/compile/psy_<name>.{c,cpp} and examples from examples/<name>_*.c, both
# found by name and gated by platform. The transport headers include psy_rt.h,
# which the include path above resolves from the repository root, so the
# compiler's own header dependencies rebuild every target after a psy_rt.h
# edit. rt comes first so its compile checks run before its dependents'.
# The list has four groups: the transports (rt, parallel, serial), the
# adaptive methods (stair, quest, gp), the trial sequencer (trials), the
# timeline (timeline) and color (color), then the rig headers.
# A header with a tests/adapt/psy_<name>_test.c gets that self-checking
# program registered with ctest too, and one with PSY_ASYNC_<name> ON gets
# the compile check and that test built a second time with
# PSY<X>_ASYNC defined, which is the opt-in layer that pulls in psy_rt.h.
set(PSY_LIBS rt parallel serial stair quest gp trials timeline color screen audio gfx video)

set(PSY_PREFIX_rt          PSYRT)
set(PSY_PLATFORMS_rt       Windows Linux Darwin)
set(PSY_THREADS_rt         ON)

set(PSY_PREFIX_parallel    PSYP)
set(PSY_PLATFORMS_parallel Windows Linux)
set(PSY_THREADS_parallel   ON)

set(PSY_PREFIX_serial      PSYS)
set(PSY_PLATFORMS_serial   Windows Linux Darwin)
set(PSY_THREADS_serial     ON)

# The adaptive-method headers are pure computation: every platform, no
# worker thread, so no NO_THREADS variant. quest and gp have an opt-in async
# layer on psy_rt.h's pump, which PSY_ASYNC_<name> builds as a second variant.
set(PSY_PREFIX_stair       PSYST)
set(PSY_PLATFORMS_stair    Windows Linux Darwin)
set(PSY_THREADS_stair      OFF)

set(PSY_PREFIX_quest       PSYQ)
set(PSY_PLATFORMS_quest    Windows Linux Darwin)
set(PSY_THREADS_quest      OFF)
set(PSY_ASYNC_quest        ON)

set(PSY_PREFIX_gp          PSYGP)
set(PSY_PLATFORMS_gp       Windows Linux Darwin)
set(PSY_THREADS_gp         OFF)
set(PSY_ASYNC_gp           ON)

# The trial sequencer includes nothing at all, not even psy_rt.h.
set(PSY_PREFIX_trials      PSYTR)
set(PSY_PLATFORMS_trials   Windows Linux Darwin)
set(PSY_THREADS_trials     OFF)

# The timeline includes nothing either; it takes times as arguments.
set(PSY_PREFIX_timeline    PSYTL)
set(PSY_PLATFORMS_timeline Windows Linux Darwin)
set(PSY_THREADS_timeline   OFF)

# The color header is pure computation too: calibration, conversions, gamut.
set(PSY_PREFIX_color       PSYCOL)
set(PSY_PLATFORMS_color    Windows Linux Darwin)
set(PSY_THREADS_color      OFF)

# The display header is the one with an external dependency. PSY_DEPS_<name>
# lists CMake targets it links; when one is missing, only its tests/adapt test
# (which must need none of them) is built.
set(PSY_PREFIX_screen      PSYSCR)
set(PSY_PLATFORMS_screen   Windows Linux Darwin)
set(PSY_THREADS_screen     OFF)
set(PSY_DEPS_screen        SDL3::SDL3)

# The audio header's one dependency is miniaudio.h, a header (see above).
set(PSY_PREFIX_audio       PSYAU)
set(PSY_PLATFORMS_audio    Windows Linux Darwin)
set(PSY_THREADS_audio      OFF)
set(PSY_DEPS_audio         psy::miniaudio)

# The stimulus header sits on psy_screen.h, so it needs what that needs. Its
# test builds without SDL and finds a GL ES 3.0 renderer at run time.
set(PSY_PREFIX_gfx         PSYGFX)
set(PSY_PLATFORMS_gfx      Windows Linux Darwin)
set(PSY_THREADS_gfx        OFF)
set(PSY_DEPS_gfx           SDL3::SDL3)

# The video header sits on psy_gfx.h and psy_timeline.h; pl_mpeg is optional
# (see above and the block after the loop). Its test needs neither SDL3 nor
# pl_mpeg.
set(PSY_PREFIX_video       PSYVID)
set(PSY_PLATFORMS_video    Windows Linux Darwin)
set(PSY_THREADS_video      OFF)
set(PSY_DEPS_video         SDL3::SDL3)

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
    # PSY_CURRENT_DEPS is set by the loop below for the header being built.
    if(PSY_CURRENT_DEPS)
        target_link_libraries(${target} PRIVATE ${PSY_CURRENT_DEPS})
        if(WIN32 AND TARGET SDL3::SDL3-shared)
            add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:${target}>)
        endif()
        if(WIN32 AND PSY_ANGLE_DIR)
            add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${PSY_ANGLE_DIR}/libEGL.dll ${PSY_ANGLE_DIR}/libGLESv2.dll $<TARGET_FILE_DIR:${target}>)
        endif()
    endif()
endfunction()

if(PSY_BUILD_TESTS)
    enable_testing()
endif()

foreach(lib IN LISTS PSY_LIBS)
    if(NOT CMAKE_SYSTEM_NAME IN_LIST PSY_PLATFORMS_${lib})
        message(STATUS "psy_${lib}.h: not supported on ${CMAKE_SYSTEM_NAME}, skipped")
        continue()
    endif()
    set(deps_ok TRUE)
    foreach(dep IN LISTS PSY_DEPS_${lib})
        if(NOT TARGET ${dep})
            set(deps_ok FALSE)
        endif()
    endforeach()
    if(NOT deps_ok)
        message(STATUS "psy_${lib}.h: ${PSY_DEPS_${lib}} not found; only its tests/adapt test is built")
    endif()
    set(PSY_CURRENT_DEPS ${PSY_DEPS_${lib}})

    # --- compile checks: every header must build clean as C11, as C++17, and
    #     (when it has threading) with it disabled.
    if(PSY_BUILD_TESTS AND deps_ok)
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
        # Extra checks named tests/compile/psy_<lib>_<what>.cpp (psy_screen_com.cpp
        # checks the COM declarations against the Windows SDK).
        file(GLOB lib_extra_checks CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/compile/psy_${lib}_*.cpp)
        foreach(src IN LISTS lib_extra_checks)
            get_filename_component(name ${src} NAME_WE)
            psy_add_program(compile_${name} ${src})
            add_test(NAME compile_${name} COMMAND compile_${name})
        endforeach()
        # psy_timeline.h's op table macros are designated initializers,
        # which C++ has from C++20 on; g++ warns about every field they
        # leave out, though zero is the default.
        if(TARGET compile_psy_timeline_ops)
            set_target_properties(compile_psy_timeline_ops PROPERTIES CXX_STANDARD 20)
            if(NOT MSVC)
                target_compile_options(compile_psy_timeline_ops PRIVATE -Wno-missing-field-initializers)
            endif()
        endif()
        # psy_color.h's value macros (PSYCOL_DKL(...)) are the same kind.
        if(TARGET compile_psy_color_macros)
            set_target_properties(compile_psy_color_macros PROPERTIES CXX_STANDARD 20)
            if(NOT MSVC)
                target_compile_options(compile_psy_color_macros PRIVATE -Wno-missing-field-initializers)
            endif()
        endif()
    endif()

    # --- verification tests: tests/adapt/psy_<lib>_test.c is self-checking
    #     (exit 0 on pass) and needs no hardware, so ctest runs it, and the
    #     sanitizer job in CI runs it instrumented.
    if(PSY_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/adapt/psy_${lib}_test.c)
        set(PSY_CURRENT_DEPS "")
        psy_add_program(test_psy_${lib} tests/adapt/psy_${lib}_test.c)
        set(PSY_CURRENT_DEPS ${PSY_DEPS_${lib}})
        add_test(NAME test_psy_${lib} COMMAND test_psy_${lib})
        if(PSY_ASYNC_${lib})
            psy_add_program(compile_psy_${lib}_async tests/compile/psy_${lib}.c)
            target_compile_definitions(compile_psy_${lib}_async PRIVATE ${PSY_PREFIX_${lib}}_ASYNC)
            add_test(NAME compile_psy_${lib}_async COMMAND compile_psy_${lib}_async)
            psy_add_program(test_psy_${lib}_async tests/adapt/psy_${lib}_test.c)
            target_compile_definitions(test_psy_${lib}_async PRIVATE ${PSY_PREFIX_${lib}}_ASYNC)
            add_test(NAME test_psy_${lib}_async COMMAND test_psy_${lib}_async)
        endif()
    endif()

    # --- loopback tests: tests/loopback/psy_<lib>_loopback.c, run by hand with
    #     the device names of a wired pair. Built, never registered with ctest.
    if(PSY_BUILD_LOOPBACK AND deps_ok AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/loopback/psy_${lib}_loopback.c)
        psy_add_program(psy_${lib}_loopback tests/loopback/psy_${lib}_loopback.c)
    endif()

    # --- examples: examples/<lib>_*.c, one program each, named after the file.
    if(PSY_BUILD_EXAMPLES AND deps_ok)
        file(GLOB lib_examples CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/examples/${lib}_*.c)
        foreach(src IN LISTS lib_examples)
            get_filename_component(name ${src} NAME_WE)
            psy_add_program(${name} ${src})
        endforeach()
    endif()
endforeach()

# The audio loopback test opens WASAPI's loopback capture itself.
if(TARGET psy_audio_loopback AND WIN32)
    target_link_libraries(psy_audio_loopback PRIVATE ole32)
endif()

# psy_video.h: pl_mpeg when it was found, else the build without MPEG-1. The
# test has its own scripted decoder; it compares QOI bytes with qoi.h when
# that was fetched.
foreach(t compile_psy_video_c compile_psy_video_cxx video_play video_bench)
    if(TARGET ${t})
        if(TARGET psy::pl_mpeg)
            target_link_libraries(${t} PRIVATE psy::pl_mpeg)
        else()
            target_compile_definitions(${t} PRIVATE PSYVID_NO_PL_MPEG)
        endif()
    endif()
endforeach()
if(TARGET test_psy_video AND PSY_QOI_DIR)
    target_include_directories(test_psy_video PRIVATE ${PSY_QOI_DIR})
    target_compile_definitions(test_psy_video PRIVATE PSYVID_TEST_QOI_REF)
endif()

# psy_gfx.h's headless presenter loads libEGL with dlopen on POSIX.
foreach(t test_psy_gfx gfx_bench)
    if(TARGET ${t})
        target_link_libraries(${t} PRIVATE ${CMAKE_DL_LIBS})
    endif()
endforeach()

if(PSY_TRACY_CHECK AND PSY_BUILD_TESTS)
    include(FetchContent)
    # The tag psy_rt.h's STATUS names. On demand and silent: the check runs
    # on CI, where nothing connects and nothing should broadcast.
    set(TRACY_ENABLE       ON CACHE BOOL "" FORCE)   # Tracy's own default is OFF
    set(TRACY_ON_DEMAND    ON CACHE BOOL "" FORCE)
    set(TRACY_NO_BROADCAST ON CACHE BOOL "" FORCE)
    FetchContent_Declare(tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG        v0.14.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(tracy)
    set(PSY_CURRENT_DEPS "")   # the loop above leaves the last header's deps here
    psy_add_program(compile_psy_rt_tracy tests/compile/psy_rt.c)
    target_compile_definitions(compile_psy_rt_tracy PRIVATE PSYRT_TRACY)
    target_link_libraries(compile_psy_rt_tracy PRIVATE Tracy::TracyClient)
    add_test(NAME compile_psy_rt_tracy COMMAND compile_psy_rt_tracy)
endif()
