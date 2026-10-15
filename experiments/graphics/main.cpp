// SPDX-License-Identifier: GPL-3.0-or-later
#include "probe.hpp"
#include <mapgyver/build_info.hpp>
#include <exception>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    if (argc > 2 || (argc == 2 && std::string_view{argv[1]} != "--json")) {
        std::cerr << "Usage: mapgyver-d3d11-baseline [--json]\n";
        return 2;
    }
    try {
        auto report = mapgyver::experiments::d3d11_shared_texture_baseline();
        const auto build = mapgyver::build_info();
        report["sourceRevision"] = build.revision.empty() ? nlohmann::json(nullptr) : nlohmann::json(build.revision);
        report["sourceDirty"] = build.dirty.has_value() ? nlohmann::json(*build.dirty) : nlohmann::json(nullptr);
        report["compiler"] = {{"id", build.compiler_id}, {"version", build.compiler_version}};
        report["buildConfiguration"] = build.configuration;
        report["platform"] = {{"targetOs", build.target_os}, {"architecture", build.architecture}};
        std::cout << report.dump(2) << '\n';
        return std::cout ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "D3D11 baseline failed: " << error.what() << '\n';
        return 1;
    }
}
