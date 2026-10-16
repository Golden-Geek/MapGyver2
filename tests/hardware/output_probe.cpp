// SPDX-License-Identifier: GPL-3.0-or-later
#include "adapters/windows/composition_renderer.hpp"
#include "adapters/windows/display_inventory.hpp"
#include "adapters/windows/preparation_device.hpp"
#include "adapters/windows/prepared_graph.hpp"
#include <mapgyver/build_info.hpp>
#include <mapgyver/control/wire.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>
#include <thread>
#include <fstream>
#include <future>

using namespace mapgyver;
using namespace domain;
namespace {
void check(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
control::ProjectState project_fixture() {
    Definitions definitions;
    definitions.assets.emplace(Id{"alpha"},Asset{Id{"alpha"},"Gradient",ExternalSource{"mapgyver.pattern","gradient",{{Id{"picture"},"Picture",StreamKind::video}}}});
    auto project = make_default_project(std::move(definitions));
    auto library = project.instances.definitions().data();
    for (auto& [id,composition] : library.compositions) {
        static_cast<void>(id);for (auto& [port,canvas] : composition.canvases) {static_cast<void>(port);canvas.width = 128;canvas.height = 64;}
    }
    static_cast<void>(project.instances.reconcile_definitions(DefinitionSnapshot{std::move(library)}));
    static_cast<void>(project.instances.bind_input(project.root_instance,project.content_input,{Id{"alpha"},Id{"picture"}},false));
    auto stage = project.stage.data();
    stage.outputs.at(project.output).adapter = "windows.window";
    stage.outputs.at(project.output).desired = {128,64,{60,1}};
    for (unsigned i = 0; i < 2; ++i) {
        const Id id{"native-output-"+std::to_string(i+2)};
        stage.outputs.emplace(id,OutputTarget{id,"Native "+std::to_string(i+2),"windows.window","",{i == 0 ? 97U : 64U,i == 0 ? 53U : 32U,{60,1}}});
        const Id patch{"native-patch-"+std::to_string(i+2)};
        stage.patches.emplace(patch,ProjectionPatch{patch,"Full frame",project.surface,id,quad_calibration(128,64)});
    }
    project.stage = StageSnapshot{std::move(stage),project.instances};
    control::ProjectController controller{std::move(project)};
    return *controller.snapshot();
}
std::shared_ptr<const render::Plan> fixture() {
    const auto state = project_fixture();
    render::Images images;
    for (const auto& [id,source] : state.instances.instances()) if (source.asset == Id{"alpha"})
        images.emplace(render::PortKey{id,Id{"picture"}},render::pattern(Id{"gradient"},1,128,64,"gradient"));
    return render::compile(state,0,images);
}
nlohmann::json mapping_cases() {
    auto state = project_fixture();control::ProjectController controller{state};
    const auto root = state.instances.placements().at(state.root_placement).source;
    const auto surface = state.stage.data().surfaces.begin()->first;
    const auto output = state.stage.data().outputs.begin()->first;
    const Id second{"mapped-surface-2"},third{"mapped-surface-3"},group{"mapped-group"},layout{"mapped-layout"};
    auto stage = state.stage.data();
    stage.outputs.at(output).desired.width = 129;stage.outputs.at(output).desired.height = 65;
    stage.surfaces.emplace(second,Surface{second,"Middle",std::nullopt,unit_quad()});
    stage.surfaces.emplace(third,Surface{third,"Right",std::nullopt,unit_quad()});
    stage.patches.at(Id{"native-patch-3"}).surface = third;
    auto& first_patch = stage.patches.begin()->second;
    first_patch.calibration = quad_calibration(128,64);
    for (auto& vertex : first_patch.calibration.vertices) vertex.projector_pixels.x /= 3;
    const auto first_patch_id = first_patch.id;
    for (unsigned i = 1; i < 3; ++i) {
        const Id id{"mapped-patch-"+std::to_string(i)};
        auto calibration = quad_calibration(128,64);
        for (auto& vertex : calibration.vertices) vertex.projector_pixels.x = (vertex.projector_pixels.x+128*i)/3;
        stage.patches.emplace(id,ProjectionPatch{id,"Mapped region",i == 1 ? second : third,output,std::move(calibration)});
    }
    stage.groups.emplace(group,SurfaceGroup{group,"Shared composition regions",{surface,second,third},{}});
    static_cast<void>(controller.transact(0,{control::ReplaceStage{stage},control::AssignSurfaceContent{second,root},control::AssignSurfaceContent{third,root}}));
    stage = controller.snapshot()->stage.data();
    stage.layouts.emplace(layout,Layout{layout,"Continuous",group,LayoutMode::continuous,
        {quad_chart(surface,{0,0},{1.0/3,1}),quad_chart(second,{1.0/3,0},{2.0/3,1}),quad_chart(third,{2.0/3,0},{1,1})}});
    static_cast<void>(controller.transact(1,{control::ReplaceStage{stage}}));
    const auto images = [&](std::uint64_t generation) {
        render::Images result;for (const auto& [id,source] : controller.snapshot()->instances.instances()) if (source.asset == Id{"alpha"})
            result.emplace(render::PortKey{id,Id{"picture"}},render::pattern(Id{"mapping-gradient"},generation,128,64,"gradient"));
        return result;
    };
    auto plan = render::compile(*controller.snapshot(),2,images(1));
    auto device = std::make_shared<windows::PreparationDevice>();std::vector<windows::OutputWindow> outputs;
    for (const auto& [id,item] : stage.outputs) outputs.push_back({id,item.name,item.desired.width,item.desired.height,plan->preview,true,{}});
    windows::CompositionRenderer renderer{plan,outputs,device};nlohmann::json captures = nlohmann::json::array();
    std::uint64_t frames = 0,staging_frames = 0,retirement_frames = 0;std::int64_t ticks = -1;
    const auto frame = [&](std::uint64_t revision) {
        const auto value = renderer.frame();check(value.sequence == ++frames && value.revision == revision && value.show_time.ticks >= ticks,"Mapping handoff regressed native frame/revision/clock");ticks = value.show_time.ticks;
    };
    const auto capture = [&](const char* phase) {
        for (const auto& target : outputs) {
            const auto actual = renderer.capture_output_for_test(target.id),expected = render::reference_output(*plan,target.id,target.width,target.height);
            check(actual.size() == expected.size(),"Mapped output dimensions differ");unsigned maximum = 0;std::size_t mismatch = 0;
            for (std::size_t i = 0; i < actual.size(); ++i) {
                const auto difference = static_cast<unsigned>(std::abs(static_cast<int>(actual[i])-expected[i]));
                if (difference > maximum) {maximum = difference;mismatch = i;}
            }
            if (maximum > 2) std::cerr << phase << " output=" << target.id.str() << " max=" << maximum << " x=" << (mismatch/4)%target.width << " y=" << (mismatch/4)/target.width << " channel=" << mismatch%4 << " actual=" << unsigned(actual[mismatch]) << " expected=" << unsigned(expected[mismatch]) << '\n';
            check(maximum <= 2,"Mapped output differs from fixed linear-display reference tolerance");
            captures.push_back({{"phase",phase},{"output",target.id.str()},{"width",target.width},{"height",target.height},{"maxChannelError",maximum},{"tolerance",2},
                {"validationCpuReadbackBytes",actual.size()},{"validationGpuCopySubmissions",1}});
        }
    };
    const auto apply = [&](std::shared_ptr<const render::Plan> next,std::uint64_t generation) {
        const auto limits = renderer.graph_limits();auto preparing = std::async(std::launch::async,[&] {
            return windows::prepare_graph(*device,next,generation,limits,std::vector<std::uint32_t>(outputs.size(),next->preview));
        });
        const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds{3};
        while (preparing.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) {frame(plan->revision);++staging_frames;check(std::chrono::steady_clock::now() < deadline,"Mapping preparation timed out");}
        auto candidate = preparing.get();while (!renderer.stage_graph(candidate)) {frame(plan->revision);++staging_frames;check(std::chrono::steady_clock::now() < deadline,"Mapping staging timed out");}
        plan = std::move(next);frame(plan->revision);
        std::unique_ptr<windows::PreparedGraph> retired;
        while (!(retired = renderer.retire_graph())) {frame(plan->revision);++retirement_frames;check(std::chrono::steady_clock::now() < deadline,"Mapping retirement timed out");}
        auto cleanup = std::async(std::launch::async,[retired=std::move(retired)]() mutable {retired.reset();});cleanup.get();
    };
    frame(2);capture("continuous-three-regions-many-to-many");
    auto changed = controller.snapshot()->stage.data();auto& perspective = changed.patches.at(first_patch_id).calibration;
    const std::array<Vec2,4> corners{{{4,4},{39,8},{35,59},{8,55}}};
    for (std::size_t i = 0; i < 4; ++i) perspective.vertices[i].projector_pixels = corners[i];
    static_cast<void>(controller.transact(2,{control::ReplaceStage{changed}}));
    apply(render::compile(*controller.snapshot(),3,images(2)),2);capture("projective-quad-black-border");
    changed = controller.snapshot()->stage.data();changed.patches.at(Id{"mapped-patch-1"}).calibration = quad_calibration(128,64);
    static_cast<void>(controller.transact(3,{control::ReplaceStage{changed}}));
    apply(render::compile(*controller.snapshot(),4,images(3)),3);capture("overlapping-linear-premultiplied-patches");
    changed = controller.snapshot()->stage.data();changed.patches.erase(Id{"mapped-patch-2"});
    auto left_half = quad_calibration(128,64),right_half = left_half;
    for (auto& vertex : left_half.vertices) vertex.projector_pixels.x *= .5;
    for (auto& vertex : right_half.vertices) vertex.projector_pixels.x = 64+vertex.projector_pixels.x*.5;
    changed.patches.at(first_patch_id).calibration = std::move(left_half);
    changed.patches.at(Id{"mapped-patch-1"}).calibration = std::move(right_half);
    changed.layouts.at(layout).charts = {quad_chart(surface,{0,0},{.5,1}),quad_chart(second,{.5,0},{1,1}),quad_chart(third,{2.0/3,0},{1,1})};
    static_cast<void>(controller.transact(4,{control::ReplaceStage{changed}}));
    apply(render::compile(*controller.snapshot(),5,images(4)),4);capture("shared-boundary-pixel-centre");
    const auto identity = renderer.adapter_identity();
    return {{"evidenceClass","native-hardware-GPU-hidden-projective-mapping"},{"surfaces",3},{"windows",3},{"patchCounts",{5,5,5,4}},{"compositionNodes",plan->nodes.size()},
        {"framesSubmitted",frames},{"oldFramesDuringPreparation",staging_frames},{"retirementFrames",retirement_frames},{"captures",captures},
        {"vendorId",identity[0]},{"deviceId",identity[1]},{"luidHigh",identity[2]},{"luidLow",identity[3]},{"sourceCpuUploadBytes",device->cpu_upload_bytes()},
        {"scope","One shared composition over three charted surface regions, one surface across outputs, several surfaces on one output, perspective quad coverage and overlapping linear-light premultiplied patches. Real background graph handoff and native completion. Intermediate texture captures, not physical scanout, mesh warps, masks, crop/fit/fill or sustained deadline qualification."}};
}
nlohmann::json run() {
    nlohmann::json displays = nlohmann::json::array();
    for (const auto& display : windows::detected_displays()) displays.push_back({{"endpoint",display.endpoint},{"name",display.name},
        {"width",display.mode.width},{"height",display.mode.height},{"refreshNumerator",display.mode.refresh_hz.numerator},
        {"refreshDenominator",display.mode.refresh_hz.denominator},{"x",display.x},{"y",display.y},{"reason",display.reason}});
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
        {"framesSubmitted",frame.sequence},{"detectedDisplays",displays},{"vendorId",gpu[0]},{"deviceId",gpu[1]},{"luidHigh",gpu[2]},{"luidLow",gpu[3]},
        {"drawSubmissions",renderer.submitted_draws()},{"foreignOwnerRejected",refused_foreign_owner},{"captures",captures},
        {"scope","Three hidden SDL native windows with independent bgfx/D3D11 swapchains. Captures validate the exact intermediate textures submitted to those windows, not physical scanout/backbuffer receipt. Static prepared content; no browser, production engine or display-topology/hotplug qualification. Software frame metadata does not establish synchronized physical scanout."}};
}
}
int main(int argc,char** argv) {
    if (argc == 3 && std::string_view{argv[1]} == "--write-project") {
        try {std::ofstream output{argv[2],std::ios::binary};output << control::wire::encode_project(project_fixture()).dump(2) << '\n';check(output.good(),"Could not write native engine fixture");return 0;}
        catch (const std::exception& error) {std::cerr << error.what() << '\n';return 1;}
    }
    if (argc == 2 && std::string_view{argv[1]} == "--version") {std::cout << "MapGyver native output window qualification\n";return 0;}
    if (argc != 2 || std::string_view{argv[1]} != "--json") {std::cerr << "Usage: mapgyver-output-probe --json\n";return 2;}
    try {
        auto result = run();result["mapping"] = mapping_cases();const auto build = build_info();result["sourceRevision"] = build.revision;result["sourceDirty"] = build.dirty ? nlohmann::json(*build.dirty) : nlohmann::json(nullptr);result["configuration"] = build.configuration;
        std::cout << result.dump(2) << '\n';return std::cout ? 0 : 1;
    } catch (const std::exception& error) {std::cerr << "Native output qualification failed: " << error.what() << '\n';return 1;}
}
