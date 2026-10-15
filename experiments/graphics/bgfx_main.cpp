// SPDX-License-Identifier: GPL-3.0-or-later
#include "probe.hpp"
#include <mapgyver/build_info.hpp>
#include <exception>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    bool json = false, hold_key = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--json" && !json) json = true;
        else if (argument == "--hold-producer-key" && !hold_key) hold_key = true;
        else {
            std::cerr << "Usage: mapgyver-bgfx-interop [--json] [--hold-producer-key]\n";
            return 2;
        }
    }
    try {
        auto report = mapgyver::experiments::bgfx_texture_interop(hold_key);
        const auto build = mapgyver::build_info();
        report["sourceRevision"] = build.revision.empty() ? nlohmann::json(nullptr) : nlohmann::json(build.revision);
        report["sourceDirty"] = build.dirty.has_value() ? nlohmann::json(*build.dirty) : nlohmann::json(nullptr);
        report["compiler"] = {{"id", build.compiler_id}, {"version", build.compiler_version}};
        report["buildConfiguration"] = build.configuration;
        std::cout << report.dump(2) << '\n';
        return std::cout ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "bgfx texture interop failed: " << error.what() << '\n';
        return 1;
    }
}
