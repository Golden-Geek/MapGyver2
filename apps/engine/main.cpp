// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/build_info.hpp>
#include <mapgyver/protocol/service.hpp>
#include "adapters/osc/network.hpp"
#ifdef _WIN32
#include "adapters/windows/pipe_service.hpp"
#endif
#include <nlohmann/json.hpp>
#include <bit>
#include <climits>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
constexpr std::string_view usage =
    "MapGyver native engine\n"
    "Usage: mapgyver-engine --version\n"
    "       mapgyver-engine --diagnose [--json] [--no-gpu]\n"
    "       mapgyver-engine --serve --pipe-name <stable-id> [--oscquery] (Windows)\n"
    "       mapgyver-engine --help\n";

nlohmann::json diagnose(bool no_gpu) {
    using nlohmann::json;
    const auto build = mapgyver::build_info();
    json features = json::object();
    for (const auto& feature : mapgyver::feature_info()) {
        features[feature.name] = {{"configured", feature.configured},
                                 {"available", feature.available}, {"reason", feature.reason}};
    }
    constexpr auto byte_order = std::endian::native == std::endian::little ? "little" :
                                std::endian::native == std::endian::big ? "big" : "mixed";
    return {
        {"schemaVersion", 1},
        {"product", {{"name", build.product}, {"version", build.version}}},
        {"build", {{"revision", build.revision.empty() ? json(nullptr) : json(build.revision)},
                   {"dirty", build.dirty.has_value() ? json(*build.dirty) : json(nullptr)},
                   {"configuration", build.configuration}, {"cmakeVersion", build.cmake_version},
                   {"compiler", {{"id", build.compiler_id}, {"version", build.compiler_version}}}}},
        {"platform", {{"os", build.target_os}, {"architecture", build.architecture},
                      {"pointerBits", sizeof(void*) * CHAR_BIT}, {"byteOrder", byte_order}}},
        {"gpu", {{"status", no_gpu ? "not_probed" : "unavailable"},
                 {"reason", no_gpu ? "disabled_by_request" : "gpu_probe_not_implemented"}}},
        {"features", std::move(features)}
    };
}
} // namespace

int main(int argc, char** argv) {
    try {
        bool version = false, diagnostic = false, json = false, no_gpu = false, help = false, serve = false, oscquery = false;
        std::optional<std::string> pipe_name;
        for (int index = 1; index < argc; ++index) {
            const std::string_view arg{argv[index]};
            if (arg == "--pipe-name") {
                if (pipe_name || index+1 >= argc) {std::cerr << "Missing or repeated pipe name\n" << usage; return 2;}
                pipe_name = argv[++index];
                try {static_cast<void>(mapgyver::domain::Id{*pipe_name});}
                catch (const mapgyver::domain::ModelError&) {std::cerr << "Invalid pipe name\n" << usage; return 2;}
                continue;
            }
            bool* flag = nullptr;
            if (arg == "--version") flag = &version;
            else if (arg == "--diagnose") flag = &diagnostic;
            else if (arg == "--json") flag = &json;
            else if (arg == "--no-gpu") flag = &no_gpu;
            else if (arg == "--help") flag = &help;
            else if (arg == "--serve") flag = &serve;
            else if (arg == "--oscquery") flag = &oscquery;
            if (flag == nullptr || *flag) {
                std::cerr << "Invalid or repeated argument: " << arg << '\n' << usage;
                return 2;
            }
            *flag = true;
        }
        const auto modes = static_cast<int>(version) + static_cast<int>(diagnostic) + static_cast<int>(help) + static_cast<int>(serve);
        if ((argc > 1 && modes != 1) || ((json || no_gpu) && !diagnostic) || (pipe_name.has_value() != serve) || (oscquery && !serve)) {
            std::cerr << "Choose one command; --json and --no-gpu require --diagnose.\n" << usage;
            return 2;
        }
        if (help || argc == 1) {
            std::cout << usage;
        } else if (version) {
            const auto build = mapgyver::build_info();
            std::cout << build.product << ' ' << build.version << '\n';
        } else if (serve) {
#ifdef _WIN32
            mapgyver::protocol::ControlService service{mapgyver::control::ProjectController{mapgyver::domain::make_default_project()}};
            std::unique_ptr<mapgyver::network::OscQueryServer> network;
            if (oscquery) network = std::make_unique<mapgyver::network::OscQueryServer>(service);
            mapgyver::windows::serve_named_pipe(service,*pipe_name,[&](const std::string& endpoint) {
                const auto build = mapgyver::build_info();
                auto ready = mapgyver::windows::pipe_capabilities();
                ready.update({{"event","control_ready"},{"transport","windows_named_pipe"},{"endpoint",endpoint},
                    {"sourceRevision",build.revision.empty() ? nlohmann::json(nullptr) : nlohmann::json(build.revision)},
                    {"sourceDirty",build.dirty.has_value() ? nlohmann::json(*build.dirty) : nlohmann::json(nullptr)}});
                if (network) ready["oscquery"] = network->endpoints();
                std::cout << ready.dump() << '\n' << std::flush;
            },[&] {if (network) network->poll();});
#else
            std::cerr << "Windows named-pipe control transport is unavailable on this platform\n"; return 3;
#endif
        } else {
            const auto report = diagnose(no_gpu);
            if (json) std::cout << report.dump(2) << '\n';
            else {
                const auto build = mapgyver::build_info();
                std::cout << build.product << ' ' << build.version << '\n'
                          << "Platform: " << build.target_os << '/' << build.architecture << '\n'
                          << "Build: " << build.configuration << " (" << build.compiler_id << ' '
                          << build.compiler_version << ")\n"
                          << "GPU: " << report.at("gpu").at("status").get<std::string>() << " ("
                          << report.at("gpu").at("reason").get<std::string>() << ")\n";
            }
        }
        if (!std::cout) throw std::runtime_error("Could not write diagnostic output");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Engine failure: " << error.what() << '\n';
        return 1;
    }
}
