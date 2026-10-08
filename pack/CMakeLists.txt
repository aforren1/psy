# The pack tool's text layout (docs/layout.md): the glue library, its
# test, and examples/gfx_layout.c. Added by the top-level CMakeLists.txt when
# YSP_BUILD_LAYOUT is ON, after third_party/.

add_library(ysp_layout STATIC layout.c ysp/layout.h)
target_include_directories(ysp_layout PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ysp_layout PUBLIC ysp::rig ysp_skribidi)
target_compile_options(ysp_layout PRIVATE ${YSP_WARNINGS})
set_target_properties(ysp_layout PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_definitions(ysp_layout PRIVATE YLAY_PATCHES="${YSP_SKRIBIDI_PATCHES}")
if(UNIX)
    target_link_libraries(ysp_layout PUBLIC m)
endif()

if(YSP_BUILD_TESTS)
    # ysp/gfx.h is compiled without SDL for ygfx_cset_check(), as in
    # tests/adapt/outline_test.c. Without the corpus fonts it says that
    # it skipped them and still runs its order checks on a font it writes.
    ysp_add_program(test_layout ${PROJECT_SOURCE_DIR}/tests/layout/layout_test.c rig)
    target_link_libraries(test_layout PRIVATE ysp_layout)
    set(YSP_LAYOUT_TEST_FONTS "C:/Windows/Fonts" CACHE PATH "Font folder of the corpus check (YSP_FONTS_DIR overrides it at run time)")
    target_compile_definitions(test_layout PRIVATE YSCR_NO_SDL
        YLAY_TEST_DIR="${PROJECT_SOURCE_DIR}/tests/layout" YLAY_TEST_FONTS="${YSP_LAYOUT_TEST_FONTS}")
    add_test(NAME test_layout COMMAND test_layout)
    # The costs of docs/layout.md; not a test (run it under the lock).
    ysp_add_program(layout_bench ${PROJECT_SOURCE_DIR}/tests/layout/layout_bench.c rig)
    target_link_libraries(layout_bench PRIVATE ysp_layout)
endif()

if(YSP_BUILD_EXAMPLES AND TARGET SDL3::SDL3)
    ysp_add_program(gfx_layout ${PROJECT_SOURCE_DIR}/examples/gfx_layout.c rig)
    target_link_libraries(gfx_layout PRIVATE ysp_layout)
endif()
