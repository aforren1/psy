# Standalone build of the beam-racing probe (docs/beam_race_probe.md).
#   cmake -S tests/probe/beam_race -B build/beam_race
#   cmake --build build/beam_race --config Release
# It is not part of the main project: it needs a display and a GPU, and it
# shows full-screen patterns.
cmake_minimum_required(VERSION 3.20)
project(beam_race_probe C)
if(NOT WIN32)
  message(FATAL_ERROR "beam_race_probe is Windows-only")
endif()
add_executable(beam_race_probe beam_race_probe.c)
set_target_properties(beam_race_probe PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_include_directories(beam_race_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../include)
target_link_libraries(beam_race_probe PRIVATE d3d11 dxgi user32 gdi32 advapi32 wtsapi32)
if(MSVC)
  target_compile_options(beam_race_probe PRIVATE /W4 /WX)
else()
  target_compile_options(beam_race_probe PRIVATE -Wall -Wextra -Werror)
endif()
