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
std::array<FeatureInfo, 5> feature_info() noexcept {
    return {{{"platform", PM_BUILD_PLATFORM != 0, false, "not_implemented"},
             {"media", PM_BUILD_MEDIA != 0, false, "not_implemented"},
             {"ui", PM_BUILD_UI != 0, PM_BUILD_UI != 0, PM_BUILD_UI ? "static_editor_built" : "not_bundled"},
             {"hardwareTests", PM_BUILD_HARDWARE_TESTS != 0, false, "no_hardware_suite"},
             {"ndi", PM_BUILD_NDI != 0, false, "owner_approval_required"}}};
}
} // namespace mapgyver
