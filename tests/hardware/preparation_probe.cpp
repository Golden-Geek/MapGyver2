// SPDX-License-Identifier: GPL-3.0-or-later
#include "adapters/windows/composition_renderer.hpp"
#include "adapters/windows/preparation_device.hpp"
#include <mapgyver/build_info.hpp>
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <thread>

using namespace mapgyver;
using namespace domain;
namespace {
void check(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
void require(HRESULT result,const char* message) {check(SUCCEEDED(result),message);}
std::shared_ptr<const render::Plan> fixture(const std::shared_ptr<const render::Image>& image) {
    Definitions definitions;
    definitions.assets.emplace(Id{"pattern"},Asset{Id{"pattern"},"Pattern",ExternalSource{"mapgyver.pattern","gradient",{{Id{"picture"},"Picture",StreamKind::video}}}});
    auto project = make_default_project(std::move(definitions));
    auto library = project.instances.definitions().data();
    for (auto& [id,composition] : library.compositions) {
        static_cast<void>(id);
        for (auto& [port,canvas] : composition.canvases) {static_cast<void>(port);canvas.width = 128;canvas.height = 64;}
    }
    static_cast<void>(project.instances.reconcile_definitions(DefinitionSnapshot{std::move(library)}));
    static_cast<void>(project.instances.bind_input(project.root_instance,project.content_input,{Id{"pattern"},Id{"picture"}},false));
    control::ProjectController controller{std::move(project)};render::Images images;
    for (const auto& [id,instance] : controller.snapshot()->instances.instances())
        if (instance.asset == Id{"pattern"}) images.emplace(render::PortKey{id,Id{"picture"}},image);
    return render::compile(*controller.snapshot(),0,images);
}
// Offline only, on the same immediate-context owner after preparation joins.
std::uint64_t verify_native(windows::PreparationDevice& device,const windows::PreparedImage& image,const render::Image& expected) {
    D3D11_TEXTURE2D_DESC description{};image.texture->GetDesc(&description);
    check(description.Usage == D3D11_USAGE_IMMUTABLE && description.Format == DXGI_FORMAT_R32G32B32A32_FLOAT &&
        description.Width == expected.width && description.Height == expected.height,"Prepared native resource description differs");
    description.Usage = D3D11_USAGE_STAGING;description.BindFlags = 0;description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    require(device.native()->CreateTexture2D(&description,nullptr,&staging),"Allocate offline prepared image staging failed");
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;device.native()->GetImmediateContext(&context);
    context->CopyResource(staging.Get(),image.texture.Get());
    const D3D11_QUERY_DESC query_description{D3D11_QUERY_EVENT,0};Microsoft::WRL::ComPtr<ID3D11Query> query;
    require(device.native()->CreateQuery(&query_description,&query),"Allocate offline preparation completion failed");
    context->End(query.Get());context->Flush();const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds{2};
    for (;;) {
        BOOL done = FALSE;const auto result = context->GetData(query.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        require(result,"Offline prepared image completion failed");if (result == S_OK && done) break;
        check(std::chrono::steady_clock::now() < deadline,"Offline prepared image completion timed out");std::this_thread::yield();
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};require(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Map offline prepared image failed");
    bool equal = true;
    for (UINT y = 0; y < expected.height; ++y) equal &= std::memcmp(static_cast<const std::uint8_t*>(mapped.pData)+static_cast<std::size_t>(y)*mapped.RowPitch,
        expected.pixels->data()+static_cast<std::size_t>(y)*expected.width,static_cast<std::size_t>(expected.width)*sizeof(render::Pixel)) == 0;
    context->Unmap(staging.Get(),0);
    check(equal,"Background prepared native float pixels differ bitwise from supplied generation");
    return static_cast<std::uint64_t>(expected.width)*expected.height*sizeof(render::Pixel);
}
nlohmann::json handoff(const std::shared_ptr<windows::PreparationDevice>& device,const std::shared_ptr<const render::Plan>& plan,
    const std::shared_ptr<const render::Image>& gradient,const std::shared_ptr<const render::Image>& bars) {
    const Id output{"handoff-window"};
    windows::CompositionRenderer renderer{plan,{{output,"Image handoff",128,64,plan->preview,true,{}}},device};
    const auto payload = renderer.resource_payload_bytes();const auto owner = std::this_thread::get_id();
    const auto next = [&](std::uint64_t generation,const std::shared_ptr<const render::Image>& source) {
        auto image = std::make_shared<render::Image>(*source);image->generation = generation;
        auto value = std::make_shared<render::Plan>(*plan);value->revision = generation;
        for (auto& node : value->nodes) if (node.image) node.image = image;
        return std::shared_ptr<const render::Plan>{std::move(value)};
    };
    const auto second_plan = next(2,bars), third_plan = next(3,gradient);
    auto preparation = std::async(std::launch::async,[&] {
        check(std::this_thread::get_id() != owner,"Live candidates did not prepare on another thread");
        auto second = windows::prepare_generation(*device,*plan,second_plan,2,payload);
        auto third = windows::prepare_generation(*device,*plan,third_plan,3,payload);
        return std::pair{std::move(second),std::move(third)};
    });
    std::uint64_t frames = 0;std::int64_t ticks = -1;unsigned captures = 0, maximum = 0, rejections = 0;
    const auto frame = [&](std::uint64_t revision) {
        const auto applied = renderer.frame();check(applied.sequence == ++frames && applied.revision == revision &&
            applied.show_time.ticks >= ticks,"Live image handoff regressed frame/revision/show clock");ticks = applied.show_time.ticks;
    };
    auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds{2};
    do {
        frame(0);check(std::chrono::steady_clock::now() < deadline,"Background live candidate preparation timed out");
    } while (preparation.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready);
    auto [second,third] = preparation.get();
    unsigned foreign_owner_rejections = 0;
    std::jthread foreign_owner{[&] {
        try {static_cast<void>(renderer.apply_images(third));} catch (const render::Error&) {++foreign_owner_rejections;}
        try {static_cast<void>(renderer.retire_images());} catch (const render::Error&) {++foreign_owner_rejections;}
    }};foreign_owner.join();
    check(foreign_owner_rejections == 2 && third,"Native image handoff/completion accepted a foreign owner");
    renderer.output_controls(output,{true,false,true}); // Holds the actual initial gradient.
    check(renderer.apply_images(second) && !second,"Prepared image ownership was not transferred");
    check(!renderer.apply_images(third) && third && third->generation_id() == 3,"Busy copy did not preserve candidate ownership/backpressure");
    const auto retire = [&](std::uint64_t revision) {
        auto until = std::chrono::steady_clock::now()+std::chrono::seconds{2};
        std::unique_ptr<windows::PreparedGeneration> retired;
        do {
            frame(revision);retired = renderer.retire_images();
            check(std::chrono::steady_clock::now() < until,"Nonblocking live image retirement did not complete");
        } while (!retired);
        auto cleanup = std::async(std::launch::async,[owner,value=std::move(retired)]() mutable {
            check(std::this_thread::get_id() != owner,"Retired generation disposal remained on the render owner");
            const auto generation = value->generation_id();value.reset();return generation;
        });
        check(cleanup.wait_for(std::chrono::seconds{2}) == std::future_status::ready && cleanup.get() == revision,
            "Completed generation did not retire on preparation thread");
    };
    const auto compare = [&](std::vector<std::uint8_t> actual,std::vector<std::uint8_t> expected) {
        check(actual.size() == expected.size(),"Live generation golden dimensions differ");
        for (std::size_t i = 0; i < actual.size(); ++i) maximum = std::max(maximum,
            static_cast<unsigned>(std::abs(static_cast<int>(actual[i])-expected[i])));
        check(maximum <= 2,"Live generation/freeze/blackout pixels differ from reference");++captures;
    };
    retire(2);
    compare(renderer.capture_for_test(plan->preview),render::reference(*second_plan,plan->preview));
    compare(renderer.capture_output_for_test(output),render::reference(*plan,plan->preview,true));
    renderer.output_controls(output,{true,false,false});frame(2);
    compare(renderer.capture_output_for_test(output),render::reference(*second_plan,plan->preview,true));
    renderer.output_controls(output,{true,true,false});
    check(renderer.apply_images(third) && !third,"Second prepared generation did not apply after actual retirement");
    retire(3);
    compare(renderer.capture_for_test(plan->preview),render::reference(*third_plan,plan->preview));
    std::vector<std::uint8_t> black(128*64*4,0);for (std::size_t i = 3; i < black.size(); i += 4) black[i] = 255;
    compare(renderer.capture_output_for_test(output),std::move(black));
    renderer.output_controls(output,{true,false,false});frame(3);
    compare(renderer.capture_output_for_test(output),render::reference(*third_plan,plan->preview,true));
    const auto reject_apply = [&](std::unique_ptr<windows::PreparedGeneration> candidate) {
        try {static_cast<void>(renderer.apply_images(candidate));} catch (const render::Error&) {check(candidate != nullptr,"Rejected candidate ownership was lost");++rejections;return;}
        throw std::runtime_error("Stale/foreign native candidate was applied");
    };
    reject_apply(windows::prepare_generation(*device,*plan,next(4,bars),2,payload)); // Old scene generation.
    reject_apply(windows::prepare_generation(*device,*plan,next(2,bars),4,payload)); // Old resource generation.
    auto foreign = std::make_shared<windows::PreparationDevice>();
    reject_apply(windows::prepare_generation(*foreign,*plan,next(4,bars),4,payload)); // Same GPU, another device.
    const auto reject_prepare = [&](std::shared_ptr<const render::Plan> value,std::uint64_t active) {
        try {static_cast<void>(windows::prepare_generation(*device,*plan,std::move(value),4,active));}
        catch (const render::Error&) {++rejections;return;}throw std::runtime_error("Incompatible/over-capacity generation prepared");
    };
    auto resized = std::make_shared<render::Plan>(*third_plan);++resized->nodes.back().width;
    reject_prepare(resized,payload);reject_prepare(next(4,bars),536870912);
    frame(3);const auto state = renderer.image_handoff_state();
    check(state.generation == 3 && state.gpu_copy_submissions == 2 && state.copied_payload_bytes == 2*128*64*sizeof(render::Pixel),
        "Rejected/busy candidates altered generation/copy accounting");
    return {{"framesSubmitted",frames},{"appliedGeneration",state.generation},{"appliedRevision",3},
        {"gpuCopySubmissions",state.gpu_copy_submissions},{"copiedPayloadBytes",state.copied_payload_bytes},
        {"backgroundAcceptedCandidateCpuUploadBytes",2*128*64*sizeof(render::Pixel)},
        {"initialSourceCpuUploadBytes",plan->image_upload_bytes},{"validationRejectedCandidateCpuUploadBytes",3*128*64*sizeof(render::Pixel)},
        {"candidateOwnershipBackpressureVerified",true},{"actualCompletionBeforeRetirement",true},{"retirementOnPreparationThread",true},
        {"staleForeignLayoutCapacityRequestsRejected",rejections},{"captures",captures},{"maxChannelError",maximum},{"tolerance",2},
        {"foreignOwnerRequestsRejected",foreign_owner_rejections},
        {"validationCpuReadbackBytes",captures*128*64*4},{"validationGpuCopySubmissions",captures},
        {"scope","Actual fixed-layout live source image copies on the native render owner, background prepared immutable generations, monotonic frame/revision/clock, freeze/unfreeze/blackout, nonblocking completion and ownership retirement. Hidden-window intermediate/offscreen captures only; no engine control integration or graph/window resource handoff."}};
}
nlohmann::json run() {
    auto device = std::make_shared<windows::PreparationDevice>();
    const auto gradient = render::pattern(Id{"prepared"},1,128,64,"gradient");
    const auto bars = render::pattern(Id{"prepared"},1,128,64,"bars");
    const auto plan = fixture(gradient);
    unsigned rejections = 0;
    const auto reject = [&](const render::Image& image) {
        try {static_cast<void>(device->prepare(image));} catch (const render::Error&) {++rejections;return;}
        throw std::runtime_error("Invalid native preparation request was accepted");
    };
    auto invalid = *gradient;invalid.generation = 0;reject(invalid);
    invalid = *gradient;invalid.width = 16385;reject(invalid);
    invalid = *gradient;invalid.pixels = std::make_shared<const std::vector<render::Pixel>>(1);reject(invalid);
    invalid = *gradient;invalid.width = 16384;invalid.height = 16384;reject(invalid);
    invalid = *gradient;auto corrupt = std::make_shared<std::vector<render::Pixel>>(*gradient->pixels);
    corrupt->front().r = 2;invalid.pixels = corrupt;reject(invalid);

    constexpr std::uint64_t generations = 128, retained_capacity = 8;
    std::vector<windows::PreparedImage> retained;retained.reserve(retained_capacity);
    std::atomic<std::uint64_t> completed{0}, frame_sequence{0};
    std::exception_ptr preparation_failure;
    std::uint64_t first_count = 0, last_count = 0, frames = 0, readback_bytes = 0;
    unsigned maximum = 0;std::int64_t last_ticks = -1;
    const auto owner = std::this_thread::get_id();
    {
        windows::CompositionRenderer renderer{plan,{},device};
        std::jthread preparation{[&](std::stop_token stop) {
            try {
                check(std::this_thread::get_id() != owner,"Preparation did not run on another thread");
                const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds{2};
                while (!frame_sequence.load() && !stop.stop_requested()) {
                    check(std::chrono::steady_clock::now() < deadline,"Native owner did not start its frame loop");std::this_thread::yield();
                }
                for (std::uint64_t generation = 1; generation <= generations && !stop.stop_requested(); ++generation) {
                    auto source = *(generation%2 ? gradient : bars);source.generation = generation;
                    auto image = device->prepare(source);
                    check(image.generation == generation && image.resource == source.resource,"Native resource generation identity differs");
                    if (retained.size() < retained_capacity) retained.push_back(std::move(image));
                    else retained[(generation-1)%retained_capacity] = std::move(image);
                    completed.store(generation);
                    std::this_thread::sleep_for(std::chrono::milliseconds{1});
                }
            } catch (...) {preparation_failure = std::current_exception();}
        }};
        for (unsigned frame = 0; frame < 120; ++frame) {
            const auto applied = renderer.frame();
            check(applied.sequence == frame+1 && applied.revision == 0 && applied.show_time.ticks >= last_ticks,
                "Background preparation disrupted native frame/revision/clock continuity");
            last_ticks = applied.show_time.ticks;frame_sequence.store(applied.sequence);frames = applied.sequence;
            if (frame == 0) first_count = completed.load();
            last_count = completed.load();std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        preparation.join();if (preparation_failure) std::rethrow_exception(preparation_failure);
        check(completed.load() == generations && last_count > first_count,"No background native allocation progressed while frames advanced");
        const auto actual = renderer.capture_for_test(plan->preview), expected = render::reference(*plan,plan->preview);
        check(actual.size() == expected.size(),"Concurrent preparation golden dimensions differ");
        for (std::size_t i = 0; i < actual.size(); ++i)
            maximum = std::max(maximum,static_cast<unsigned>(std::abs(static_cast<int>(actual[i])-expected[i])));
        check(maximum <= 2,"Background native creation changed active composition pixels");
        const auto& final = retained[(generations-1)%retained_capacity];
        check(final.generation == generations,"Bounded native generation ring did not retain latest prepared generation");
        readback_bytes = verify_native(*device,final,*bars);
    } // bgfx shutdown while the explicitly owned device/unused generations survive.
    auto after_shutdown = *gradient;after_shutdown.generation = generations+1;
    const auto survivor = device->prepare(after_shutdown);
    check(survivor.generation == generations+1,"Owned device did not survive compositor shutdown");
    const auto& gpu = device->identity();const auto threading = device->threading();
    auto live = handoff(device,plan,gradient,bars);
    {
        // Leave a real copy unretired: shutdown must retain its candidate and
        // explicitly drain completion before native ownership is disposed.
        windows::CompositionRenderer renderer{plan,{},device};
        const auto payload = renderer.resource_payload_bytes();
        auto prepare = std::async(std::launch::async,[&] {
            auto source = std::make_shared<render::Image>(*bars);source->generation = 2;
            auto value = std::make_shared<render::Plan>(*plan);value->revision = 2;
            for (auto& node : value->nodes) if (node.image) node.image = source;
            return windows::prepare_generation(*device,*plan,std::move(value),2,payload);
        });
        check(prepare.wait_for(std::chrono::seconds{2}) == std::future_status::ready,"Pending-shutdown preparation timed out");
        auto candidate = prepare.get();check(renderer.apply_images(candidate) && !candidate,"Pending-shutdown copy was not submitted");
    }
    live["shutdownWithPendingCopy"] = true;live["shutdownProbeGpuCopySubmissions"] = 1;
    live["shutdownProbeCpuUploadBytes"] = 2*128*64*sizeof(render::Pixel);
    nlohmann::json borrowed;
    {
        windows::CompositionRenderer renderer{plan,{},device};
        const auto before = renderer.frame();
        auto preparation = std::async(std::launch::async,[device] {return device->target(257,131,DXGI_FORMAT_R8G8B8A8_UNORM);});
        const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds{2};
        while (preparation.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) {
            static_cast<void>(renderer.frame());check(std::chrono::steady_clock::now() < deadline,"Background native target preparation timed out");
        }
        const auto actual = renderer.qualify_borrowed_target(preparation.get());
        const auto after = renderer.frame();check(after.sequence >= before.sequence+5 && after.show_time.ticks > before.show_time.ticks,
            "Old native scene/clock did not continue through target binding");
        const auto expected = render::reference(*plan,plan->preview);const auto& node = plan->nodes[plan->preview];unsigned target_maximum = 0;
        check(actual.size() == 257*131*4,"Borrowed target did not capture actual native dimensions");
        for (std::uint32_t y = 0; y < 131; ++y) for (std::uint32_t x = 0; x < 257; ++x) {
            const auto sx = (static_cast<std::uint64_t>(x)*2+1)*node.width/(257*2);
            const auto sy = (static_cast<std::uint64_t>(y)*2+1)*node.height/(131*2);
            for (std::size_t c = 0; c < 4; ++c) target_maximum = std::max(target_maximum,static_cast<unsigned>(std::abs(
                static_cast<int>(actual[(static_cast<std::size_t>(y)*257+x)*4+c])-expected[static_cast<std::size_t>((sy*node.width+sx)*4)+c])));
        }
        if (target_maximum > 2) {
            std::size_t nonzero = 0;for (std::size_t i = 0; i < actual.size(); i += 4) if (actual[i] || actual[i+1] || actual[i+2]) ++nonzero;
            std::cerr << "Borrowed target max error=" << target_maximum << " nonzero RGB pixels=" << nonzero << " of " << 257*131 << '\n';
        }
        check(target_maximum <= 2,"Borrowed resized target differs from fixed colour/viewport reference");
        const auto old = renderer.capture_for_test(plan->preview);
        for (std::size_t i = 0; i < old.size(); ++i) target_maximum = std::max(target_maximum,static_cast<unsigned>(std::abs(static_cast<int>(old[i])-expected[i])));
        check(target_maximum <= 2,"Borrowed target binding changed the old active scene");
        borrowed = {{"width",257},{"height",131},{"oldFramesAdvanced",after.sequence-before.sequence},{"maxChannelError",target_maximum},{"tolerance",2},
            {"backgroundNativeTargetPayloadBytes",257*131*4},{"ownerPlaceholderPayloadBytes",4},
            {"ownerImportSrvCreations",1},{"ownerFramebufferRtvCreations",1},{"validationCpuReadbackBytes",actual.size()+old.size()},
            {"validationGpuCopySubmissions",2},{"scope","Real background D3D11 target allocation, owner 1x1 bgfx placeholder/import/RTV binding with old scene frames continuing, resized target and unchanged-scene GPU goldens. Not complete graph/window handoff or deadline-budget qualification."}};
    }
    return {{"schemaVersion",1},{"evidenceClass","native-hardware-owned-D3D11-device-concurrent-image-preparation"},{"liveImageHandoff",std::move(live)},
        {"borrowedRenderTarget",std::move(borrowed)},
        {"vendorId",gpu.VendorId},{"deviceId",gpu.DeviceId},{"luidHigh",static_cast<std::uint32_t>(gpu.AdapterLuid.HighPart)},{"luidLow",gpu.AdapterLuid.LowPart},
        {"deviceCreationFlags",device->native()->GetCreationFlags()},{"driverConcurrentCreates",threading.DriverConcurrentCreates != FALSE},
        {"driverCommandLists",threading.DriverCommandLists != FALSE},{"framesSubmitted",frames},{"backgroundImageGenerations",completed.load()},
        {"preparedAtFirstFrame",first_count},{"preparedAtLastFrame",last_count},{"retainedGenerationCapacity",retained_capacity},
        {"retainedImagePayloadCapacityBytes",retained_capacity*128*64*sizeof(render::Pixel)},
        {"backgroundCpuUploadBytes",generations*128*64*sizeof(render::Pixel)},{"backgroundGpuCopySubmissions",0},
        {"deviceSuppliedCpuUploadBytesTotal",device->cpu_upload_bytes()},
        {"invalidRequestsRejected",rejections},{"postCompositorShutdownPreparation",true},
        {"activeOutputMaxChannelError",maximum},{"activeOutputTolerance",2},{"backgroundImageBitwiseFloatReadback",true},
        {"validationCpuReadbackBytes",readback_bytes+128*64*4},{"validationGpuCopySubmissions",2},
        {"scope","Actual owned hardware device and immutable image CPU uploads concurrent with native composition frames, plus fixed-layout live image handoff below. Immediate context/bgfx remain on their render owner. Synchronous readbacks only offline. No engine control integration, graph/window handoff, deadline budget, physical scanout or device-loss qualification."}};
}
}
int main(int argc,char** argv) {
    if (argc == 2 && std::string_view{argv[1]} == "--version") {std::cout << "MapGyver owned-device image preparation qualification\n";return 0;}
    if (argc != 2 || std::string_view{argv[1]} != "--json") {std::cerr << "Usage: mapgyver-preparation-probe --json\n";return 2;}
    try {
        auto report = run();const auto build = build_info();report["sourceRevision"] = build.revision;
        report["sourceDirty"] = build.dirty ? nlohmann::json(*build.dirty) : nlohmann::json(nullptr);report["configuration"] = build.configuration;
        std::cout << report.dump(2) << '\n';return std::cout ? 0 : 1;
    } catch (const std::exception& error) {std::cerr << "Native preparation qualification failed: " << error.what() << '\n';return 1;}
}
