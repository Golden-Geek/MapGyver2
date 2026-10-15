// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/build_info.hpp>
#include <mapgyver/build_config.hpp>
#include <mapgyver/source_revision.hpp>
namespace mapgyver {
BuildInfo build_info() noexcept {
#if defined(_M_X64) || defined(__x86_64__)
    constexpr std::string_view architecture = "x64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    constexpr std::string_view architecture = "arm64";
#else
    constexpr std::string_view architecture = "unknown";
#endif
    return {MAPGYVER_PRODUCT_NAME, MAPGYVER_VERSION, source_revision,
            source_known ? std::optional<bool>{source_dirty} : std::nullopt,
            MAPGYVER_COMPILER_ID, MAPGYVER_COMPILER_VERSION,
            MAPGYVER_BUILD_CONFIGURATION, MAPGYVER_CMAKE_VERSION,
            MAPGYVER_TARGET_OS, architecture};
}
} // namespace mapgyver
