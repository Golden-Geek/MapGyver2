// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_program.hpp"
#include "composition_renderer.hpp"
#include "preparation_device.hpp"
#include "display_inventory.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>

namespace mapgyver::windows {
namespace {
using namespace domain;
using control::wire::Json;
constexpr std::size_t max_outputs = 256;
using Controls = std::array<OutputControls,max_outputs>;
struct Update {std::uint64_t generation = 1, revision = 0;Controls outputs{};};
struct Request {std::uint64_t generation = 1, revision = 0;std::shared_ptr<const control::ProjectState> state;};
Json static_state(const control::ProjectState& state) {
    auto value = control::wire::encode_project(state);
    // Display names are metadata, never GPU resource identity. Normalize only
    // typed record fields, so an entity whose ID is "name" stays in its map.
    for (auto& asset : value["definitions"]["assets"]) asset.erase("name");
    for (auto& composition : value["definitions"]["compositions"]) {
        composition.erase("name");
        for (const auto* field : {"inputs","outputs","layers"}) for (auto& record : composition[field]) record.erase("name");
    }
    for (const auto* field : {"surfaces","groups","layouts","outputs","patches"}) for (auto& record : value["stage"][field]) record.erase("name");
    for (auto& [id,output] : value["stage"]["outputs"].items()) {
        static_cast<void>(id);output.erase("enabled");output.erase("blackout");output.erase("freeze");
    }
    return value;
}
render::Images patterns(const control::ProjectState& state,std::uint64_t generation = 1,std::uint64_t byte_limit = render::Limits{}.image_bytes) {
    render::Images images;std::map<Id,std::shared_ptr<const render::Image>> assets;
    std::uint64_t bytes = 0;
    const auto active = render::compile(state,0,{});
    for (const auto& node : active->nodes) {
        const auto& source = state.instances.instance(node.source.first);
        const auto& asset = state.instances.definitions().asset(source.asset);
        const auto* external = std::get_if<ExternalSource>(&asset.source);
        if (!external || external->adapter != "mapgyver.pattern") continue;
        if (external->locator != "bars" && external->locator != "checker" && external->locator != "alpha" && external->locator != "gradient") continue;
        auto found = assets.find(asset.id);
        if (found == assets.end()) {
            constexpr std::uint64_t size = 128*64*16;
            if (size > byte_limit-bytes) throw render::Error("Prepared test patterns exceed bounded image capacity");
            auto image = render::pattern(asset.id,generation,128,64,external->locator);bytes += size;found = assets.emplace(asset.id,std::move(image)).first;
        }
        images.emplace(node.source,found->second);
    }
    return images;
}
}
struct NativeProgram::Impl {
    protocol::ControlService& service;
    ProgramOptions options;
    std::shared_ptr<const control::ProjectState> initial;
    std::uint64_t initial_revision;
    Json baseline;
    const Json initial_stage;
    std::thread worker;
    std::promise<void> exited;
    std::future<void> completion = exited.get_future();
    std::atomic<bool> stopping{false};
    std::mutex input_mutex, status_mutex;
    Update pending{};bool has_pending = false;
    Request request{};
    enum class Phase {idle,ready,in_use,retired};
    Phase phase = Phase::idle;
    std::unique_ptr<PreparedGeneration> candidate;
    std::shared_ptr<const render::Plan> prepared_plan;
    std::uint64_t ready_generation = 0, failed_generation = 0, desired_generation = 1;
    std::uint64_t prepared_generations = 0, discarded_generations = 0;
    std::string preparation_error;
    struct Status {bool failed = false, finished = false;RenderedFrame frame{};std::uint64_t scene_generation = 1;std::string error, preparation_reason;};
    Status status;
    std::uint64_t checked_revision = UINT64_MAX, observed_frame = 0;
    bool failure_reported = false;
    explicit Impl(protocol::ControlService& source,ProgramOptions settings)
        : service(source),options(std::move(settings)),initial(service.snapshot()),initial_revision(service.revision()),baseline(static_state(*initial)),initial_stage(baseline["stage"]) {
        if (!options.report.empty() && !options.frame_limit) throw render::Error("Offline native report requires a bounded frame run");
        service.begin_renderer(1);
        worker = std::thread([this] {
            try {run();} catch (const std::exception& error) {
                std::lock_guard lock{status_mutex};status.failed = true;status.error = std::string{error.what()}.substr(0,512);
            }
            exited.set_value();
        });
    }
    ~Impl() {
        stop();
    }
    void stop() {
        stopping.store(true);
        if (completion.wait_for(std::chrono::seconds{5}) != std::future_status::ready) {
            std::fputs("Fatal: native render owner shutdown did not complete within five seconds\n",stderr);std::_Exit(1);
        }
        if (worker.joinable()) worker.join();
    }
    void run() {
        if (initial->stage.data().outputs.size() > max_outputs) throw render::Error("Native output inventory exceeds bounded control capacity");
        auto plan = render::compile(*initial,initial_revision,patterns(*initial));
        auto stage = initial->stage.data();
        // Explicitly prewarm configured disabled targets, retaining their actual
        // authored enable intent below, so enable never creates a deadline resource.
        for (auto& [id,output] : stage.outputs) {static_cast<void>(id);output.enabled = true;}
        const std::array caps{runtime::OutputCapability{"windows.window",false,16384,16384},runtime::OutputCapability{"windows.display",true,16384,16384}};
        const auto displays = detected_displays();
        auto bindings = runtime::bind_outputs(StageSnapshot{std::move(stage),initial->instances},displays,caps);
        std::vector<OutputWindow> windows;std::vector<std::size_t> indices;
        Json reasons = Json::array();
        for (std::size_t index = 0; index < bindings.size(); ++index) {
            auto& binding = bindings[index];const auto& original = initial->stage.data().outputs.at(binding.desired.id);
            if (binding.reason.empty()) {
                const ProjectionPatch* patch = nullptr;std::size_t count = 0;
                for (const auto& [id,item] : initial->stage.data().patches) {static_cast<void>(id);if (item.output == original.id) {patch = &item;++count;}}
                if (count != 1 || !initial->stage.data().layouts.empty() || patch->calibration != quad_calibration(patch->calibration.reference_width,patch->calibration.reference_height) ||
                    initial->stage.data().surfaces.at(patch->surface).geometry != unit_quad()) binding.reason = "projection_mapping_unavailable";
                else {
                    const auto surface = std::find_if(plan->surfaces.begin(),plan->surfaces.end(),[&](const auto& item){return item.id == patch->surface;});
                    if (surface == plan->surfaces.end()) binding.reason = "surface_content_unavailable";
                    else {
                        OutputWindow output{original.id,original.name,original.desired.width,original.desired.height,surface->node,options.hidden,
                            {original.enabled,original.blackout,original.freeze}};
                        if (binding.display) output.desktop_position = std::array{binding.display->x,binding.display->y};
                        windows.push_back(std::move(output));indices.push_back(index);
                    }
                }
            }
            reasons.push_back({{"id",original.id.str()},{"reason",binding.reason},{"configuredAdapter",original.adapter},{"configuredEndpoint",original.endpoint}});
        }
        auto device = std::make_shared<PreparationDevice>();
        CompositionRenderer renderer{plan,windows,device};
        const auto layout = plan;
        const auto payload = renderer.resource_payload_bytes();
        // One candidate can be working, ready, copying or retired. No next
        // allocation starts until retired ownership is disposed on preparation.
        std::jthread preparer{[&,layout,payload](std::stop_token stop) {
            std::uint64_t attempted = 1;
            while (!stop.stop_requested()) {
                Request work;std::unique_ptr<PreparedGeneration> dispose;
                {
                    std::lock_guard lock{input_mutex};
                    if (phase == Phase::retired) {
                        dispose = std::move(candidate);prepared_plan.reset();phase = Phase::idle;
                    }
                    if (phase == Phase::idle && request.generation > attempted) {work = request;attempted = work.generation;}
                }
                dispose.reset(); // Includes native source refs/old CPU plans, off the render owner.
                if (!work.state) {std::this_thread::sleep_for(std::chrono::milliseconds{2});continue;}
                try {
                    if (work.state->stage.data().outputs.size() > max_outputs || static_state(*work.state)["stage"] != initial_stage)
                        throw render::Error("Output/stage changes require graph/window resource preparation");
                    auto next_plan = render::compile(*work.state,work.revision,patterns(*work.state,work.generation,134217728));
                    {
                        std::lock_guard lock{input_mutex};
                        if (stop.stop_requested() || pending.generation != work.generation) {++discarded_generations;continue;}
                    }
                    auto next_candidate = prepare_generation(*device,*layout,next_plan,work.generation,payload);
                    std::lock_guard lock{input_mutex};++prepared_generations;
                    if (stop.stop_requested() || pending.generation != work.generation) {++discarded_generations;continue;}
                    candidate = std::move(next_candidate);prepared_plan = std::move(next_plan);ready_generation = work.generation;phase = Phase::ready;
                } catch (const std::exception& error) {
                    std::lock_guard lock{input_mutex};failed_generation = work.generation;preparation_error = std::string{error.what()}.substr(0,512);
                }
            }
        }};
        {
            std::lock_guard lock{status_mutex};
            status.preparation_reason = !plan->issues.empty() ? "source_frames_unavailable" : windows.empty() && !bindings.empty() ? "output_targets_unavailable" : "";
        }
        std::array<OutputControls,64> controls{};
        for (std::size_t i = 0; i < windows.size(); ++i) controls[i] = windows[i].controls;
        std::uint64_t revision = initial_revision, scene_generation = 1;RenderedFrame frame{};
        auto next = std::chrono::steady_clock::now();
        while (!stopping.load()) {
            {
                std::unique_lock lock{input_mutex,std::try_to_lock};
                if (lock.owns_lock()) {
                    if (phase == Phase::in_use) {
                        auto retired = renderer.retire_images();
                        if (retired) {candidate = std::move(retired);phase = Phase::retired;}
                    }
                    if (phase == Phase::ready) {
                        if (ready_generation != pending.generation) {phase = Phase::retired;++discarded_generations;}
                        else if (renderer.apply_images(candidate)) {
                            plan = prepared_plan;scene_generation = ready_generation;phase = Phase::in_use;
                        }
                    }
                    if (has_pending && pending.generation == scene_generation && pending.revision >= revision) {
                        for (std::size_t i = 0; i < indices.size(); ++i) controls[i] = pending.outputs[indices[i]];
                        revision = pending.revision;
                        renderer.apply_output_controls(revision,std::span{controls.data(),indices.size()});
                        has_pending = false;
                    }
                }
            }
            frame = renderer.frame();
            {
                std::unique_lock lock{status_mutex,std::try_to_lock};if (lock.owns_lock()) {status.frame = frame;status.scene_generation = scene_generation;}
            }
            if (options.frame_limit && frame.sequence >= options.frame_limit) break;
            next += std::chrono::nanoseconds{16666667};
            const auto now = std::chrono::steady_clock::now();if (next < now) next = now;
            std::this_thread::sleep_until(next);
        }
        preparer.request_stop();preparer.join(); // Device preparation ends before bgfx/native teardown.
        if (!options.report.empty() && frame.sequence >= options.frame_limit) {
            // Program loop has ended. Synchronous validation/readback and JSON/IO
            // are explicitly offline qualification, never a render deadline path.
            Json captures = Json::array();
            for (std::size_t i = 0; i < windows.size(); ++i) {
                const auto actual = renderer.capture_output_for_test(windows[i].id);
                const auto base = render::reference(*plan,windows[i].node,true);
                const auto held = renderer.held_frame(windows[i].id);
                const bool black = !controls[i].enabled || controls[i].blackout || !held;
                const auto& node = plan->nodes[windows[i].node];unsigned maximum = 0;
                for (std::uint32_t y = 0; y < windows[i].height; ++y) for (std::uint32_t x = 0; x < windows[i].width; ++x) {
                    const auto offset = (static_cast<std::size_t>(y)*windows[i].width+x)*4;
                    const auto sx = (static_cast<std::uint64_t>(x)*2+1)*node.width/(static_cast<std::uint64_t>(windows[i].width)*2);
                    const auto sy = (static_cast<std::uint64_t>(y)*2+1)*node.height/(static_cast<std::uint64_t>(windows[i].height)*2);
                    for (std::size_t c = 0; c < 4; ++c) {
                        const auto expected = black ? (c == 3 ? 255 : 0) : base[static_cast<std::size_t>((sy*node.width+sx)*4)+c];
                        maximum = std::max(maximum,static_cast<unsigned>(std::abs(static_cast<int>(actual[offset+c])-expected)));
                    }
                }
                if (maximum > 2) throw render::Error("Offline engine output differs from fixed display-reference tolerance");
                std::uint64_t checksum = 1469598103934665603ULL;
                for (const auto byte : actual) checksum = (checksum^byte)*1099511628211ULL;
                captures.push_back({{"output",windows[i].id.str()},{"width",windows[i].width},{"height",windows[i].height},
                    {"heldSoftwareFrame",held},{"actualPixelChecksum",std::to_string(checksum)},{"maxChannelError",maximum},{"tolerance",2},
                    {"validationCpuReadbackBytes",actual.size()},{"validationGpuCopySubmissions",1}});
            }
            const auto gpu = renderer.adapter_identity();
            Json report{{"evidenceClass","native-engine-program-pattern-source-handoff-hidden-window-API"},{"framesSubmitted",frame.sequence},
                {"appliedRevision",frame.revision},{"showTimeTicks",std::to_string(frame.show_time.ticks)},{"vendorId",gpu[0]},
                {"sourceCpuUploadBytes",device->cpu_upload_bytes()},{"sceneGeneration",scene_generation},
                {"preparedGenerations",prepared_generations},{"discardedGenerations",discarded_generations},
                {"sourceGpuCopySubmissions",renderer.image_handoff_state().gpu_copy_submissions},
                {"sourceCopiedPayloadBytes",renderer.image_handoff_state().copied_payload_bytes},
                {"lastPreparationError",preparation_error},
                {"deviceId",gpu[1]},{"luidHigh",gpu[2]},{"luidLow",gpu[3]},{"configuredOutputs",reasons},{"captures",captures},
                {"scope","Actual engine native render owner and bounded fixed-layout image preparation/handoff. Offline final intermediate texture captures after the bounded program loop, not physical scanout or encoded preview. Graph/window resource changes and nontrivial patch mapping remain open."}};
            report["sourceIssues"] = Json::array();
            for (const auto& issue : plan->issues) report["sourceIssues"].push_back({{"instance",issue.source.first.str()},{"port",issue.source.second.str()},{"reason",issue.reason}});
            std::ofstream output{std::filesystem::path{std::u8string{options.report.begin(),options.report.end()}},std::ios::binary};output << report.dump(2) << '\n';
            if (!output.good()) throw render::Error("Could not retain offline native program report");
        }
        std::lock_guard lock{status_mutex};status.frame = frame;status.scene_generation = scene_generation;status.finished = true;
    }
    bool poll() {
        if (service.revision() != checked_revision) {
            checked_revision = service.revision();const auto current = service.snapshot();
            auto structure = static_state(*current);
            const bool changed = structure != baseline;
            if (changed) {
                if (desired_generation == UINT64_MAX) throw render::Error("Native scene generation capacity exhausted");
                ++desired_generation;baseline = std::move(structure);
            }
            {
                Update update;update.generation = desired_generation;update.revision = checked_revision;std::size_t index = 0;
                for (const auto& [id,output] : current->stage.data().outputs) {
                    static_cast<void>(id);if (index < max_outputs) update.outputs[index++] = {output.enabled,output.blackout,output.freeze};
                }
                std::lock_guard lock{input_mutex};pending = update;has_pending = true;
                if (changed) request = Request{desired_generation,checked_revision,current};
            }
        }
        Status observed;{std::lock_guard lock{status_mutex};observed = status;}
        if (observed.failed) {
            if (!failure_reported) {service.renderer_failed(1,"native_render_owner_failed");std::cerr << "Native program failed: " << observed.error << '\n';failure_reported = true;}
        } else if (observed.frame.sequence > observed_frame) {
            std::string reason = observed.preparation_reason;
            if (observed.scene_generation != desired_generation) {
                std::lock_guard lock{input_mutex};reason = failed_generation == desired_generation ? "render_state_requires_preparation" : "render_state_preparing";
            }
            service.report_rendered({1,observed.frame.revision,observed.frame.sequence,observed.frame.show_time},
                std::move(reason));observed_frame = observed.frame.sequence;
        }
        return observed.finished || (options.frame_limit && observed.failed);
    }
};
NativeProgram::NativeProgram(protocol::ControlService& service,ProgramOptions options) : impl_(std::make_unique<Impl>(service,std::move(options))) {}
NativeProgram::~NativeProgram() = default;
bool NativeProgram::poll() {return impl_->poll();}
void NativeProgram::finish() {
    impl_->stop();std::lock_guard lock{impl_->status_mutex};
    if (impl_->status.failed) throw render::Error(impl_->status.error);
}
}
