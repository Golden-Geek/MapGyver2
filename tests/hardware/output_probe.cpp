// SPDX-License-Identifier: GPL-3.0-or-later
#include "adapters/windows/composition_renderer.hpp"
#include <mapgyver/build_info.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>
#include <thread>

using namespace mapgyver;
using namespace domain;
namespace {
void check(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
std::shared_ptr<const render::Plan> fixture() {
    Definitions definitions;
    definitions.assets.emplace(Id{"alpha"},Asset{Id{"alpha"},"Gradient",ExternalSource{"mapgyver.pattern","gradient",{{Id{"picture"},"Picture",StreamKind::video}}}});
    auto project = make_default_project(std::move(definitions));
    auto library = project.instances.definitions().data();
    for (auto& [id,composition] : library.compositions) {
        static_cast<void>(id);for (auto& [port,canvas] : composition.canvases) {static_cast<void>(port);canvas.width = 128;canvas.height = 64;}
    }
    static_cast<void>(project.instances.reconcile_definitions(DefinitionSnapshot{std::move(library)}));
    static_cast<void>(project.instances.bind_input(project.root_instance,project.content_input,{Id{"alpha"},Id{"picture"}},false));
    control::ProjectController controller{std::move(project)};
    render::Images images;
    for (const auto& [id,source] : controller.snapshot()->instances.instances()) if (source.asset == Id{"alpha"})
        images.emplace(render::PortKey{id,Id{"picture"}},render::pattern(Id{"gradient"},1,128,64,"gradient"));
    return render::compile(*controller.snapshot(),controller.revision(),images);
}
nlohmann::json run() {
    const auto plan = fixture();
    std::vector<windows::OutputWindow> outputs{
        {Id{"one"},"MapGyver test 1",128,64,plan->preview,true,{true,false,true}},
        {Id{"two"},"MapGyver test 2",97,53,plan->preview,true,{}},
        {Id{"three"},"MapGyver test 3",64,32,plan->preview,true,{}}};
    windows::CompositionRenderer renderer{plan,outputs};
    bool refused_foreign_owner = false;
    std::thread foreign{[&] {
        try {renderer.output_controls(Id{"one"},{});} catch (const render::Error&) {refused_foreign_owner = true;}
    }};foreign.join();check(refused_foreign_owner,"Foreign thread mutated native output controls");
    const auto expected = render::reference(*plan,plan->preview,true);
    nlohmann::json captures = nlohmann::json::array();
    const auto capture = [&](const char* name,const char* id,bool blank) {
        const auto actual = renderer.capture_output_for_test(Id{id});
        const auto& configured = *std::find_if(outputs.begin(),outputs.end(),[&](const auto& item){return item.id == Id{id};});
        std::vector<std::uint8_t> reference(static_cast<std::size_t>(configured.width)*configured.height*4,0);
        for (std::uint32_t y = 0; y < configured.height; ++y) for (std::uint32_t x = 0; x < configured.width; ++x) {
            const auto offset = (static_cast<std::size_t>(y)*configured.width+x)*4;
            reference[offset+3] = 255;
            if (!blank) {
                const auto sx = (static_cast<std::uint64_t>(x)*2+1)*128/(static_cast<std::uint64_t>(configured.width)*2);
                const auto sy = (static_cast<std::uint64_t>(y)*2+1)*64/(static_cast<std::uint64_t>(configured.height)*2);
                std::copy_n(expected.begin()+static_cast<std::ptrdiff_t>((sy*128+sx)*4),4,reference.begin()+static_cast<std::ptrdiff_t>(offset));
            }
        }
        check(actual.size() == reference.size(),"Native output capture dimensions differ");unsigned maximum = 0;
        for (std::size_t i = 0; i < actual.size(); ++i) maximum = std::max(maximum,static_cast<unsigned>(std::abs(static_cast<int>(actual[i])-reference[i])));
        check(maximum <= 2,"Native logical output differs from fixed display-reference tolerance");
        captures.push_back({{"phase",name},{"output",id},{"width",configured.width},{"height",configured.height},{"black",blank},{"maxChannelError",maximum},{"tolerance",2},
            {"heldSoftwareFrame",renderer.held_frame(Id{id})},{"validationCpuReadbackBytes",actual.size()},{"validationGpuCopySubmissions",1}});
    };
    auto frame = renderer.frame();
    capture("initial-freeze","one",true);capture("active","two",false);capture("active","three",false);
    check(renderer.held_frame(Id{"one"}) == 0,"Initial freeze sampled an uninitialized resource");
    for (unsigned i = 0; i < 4; ++i) frame = renderer.frame();
    renderer.output_controls(Id{"one"},{true,false,false});frame = renderer.frame();
    check(frame.sequence == 6 && renderer.held_frame(Id{"one"}) == 6,"Unfreeze failed to accept the current complete frame");capture("unfreeze","one",false);
    renderer.output_controls(Id{"one"},{true,false,true});renderer.output_controls(Id{"two"},{true,true,false});
    const auto clock = frame.show_time.ticks;
    for (unsigned i = 0; i < 5; ++i) frame = renderer.frame();
    check(frame.sequence == 11 && frame.show_time.ticks > clock && renderer.held_frame(Id{"one"}) == 6 && renderer.held_frame(Id{"three"}) == 11,
        "One output's freeze/blackout stopped native output or its monotonic clock");
    capture("held","one",false);capture("blackout","two",true);capture("independent","three",false);
    renderer.output_controls(Id{"one"},{true,true,true});renderer.output_controls(Id{"two"},{});renderer.output_controls(Id{"three"},{false,false,false});
    frame = renderer.frame();capture("blackout-frozen","one",true);capture("blackout-release","two",false);capture("disabled","three",true);
    renderer.output_controls(Id{"one"},{true,false,true});frame = renderer.frame();capture("restore-held","one",false);
    check(frame.sequence == 13 && renderer.held_frame(Id{"one"}) == 6 && renderer.held_frame(Id{"three"}) == 11,"Blackout/disable discarded retained frame ownership");
    const auto gpu = renderer.adapter_identity();
    return {{"schemaVersion",1},{"evidenceClass","native-hardware-GPU-hidden-output-window-API"},{"windows",outputs.size()},
        {"framesSubmitted",frame.sequence},{"vendorId",gpu[0]},{"deviceId",gpu[1]},{"luidHigh",gpu[2]},{"luidLow",gpu[3]},
        {"drawSubmissions",renderer.submitted_draws()},{"foreignOwnerRejected",refused_foreign_owner},{"captures",captures},
        {"scope","Three hidden SDL native windows with independent bgfx/D3D11 swapchains. Captures validate the exact intermediate textures submitted to those windows, not physical scanout/backbuffer receipt. Static prepared content; no browser, production engine or display-topology/hotplug qualification. Software frame metadata does not establish synchronized physical scanout."}};
}
}
int main(int argc,char** argv) {
    if (argc == 2 && std::string_view{argv[1]} == "--version") {std::cout << "MapGyver native output window qualification\n";return 0;}
    if (argc != 2 || std::string_view{argv[1]} != "--json") {std::cerr << "Usage: mapgyver-output-probe --json\n";return 2;}
    try {
        auto result = run();const auto build = build_info();result["sourceRevision"] = build.revision;result["sourceDirty"] = build.dirty ? nlohmann::json(*build.dirty) : nlohmann::json(nullptr);result["configuration"] = build.configuration;
        std::cout << result.dump(2) << '\n';return std::cout ? 0 : 1;
    } catch (const std::exception& error) {std::cerr << "Native output qualification failed: " << error.what() << '\n';return 1;}
}
