# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.31)
project(MapGyverSpoutReference LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "The pinned independent peer SDK requires native Windows x64 MSVC")
endif()
get_property(multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(multi_config)
  set(CMAKE_CONFIGURATION_TYPES Release CACHE STRING "Pinned SDK ABI supports Release only" FORCE)
elseif(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  message(FATAL_ERROR "Build this reference peer in Release to match the pinned SDK ABI")
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT EXISTS "${REFERENCE_SPOUT_SDK_ROOT}/inputs.json")
  message(FATAL_ERROR "Run tools/qualification/prepare_spout_peer.py first and provide REFERENCE_SPOUT_SDK_ROOT")
endif()
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
get_filename_component(repo_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(metadata "${CMAKE_CURRENT_BINARY_DIR}/generated/mapgyver/source_revision.hpp")
add_custom_target(reference_source_metadata
  COMMAND "${Python3_EXECUTABLE}" "${repo_root}/tools/build_metadata.py" --root "${repo_root}" --output "${metadata}"
  BYPRODUCTS "${metadata}" VERBATIM)
add_library(reference_spout SHARED IMPORTED)
set_target_properties(reference_spout PROPERTIES
  IMPORTED_IMPLIB "${REFERENCE_SPOUT_SDK_ROOT}/MD/lib/SpoutDX.lib"
  IMPORTED_LOCATION "${REFERENCE_SPOUT_SDK_ROOT}/MD/bin/SpoutDX.dll")
add_executable(mapgyver_spout_reference "${repo_root}/adapters/windows/spout_participant.cpp")
add_dependencies(mapgyver_spout_reference reference_source_metadata)
set_target_properties(mapgyver_spout_reference PROPERTIES OUTPUT_NAME mapgyver-spout-reference)
target_include_directories(mapgyver_spout_reference SYSTEM PRIVATE "${REFERENCE_SPOUT_SDK_ROOT}/include/SpoutDX")
target_include_directories(mapgyver_spout_reference PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(mapgyver_spout_reference PRIVATE nlohmann_json::nlohmann_json reference_spout d3d11 dxgi)
target_compile_definitions(mapgyver_spout_reference PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN SPOUT_IMPORT_DLL
  MAPGYVER_REFERENCE_SPOUT "MAPGYVER_SPOUT_SDK_VERSION=\"2.007.018\"" _ITERATOR_DEBUG_LEVEL=0)
target_compile_options(mapgyver_spout_reference PRIVATE /W4 /WX /permissive- /utf-8)
add_custom_command(TARGET mapgyver_spout_reference POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:reference_spout>" "$<TARGET_FILE_DIR:mapgyver_spout_reference>"
  VERBATIM)
