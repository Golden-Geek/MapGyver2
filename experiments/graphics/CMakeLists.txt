# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(mapgyver_d3d11_baseline main.cpp ../../adapters/windows/d3d11_baseline.cpp)
set_target_properties(mapgyver_d3d11_baseline PROPERTIES OUTPUT_NAME mapgyver-d3d11-baseline)
target_include_directories(mapgyver_d3d11_baseline PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(mapgyver_d3d11_baseline PRIVATE mapgyver_core nlohmann_json::nlohmann_json d3d11 dxgi)
target_compile_definitions(mapgyver_d3d11_baseline PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(mapgyver_d3d11_baseline PRIVATE /W4 /WX /permissive- /utf-8)
