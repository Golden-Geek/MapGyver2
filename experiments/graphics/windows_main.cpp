// SPDX-License-Identifier: GPL-3.0-or-later
#include "probe.hpp"
#include <mapgyver/build_info.hpp>
#include <exception>
#include <iostream>
#include <string_view>
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view{argv[1]} == "--version") {
        std::cout << "MapGyver M01 native hidden multi-window API probe\n"; return 0;
    }
    if (argc != 2 || std::string_view{argv[1]} != "--json") {
        std::cerr << "Usage: mapgyver-window-probe --json\n"; return 2;
    }
    try {
        auto report = mapgyver::experiments::bgfx_window_submission();
        const auto build = mapgyver::build_info();
        report["sourceRevision"] = build.revision;
        report["sourceDirty"] = build.dirty.has_value() ? nlohmann::json(*build.dirty) : nlohmann::json(nullptr);
        report["buildConfiguration"] = build.configuration;
        std::cout << report.dump(2) << '\n'; return std::cout ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Native window probe failed: " << error.what() << '\n'; return 1;
    }
}
