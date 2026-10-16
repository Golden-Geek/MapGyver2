# SPDX-License-Identifier: GPL-3.0-or-later
find_package(bgfx CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
if(NOT VCPKG_HOST_TRIPLET)
  set(VCPKG_HOST_TRIPLET "${VCPKG_TARGET_TRIPLET}")
endif()
find_program(MAPGYVER_SHADERC NAMES shaderc REQUIRED
  PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/bgfx" NO_DEFAULT_PATH)
set(shader_output "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(shader_sources "${CMAKE_CURRENT_SOURCE_DIR}/../shaders")
add_custom_command(OUTPUT "${shader_output}/render_shaders.hpp" "${shader_output}/shader-build.json"
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/build_render_shaders.py"
    --compiler "${MAPGYVER_SHADERC}" --include "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/include/bgfx"
    --source "${shader_sources}" --output "${shader_output}"
  DEPENDS "${PROJECT_SOURCE_DIR}/tools/build_render_shaders.py"
    "${shader_sources}/fullscreen.sc" "${shader_sources}/composite.sc" "${shader_sources}/encode.sc" "${shader_sources}/varying.def.sc"
  VERBATIM)
add_library(mapgyver_windows_renderer STATIC ../composition_renderer.cpp "${shader_output}/render_shaders.hpp")
target_include_directories(mapgyver_windows_renderer PRIVATE "${shader_output}")
target_include_directories(mapgyver_windows_renderer PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(mapgyver_windows_renderer PUBLIC mapgyver_render PRIVATE bgfx::bgfx SDL3::SDL3 d3d11 dxgi)
target_compile_definitions(mapgyver_windows_renderer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(mapgyver_windows_renderer PRIVATE /W4 /WX /permissive- /utf-8)
add_executable(mapgyver_composition_probe "${PROJECT_SOURCE_DIR}/tests/hardware/composition_probe.cpp")
set_target_properties(mapgyver_composition_probe PROPERTIES OUTPUT_NAME mapgyver-composition-probe)
target_link_libraries(mapgyver_composition_probe PRIVATE mapgyver_windows_renderer mapgyver_core nlohmann_json::nlohmann_json)
target_compile_options(mapgyver_composition_probe PRIVATE /W4 /WX /permissive- /utf-8)
add_executable(mapgyver_output_probe "${PROJECT_SOURCE_DIR}/tests/hardware/output_probe.cpp")
set_target_properties(mapgyver_output_probe PROPERTIES OUTPUT_NAME mapgyver-output-probe)
target_link_libraries(mapgyver_output_probe PRIVATE mapgyver_windows_renderer mapgyver_core nlohmann_json::nlohmann_json)
target_compile_options(mapgyver_output_probe PRIVATE /W4 /WX /permissive- /utf-8)
