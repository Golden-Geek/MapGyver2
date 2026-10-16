// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/build_info.hpp>
#include <mapgyver/protocol/service.hpp>
#include <mapgyver/assets/import.hpp>
#include "adapters/osc/network.hpp"
#ifdef _WIN32
#include "adapters/windows/pipe_service.hpp"
#if MAPGYVER_NATIVE_RENDERER
#include "adapters/windows/native_program.hpp"
#endif
#endif
#include <nlohmann/json.hpp>
#include <bit>
#include <climits>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <filesystem>
#include <fstream>
#include <charconv>

namespace {
constexpr std::string_view usage =
    "MapGyver native engine\n"
    "Usage: mapgyver-engine --version\n"
    "       mapgyver-engine --diagnose [--json] [--no-gpu]\n"
    "       mapgyver-engine --serve --pipe-name <stable-id> [--oscquery]\n"
    "         [--editor-dir <static-assets>] [--dev-origin http://127.0.0.1:<port>] (Windows)\n"
    "         [--render] [--initial-state <control-project.json>]\n"
    "         [--image <PNG-or-JPEG-path>] [--media-root <directory>]\n"
    "         [--render-hidden --native-frames <count> --native-report <offline-report.json>]\n"
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

int engine_main(int argc, char** argv) {
    try {
        bool version = false, diagnostic = false, json = false, no_gpu = false, help = false, serve = false, oscquery = false, render = false, render_hidden = false;
        std::string initial_state, native_report, image, media_root;
        std::uint64_t native_frames = 0;
        std::optional<std::string> pipe_name;
        mapgyver::network::Options network_options;
        for (int index = 1; index < argc; ++index) {
            const std::string_view arg{argv[index]};
            if (arg == "--initial-state" || arg == "--native-report" || arg == "--image" || arg == "--media-root") {
                auto& value = arg == "--initial-state" ? initial_state : arg == "--native-report" ? native_report : arg == "--image" ? image : media_root;
                if (!value.empty() || index+1 >= argc || std::string_view{argv[index+1]}.empty() || std::string_view{argv[index+1]}.starts_with("--")) {std::cerr << "Missing or repeated native option\n";return 2;}
                value = argv[++index];continue;
            }
            if (arg == "--native-frames") {
                if (native_frames || index+1 >= argc) {std::cerr << "Missing or repeated native frame budget\n";return 2;}
                const std::string_view value{argv[++index]};
                const auto result = std::from_chars(value.data(),value.data()+value.size(),native_frames);
                if (result.ec != std::errc{} || result.ptr != value.data()+value.size() || !native_frames || native_frames > 1000000) {std::cerr << "Native frame budget must be 1..1000000\n";return 2;}
                continue;
            }
            if (arg == "--editor-dir" || arg == "--dev-origin") {
                auto& value = arg == "--editor-dir" ? network_options.editor_directory : network_options.development_origin;
                if (!value.empty() || index+1 >= argc || std::string_view{argv[index+1]}.empty() || std::string_view{argv[index+1]}.starts_with("--")) {std::cerr << "Missing or repeated network option\n" << usage; return 2;}
                value = argv[++index]; continue;
            }
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
            else if (arg == "--render") flag = &render;
            else if (arg == "--render-hidden") flag = &render_hidden;
            if (flag == nullptr || *flag) {
                std::cerr << "Invalid or repeated argument: " << arg << '\n' << usage;
                return 2;
            }
            *flag = true;
        }
        const auto modes = static_cast<int>(version) + static_cast<int>(diagnostic) + static_cast<int>(help) + static_cast<int>(serve);
        if ((argc > 1 && modes != 1) || ((json || no_gpu) && !diagnostic) || (pipe_name.has_value() != serve) || (oscquery && !serve) ||
            ((!network_options.editor_directory.empty() || !network_options.development_origin.empty()) && (!serve || !oscquery)) ||
            ((render || !initial_state.empty()) && !serve) || ((render_hidden || native_frames || !native_report.empty()) && !render) ||
            ((!image.empty() || !media_root.empty()) && (!serve || !render)) ||
            (!native_report.empty() && !native_frames)) {
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
            mapgyver::control::ProjectController controller{mapgyver::domain::make_default_project()};
            if (!initial_state.empty()) {
                std::ifstream input{std::filesystem::path{std::u8string{initial_state.begin(),initial_state.end()}},std::ios::binary|std::ios::ate};
                if (!input.good() || input.tellg() < 0 || input.tellg() > 16777216) throw std::runtime_error("Initial authored state is missing or exceeds its byte capacity");
                std::string bytes(static_cast<std::size_t>(input.tellg()),'\0');input.seekg(0);input.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
                if (!input.good()) throw std::runtime_error("Could not read initial authored state");
                controller = mapgyver::control::ProjectController{mapgyver::control::wire::decode_project(mapgyver::control::wire::parse(bytes))};
            }
            if (media_root.empty()) {
                const auto root = initial_state.empty() ? std::filesystem::current_path() : std::filesystem::absolute(std::filesystem::path{std::u8string{initial_state.begin(),initial_state.end()}}).parent_path();
                const auto bytes = root.u8string();media_root.assign(bytes.begin(),bytes.end());
            }
            if (!image.empty()) {
                auto imported = mapgyver::assets::import_image(*controller.snapshot(),image,"Imported image",initial_state.empty());
                if (initial_state.empty()) {
                    auto stage = controller.snapshot()->stage.data();for (auto& [id,output] : stage.outputs) {static_cast<void>(id);output.adapter = "windows.window";output.desired.width = 1280;output.desired.height = 720;}
                    imported.commands.push_back(mapgyver::control::ReplaceStage{std::move(stage)});
                }
                static_cast<void>(mapgyver::control::ParameterRegistry{controller}.execute(controller,controller.revision(),imported.commands));
            }
            mapgyver::protocol::ControlService service{std::move(controller)};
#if MAPGYVER_NATIVE_RENDERER
            std::unique_ptr<mapgyver::windows::NativeProgram> program;
            if (render) program = std::make_unique<mapgyver::windows::NativeProgram>(service,mapgyver::windows::ProgramOptions{render_hidden,native_frames,native_report,media_root});
#else
            if (render) {std::cerr << "Native compositor is not included in this build\n";return 3;}
#endif
            std::unique_ptr<mapgyver::network::OscQueryServer> network;
            if (network_options.editor_directory.empty()) {
                for (const auto& feature : mapgyver::feature_info()) if (feature.name == "ui" && feature.available) {
                    const auto path = (std::filesystem::path{std::u8string{argv[0],argv[0]+std::char_traits<char>::length(argv[0])}}.parent_path()/"ui").u8string();
                    network_options.editor_directory.assign(path.begin(),path.end());
                }
            }
            if (oscquery) network = std::make_unique<mapgyver::network::OscQueryServer>(service,network_options);
            mapgyver::windows::serve_named_pipe(service,*pipe_name,[&](const std::string& endpoint) {
                const auto build = mapgyver::build_info();
                auto ready = mapgyver::windows::pipe_capabilities();
                ready.update({{"event","control_ready"},{"transport","windows_named_pipe"},{"endpoint",endpoint},
                    {"sourceRevision",build.revision.empty() ? nlohmann::json(nullptr) : nlohmann::json(build.revision)},
                    {"sourceDirty",build.dirty.has_value() ? nlohmann::json(*build.dirty) : nlohmann::json(nullptr)}});
                if (network) {ready["oscquery"] = network->endpoints(); ready["editor"] = network->editor_bootstrap();}
                ready["nativeProgram"] = {{"configured",render},{"hidden",render_hidden},{"frameLimit",std::to_string(native_frames)}};
                std::cout << ready.dump() << '\n' << std::flush;
            },[&] {
                if (network) network->poll();
#if MAPGYVER_NATIVE_RENDERER
                if (program && program->poll()) mapgyver::windows::request_pipe_stop();
#endif
            });
#if MAPGYVER_NATIVE_RENDERER
            if (program) program->finish();
#endif
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
#ifndef _WIN32
int main(int argc,char** argv) {return engine_main(argc,argv);}
#endif
