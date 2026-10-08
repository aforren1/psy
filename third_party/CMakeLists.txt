# The vendored layout stack (README.md here): static libraries, built from
# the files in this tree only, no network. Upstream code compiles with its
# warnings off, and its include directories are SYSTEM, so our /W4 /WX and
# -Werror never see its warnings. Upstream's own build files are not
# vendored: these targets replace them.

if(MSVC)
    set(PSY_TP_NOWARN /W0)
else()
    set(PSY_TP_NOWARN -w)
endif()

# HarfBuzz from its amalgamation. C++ without exceptions and RTTI, as the
# layout probe built it.
add_library(psy_harfbuzz STATIC harfbuzz/src/harfbuzz.cc)
target_include_directories(psy_harfbuzz SYSTEM PUBLIC harfbuzz/src)
target_compile_options(psy_harfbuzz PRIVATE ${PSY_TP_NOWARN})
set_target_properties(psy_harfbuzz PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(psy_harfbuzz PRIVATE /bigobj /GR- /EHs-c-)
    target_compile_definitions(psy_harfbuzz PRIVATE _HAS_EXCEPTIONS=0)
else()
    target_compile_options(psy_harfbuzz PRIVATE -fno-exceptions -fno-rtti)
    if(MINGW)
        target_compile_options(psy_harfbuzz PRIVATE -Wa,-mbig-obj)
    endif()
endif()

# SheenBidi as one translation unit (SheenBidi.c includes the others).
add_library(psy_sheenbidi STATIC sheenbidi/Source/SheenBidi.c)
target_compile_definitions(psy_sheenbidi PUBLIC SB_CONFIG_UNITY)
target_include_directories(psy_sheenbidi SYSTEM PUBLIC sheenbidi/Headers)
target_include_directories(psy_sheenbidi PRIVATE sheenbidi/Source)
target_compile_options(psy_sheenbidi PRIVATE ${PSY_TP_NOWARN})

# libunibreak: the files its own Makefile.gcc compiles (the *data.c files are
# included by them).
add_library(psy_unibreak STATIC
    libunibreak/src/linebreak.c libunibreak/src/linebreakdata.c libunibreak/src/linebreakdef.c
    libunibreak/src/wordbreak.c libunibreak/src/graphemebreak.c libunibreak/src/eastasianwidthdef.c
    libunibreak/src/emojidef.c libunibreak/src/unibreakbase.c libunibreak/src/unibreakdef.c)
target_include_directories(psy_unibreak SYSTEM PUBLIC libunibreak/src)
target_compile_options(psy_unibreak PRIVATE ${PSY_TP_NOWARN})

add_library(psy_budoux STATIC budouxc/src/budoux.c)
target_include_directories(psy_budoux SYSTEM PUBLIC budouxc/include)
target_compile_options(psy_budoux PRIVATE ${PSY_TP_NOWARN})

# Skribidi's layout and editor. emoji_presentation_scanner.c is included by
# skb_common.c, so it is not compiled on its own. C17 with extensions, as
# upstream sets it.
add_library(psy_skribidi STATIC
    skribidi/src/skb_attribute_collection.c skribidi/src/skb_attributes.c skribidi/src/skb_common.c
    skribidi/src/skb_editor.c skribidi/src/skb_editor_rules.c skribidi/src/skb_font_collection.c
    skribidi/src/skb_icon_collection.c skribidi/src/skb_layout.c skribidi/src/skb_rich_layout.c
    skribidi/src/skb_rich_text.c skribidi/src/skb_text.c)
target_include_directories(psy_skribidi SYSTEM PUBLIC skribidi/include)
target_include_directories(psy_skribidi PRIVATE skribidi/src)
target_compile_options(psy_skribidi PRIVATE ${PSY_TP_NOWARN})
set_target_properties(psy_skribidi PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS ON)
target_link_libraries(psy_skribidi PUBLIC psy_harfbuzz PRIVATE psy_sheenbidi psy_unibreak psy_budoux)
if(NOT WIN32)
    target_link_libraries(psy_skribidi PUBLIC m)
endif()

# The patch hashes the layout glue logs (psylay_get_versions, the pack
# manifest), in the order the patches apply; the pins themselves are in
# pack/psy_layout.c. The patches' hashes say what should be applied; the
# patched file's hash against the manifest says that it is. A tree that
# differs still builds (to compare with upstream, for example), but its
# logged versions say so.
set(patches "")
foreach(name skribidi-0001-bidi-run-merge skribidi-0002-l1-trailing-whitespace)
    file(SHA256 ${CMAKE_CURRENT_SOURCE_DIR}/patches/${name}.patch sha)
    list(APPEND patches "sha256:${sha}")
endforeach()
string(JOIN "," patches ${patches})
file(SHA256 ${CMAKE_CURRENT_SOURCE_DIR}/skribidi/src/skb_layout.c layout_sha)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/MANIFEST.sha256 layout_line REGEX " skribidi/src/skb_layout\\.c$")
string(REGEX MATCH "^[0-9a-f]+" layout_want "${layout_line}")
if(NOT layout_sha STREQUAL layout_want)
    message(WARNING "third_party/skribidi/src/skb_layout.c differs from MANIFEST.sha256: the logged versions say tree-differs")
    set(patches "${patches},tree-differs")
endif()
set(PSY_SKRIBIDI_PATCHES ${patches} PARENT_SCOPE)
