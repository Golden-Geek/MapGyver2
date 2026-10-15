# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(mapgyver_d3d11_baseline main.cpp ../../adapters/windows/d3d11_baseline.cpp)
set_target_properties(mapgyver_d3d11_baseline PROPERTIES OUTPUT_NAME mapgyver-d3d11-baseline)
target_include_directories(mapgyver_d3d11_baseline PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(mapgyver_d3d11_baseline PRIVATE mapgyver_core nlohmann_json::nlohmann_json d3d11 dxgi)
target_compile_definitions(mapgyver_d3d11_baseline PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(mapgyver_d3d11_baseline PRIVATE /W4 /WX /permissive- /utf-8)
if(PM_BUILD_BGFX_EXPERIMENT)
  find_package(bgfx CONFIG REQUIRED)
  find_package(SDL3 CONFIG REQUIRED)
  set(shader_header "${CMAKE_CURRENT_BINARY_DIR}/generated/probe_shaders.hpp")
  add_custom_command(OUTPUT "${shader_header}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/embed_probe_shaders.py"
      --directory "${CMAKE_CURRENT_SOURCE_DIR}/shaders" --output "${shader_header}"
    DEPENDS "${PROJECT_SOURCE_DIR}/tools/embed_probe_shaders.py"
      shaders/provenance.json shaders/vs_fullscreen.bin shaders/fs_bokeh_copy.bin
    VERBATIM)
  add_executable(mapgyver_bgfx_interop bgfx_main.cpp ../../adapters/windows/bgfx_interop.cpp "${shader_header}")
  set_target_properties(mapgyver_bgfx_interop PROPERTIES OUTPUT_NAME mapgyver-bgfx-interop)
  target_include_directories(mapgyver_bgfx_interop PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_link_libraries(mapgyver_bgfx_interop PRIVATE mapgyver_core nlohmann_json::nlohmann_json bgfx::bgfx SDL3::SDL3 d3d11 dxgi)
  target_compile_definitions(mapgyver_bgfx_interop PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(mapgyver_bgfx_interop PRIVATE /W4 /WX /permissive- /utf-8)
endif()
