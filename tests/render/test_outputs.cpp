// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/runtime/outputs.hpp>
#include <mapgyver/control/wire.hpp>
#include <algorithm>
#include <array>
#include <iostream>

using namespace mapgyver;
using namespace domain;
namespace {
void check(bool condition,const char* message) {if (!condition) throw std::runtime_error(message);}
constexpr auto display_adapter = "windows.display";
const std::array capabilities{runtime::OutputCapability{display_adapter,true,8192,8192},runtime::OutputCapability{"windows.window",false,8192,8192}};
runtime::Display display(std::string path = "stable-monitor-path") {return {std::move(path),"Projector",{1920,1080,{60000,1000}},-1920,0};}
void persistent_binding() {
    auto initial = make_default_project();const auto output = initial.output;
    auto stage = initial.stage.data();
    const std::array detected{display()};
    auto bind = [&] {return runtime::bind_outputs(StageSnapshot{stage,initial.instances},detected,capabilities).front();};
    check(bind().reason == "output_unbound","Default output silently selected a detected monitor");
    stage.outputs.at(output).endpoint = "stable-monitor-path";
    auto ready = bind();
    check(ready.reason.empty() && ready.display && ready.display->x == -1920 && ready.desired.id == output,"Exact stable endpoint/mode failed binding");
    const auto duplicate = std::array{display(),display()};
    const auto ambiguous = runtime::bind_outputs(StageSnapshot{stage,initial.instances},duplicate,capabilities);
    check(ambiguous.front().reason == "output_display_ambiguous" && !ambiguous.front().display,"Ambiguous target silently selected an OS enumeration entry");
    const std::array reordered{display("other-path"),display()};
    check(runtime::bind_outputs(StageSnapshot{stage,initial.instances},reordered,capabilities).front().display->endpoint == ready.display->endpoint,"Display order changed logical target identity");
    check(runtime::bind_outputs(StageSnapshot{stage,initial.instances},{},capabilities).front().reason == "output_display_absent","Missing target was rebound to the operator display");
    const auto patches = stage.patches;
    stage.outputs.at(output).desired.width = 1280;
    check(bind().reason == "output_display_mode_mismatch" && stage.patches == patches,"Mode mismatch rewrote calibration or claimed an actual display mode switch");
    auto cloned = std::array{display()};cloned[0].reason = "output_display_source_cloned";
    check(runtime::bind_outputs(StageSnapshot{stage,initial.instances},cloned,capabilities).front().reason == "output_display_source_cloned",
        "Cloned display source was declared independently presentable");
    stage.outputs.at(output).adapter = "transport.ndi";
    stage.outputs.at(output).endpoint = "unsupported-configured-target";
    const auto unsupported = bind();
    check(unsupported.reason == "output_adapter_unavailable" && unsupported.desired == stage.outputs.at(output),"Unsupported target configuration was lost");
    for (unsigned i = 0; i < 37; ++i) {
        const Id id{"window-"+std::to_string(i)};
        stage.outputs.emplace(id,OutputTarget{id,"Window","windows.window","",{320,180,{60,1}}});
    }
    const auto all = runtime::bind_outputs(StageSnapshot{stage,initial.instances},detected,capabilities);
    check(all.size() == 38 && std::count_if(all.begin(),all.end(),[](const auto& x){return x.reason.empty();}) == 37,"Arbitrary logical windows depend on detected monitor count");
    auto limits = runtime::OutputLimits{};limits.targets = 37;
    bool rejected = false;try {static_cast<void>(runtime::bind_outputs(StageSnapshot{stage,initial.instances},detected,capabilities,limits));} catch (const runtime::OutputError&) {rejected = true;}
    check(rejected && stage.outputs.size() == 38,"Output capacity silently removed configured objects");
}
void controls_round_trip() {
    auto initial = make_default_project();const auto output = initial.output;
    control::ProjectController controller{std::move(initial)};
    const auto original = controller.snapshot(); const auto address = "/outputs/"+output.str();
    const auto change = control::ParameterRegistry{controller}.write(controller,0,
        {{address+"/enabled",false},{address+"/blackout",true},{address+"/freeze",true}});
    const auto& target = change.snapshot->stage.data().outputs.at(output);
    check(!target.enabled && target.blackout && target.freeze && change.snapshot->stage.data().patches == original->stage.data().patches,
        "Validated output controls changed content/calibration or partially applied");
    const auto wire = control::wire::encode_project(*change.snapshot);
    const auto restored = control::wire::decode_project(wire);
    check(restored.stage.data().outputs.at(output) == target,"Output intent did not round trip through the typed schema");
    const auto bindings = runtime::bind_outputs(change.snapshot->stage,{},capabilities);
    check(bindings.front().reason == "output_disabled" && bindings.front().desired.blackout && bindings.front().desired.freeze,"Disabled output lost retained blackout/freeze intent");
    static_cast<void>(controller.undo(1));
    check(controller.snapshot() == original,"Output controls bypassed shared undo/history");
}
}
int main() {
    try {persistent_binding();controls_round_trip();std::cout << "Persistent output binding and validated control contracts passed\n";return 0;}
    catch (const std::exception& error) {std::cerr << error.what() << '\n';return 1;}
}
