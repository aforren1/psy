# The pack tool's text layout (docs/psy_layout.md): the glue library, its
# test, and examples/gfx_layout.c. Added by the top-level CMakeLists.txt when
# PSY_BUILD_LAYOUT is ON, after third_party/.

add_library(psy_layout STATIC psy_layout.c psy_layout.h)
target_include_directories(psy_layout PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(psy_layout PUBLIC psy::psy psy_skribidi)
target_compile_options(psy_layout PRIVATE ${PSY_WARNINGS})
set_target_properties(psy_layout PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_definitions(psy_layout PRIVATE PSYLAY_PATCHES="${PSY_SKRIBIDI_PATCHES}")
if(UNIX)
    target_link_libraries(psy_layout PUBLIC m)
endif()

if(PSY_BUILD_TESTS)
    # psy_gfx.h is compiled without SDL for psygfx_cset_check(), as in
    # tests/adapt/psy_outline_test.c. Without the corpus fonts it says that
    # it skipped them and still runs its order checks on a font it writes.
    set(PSY_CURRENT_DEPS "")
    psy_add_program(test_psy_layout ${PROJECT_SOURCE_DIR}/tests/layout/psy_layout_test.c)
    target_link_libraries(test_psy_layout PRIVATE psy_layout ${CMAKE_DL_LIBS})
    set(PSY_LAYOUT_TEST_FONTS "C:/Windows/Fonts" CACHE PATH "Font folder of the corpus check (PSY_FONTS_DIR overrides it at run time)")
    target_compile_definitions(test_psy_layout PRIVATE PSYSCR_NO_SDL
        PSYLAY_TEST_DIR="${PROJECT_SOURCE_DIR}/tests/layout" PSYLAY_TEST_FONTS="${PSY_LAYOUT_TEST_FONTS}")
    add_test(NAME test_psy_layout COMMAND test_psy_layout)
    # The costs of docs/psy_layout.md; not a test (run it under the lock).
    psy_add_program(layout_bench ${PROJECT_SOURCE_DIR}/tests/layout/layout_bench.c)
    target_link_libraries(layout_bench PRIVATE psy_layout)
endif()

if(PSY_BUILD_EXAMPLES AND TARGET SDL3::SDL3)
    set(PSY_CURRENT_DEPS SDL3::SDL3)
    psy_add_program(gfx_layout ${PROJECT_SOURCE_DIR}/examples/gfx_layout.c)
    target_link_libraries(gfx_layout PRIVATE psy_layout)
    set(PSY_CURRENT_DEPS "")
endif()
