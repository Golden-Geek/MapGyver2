// SPDX-License-Identifier: GPL-3.0-or-later
#include "adapters/windows/composition_renderer.hpp"
#include <mapgyver/build_info.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string_view>

using namespace mapgyver;
using namespace domain;
namespace {
void check(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
Asset pattern(std::string id) {return {Id{id},id,ExternalSource{"mapgyver.pattern",id,{{Id{"picture"},"Picture",StreamKind::video}}}};}
Composition composition(std::string id,std::vector<Layer> layers) {
    return {Id{id},id,{},{{Id{"picture"},"Picture",StreamKind::video},{Id{"small"},"Small",StreamKind::video}},std::move(layers),
        {{Id{"picture"},Canvas{64,32}},{Id{"small"},Canvas{17,11}}}};
}
Layer layer(std::string id,std::string source) {return {Id{id},id,StreamKind::video,AssetPort{Id{source},Id{"picture"}},std::nullopt};}
std::shared_ptr<const render::Plan> fixture(unsigned mode) {
    Definitions definitions;
    definitions.assets.emplace(Id{"bars"},pattern("bars"));definitions.assets.emplace(Id{"alpha"},pattern("alpha"));
    definitions.assets.emplace(Id{"gradient"},pattern("gradient"));
    std::string prior = mode >= 2 ? "gradient" : "bars";
    const unsigned depth = mode == 5 ? 8 : 1;
    for (unsigned index = 0; index < depth; ++index) {
        const auto id = "nested-"+std::to_string(index);
        std::vector<Layer> layers{layer("back",prior)};
        if (index == 0 && mode != 0) layers.push_back(layer("front","alpha"));
        if (mode == 6 && index == 0) for (unsigned copy = 0; copy < 300; ++copy) layers.push_back(layer("copy-"+std::to_string(copy),"bars"));
        definitions.assets.emplace(Id{id},Asset{Id{id},id,CompositionSource{Id{id}}});
        definitions.compositions.emplace(Id{id},composition(id,std::move(layers))); prior = id;
    }
    auto project = make_default_project(std::move(definitions));
    auto library = project.instances.definitions().data();
    const auto root_asset = project.instances.instance(project.root_instance).asset;
    const auto root_composition = std::get<CompositionSource>(library.assets.at(root_asset).source).composition;
    for (auto& [port,canvas] : library.compositions.at(root_composition).canvases) {
        static_cast<void>(port);canvas.width = 128;canvas.height = 64;
        canvas.colour_space = mode == 2 || mode == 4 ? "linear-srgb" : "srgb";
        canvas.alpha = mode == 3 || mode == 4 ? "premultiplied" : "straight";
    }
    static_cast<void>(project.instances.reconcile_definitions(DefinitionSnapshot{std::move(library)}));
    static_cast<void>(project.instances.bind_input(project.root_instance,project.content_input,{Id{prior},Id{"picture"}},false));
    const auto nested = project.instances.instance(project.root_instance).inputs.at(project.content_input).instance;
    control::ProjectController controller{std::move(project)};
    auto stage = controller.snapshot()->stage.data();
    stage.surfaces.emplace(Id{"second"},Surface{Id{"second"},"Independent named output",std::nullopt,unit_quad()});
    static_cast<void>(controller.transact(0,{control::ReplaceStage{stage},control::AssignSurfaceContent{Id{"second"},InstancePort{nested,Id{"small"}}}}));
    render::Images images;
    const auto bars = render::pattern(Id{"bars-resource"},mode+1,64,32,"bars");
    const auto alpha = render::pattern(Id{"alpha-resource"},mode+1,64,32,"alpha");
    const auto gradient = render::pattern(Id{"gradient-resource"},mode+1,64,32,"gradient");
    for (const auto& [id,source] : controller.snapshot()->instances.instances()) {
        if (mode == 7) continue; // Actual unavailable sources: retain configured objects and reasons.
        if (source.asset == Id{"bars"}) images.emplace(render::PortKey{id,Id{"picture"}},bars);
        if (source.asset == Id{"alpha"}) images.emplace(render::PortKey{id,Id{"picture"}},alpha);
        if (source.asset == Id{"gradient"}) images.emplace(render::PortKey{id,Id{"picture"}},gradient);
    }
    return render::compile(*controller.snapshot(),controller.revision(),images);
}
nlohmann::json run() {
    nlohmann::json cases = nlohmann::json::array();
    for (unsigned mode = 0; mode < 8; ++mode) {
        const auto plan = fixture(mode);
        windows::CompositionRenderer renderer{plan};
        std::int64_t prior = -1; windows::RenderedFrame last{};
        for (unsigned frame = 0; frame < 20; ++frame) {
            last = renderer.frame();
            check(last.sequence == frame+1 && last.revision == plan->revision && last.show_time.ticks >= prior,"Native frame/monotonic authored application metadata differs");
            prior = last.show_time.ticks;
        }
        std::vector<std::uint32_t> outputs{plan->preview};
        for (const auto& surface : plan->surfaces) if (std::find(outputs.begin(),outputs.end(),surface.node) == outputs.end()) outputs.push_back(surface.node);
        nlohmann::json captures = nlohmann::json::array();
        for (const auto output : outputs) {
            const auto actual = renderer.capture_for_test(output), expected = render::reference(*plan,output);
            check(actual.size() == expected.size(),"Native compositor capture dimensions differ from declared canvas");
            unsigned maximum = 0; std::uint64_t checksum = 1469598103934665603ULL;
            for (std::size_t index = 0; index < actual.size(); ++index) {
                maximum = std::max(maximum,static_cast<unsigned>(std::abs(static_cast<int>(actual[index])-expected[index])));
                checksum = (checksum^actual[index])*1099511628211ULL;
            }
            check(maximum <= 2,"Native compositor differs from fixed linear/alpha reference tolerance");
            captures.push_back({{"node",output},{"width",plan->nodes[output].width},{"height",plan->nodes[output].height},
                {"pixelFormat","rgba8-unorm"},{"colourSpace",plan->nodes[output].encoding.transfer == render::Transfer::srgb ? "srgb" : "linear-srgb"},
                {"alpha",plan->nodes[output].encoding.alpha == render::Alpha::straight ? "straight" : "premultiplied"},
                {"maxChannelError",maximum},{"tolerance",2},{"actualPixelChecksum",std::to_string(checksum)},
                {"validationCpuReadbackBytes",actual.size()},{"validationGpuCopySubmissions",1}});
        }
        const auto gpu = renderer.adapter_identity();
        cases.push_back({{"mode",mode},{"vendorId",gpu[0]},{"deviceId",gpu[1]},{"luidHigh",gpu[2]},{"luidLow",gpu[3]},
            {"framesSubmitted",last.sequence},{"authoredRevisionSubmitted",last.revision},{"lastMonotonicTicks",std::to_string(last.show_time.ticks)},
            {"clockTimeBase",{{"numerator","1"},{"denominator","1000000000"}}},{"drawSubmissions",renderer.submitted_draws()},
            {"planNodes",plan->nodes.size()},{"targetSlots",plan->targets.size()},{"plannedInternalTargetPayloadBytes",plan->target_bytes},{"plannedExportPayloadBytes",plan->export_bytes},
            {"sourceCpuUploadBytes",plan->image_upload_bytes},{"internalConstantUploadBytes",plan->fallback_bytes},{"unavailableSources",plan->issues.size()},{"captures",captures}});
    }
    return {{"schemaVersion",1},{"evidenceClass","native-hardware-GPU-offscreen-bgfx-D3D11-composition-goldens"},{"cases",cases},
        {"scope","Actual GPU rasterization and offline CPU readback. Static prepared render plans, no browser/native output windows or physical scanout. Resource preparation and synchronous validation are outside frame(). Runtime graph handoff, encoded preview, deadline/performance and hardware recovery qualification remain open."}};
}
}
int main(int argc,char** argv) {
    if (argc == 2 && std::string_view{argv[1]} == "--version") {std::cout << "MapGyver provisional native compositor qualification\n";return 0;}
    if (argc != 2 || std::string_view{argv[1]} != "--json") {std::cerr << "Usage: mapgyver-composition-probe --json\n";return 2;}
    try {
        auto result = run();const auto build = build_info();result["sourceRevision"] = build.revision;result["sourceDirty"] = build.dirty ? nlohmann::json(*build.dirty) : nlohmann::json(nullptr);result["configuration"] = build.configuration;
        std::cout << result.dump(2) << '\n';return std::cout ? 0 : 1;
    } catch (const std::exception& error) {std::cerr << "Native composition qualification failed: " << error.what() << '\n';return 1;}
}
