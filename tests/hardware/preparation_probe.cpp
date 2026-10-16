// SPDX-License-Identifier: GPL-3.0-or-later
#include "adapters/windows/composition_renderer.hpp"
#include "adapters/windows/preparation_device.hpp"
#include <mapgyver/build_info.hpp>
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <cstring>
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
    return {{"schemaVersion",1},{"evidenceClass","native-hardware-owned-D3D11-device-concurrent-image-preparation"},
        {"vendorId",gpu.VendorId},{"deviceId",gpu.DeviceId},{"luidHigh",static_cast<std::uint32_t>(gpu.AdapterLuid.HighPart)},{"luidLow",gpu.AdapterLuid.LowPart},
        {"deviceCreationFlags",device->native()->GetCreationFlags()},{"driverConcurrentCreates",threading.DriverConcurrentCreates != FALSE},
        {"driverCommandLists",threading.DriverCommandLists != FALSE},{"framesSubmitted",frames},{"backgroundImageGenerations",completed.load()},
        {"preparedAtFirstFrame",first_count},{"preparedAtLastFrame",last_count},{"retainedGenerationCapacity",retained_capacity},
        {"retainedImagePayloadCapacityBytes",retained_capacity*128*64*sizeof(render::Pixel)},
        {"backgroundCpuUploadBytes",generations*128*64*sizeof(render::Pixel)},{"backgroundGpuCopySubmissions",0},
        {"invalidRequestsRejected",rejections},{"postCompositorShutdownPreparation",true},
        {"activeOutputMaxChannelError",maximum},{"activeOutputTolerance",2},{"backgroundImageBitwiseFloatReadback",true},
        {"validationCpuReadbackBytes",readback_bytes+128*64*4},{"validationGpuCopySubmissions",2},
        {"scope","Actual owned hardware device and immutable image CPU uploads concurrent with native composition frames. Immediate context/bgfx remain on their render owner. Final offline GPU completion/readbacks only. No runtime image-generation adoption, graph/window handoff, deadline budget, physical scanout or device-loss qualification."}};
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
