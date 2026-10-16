// SPDX-License-Identifier: GPL-3.0-or-later
#include "composition_renderer.hpp"
#include "preparation_device.hpp"
#include "prepared_graph.hpp"
#include "prepared_windows.hpp"
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <SDL3/SDL.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include "render_shaders.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <algorithm>
#include <cstring>
#include <set>
#include <utility>

namespace mapgyver::windows {
namespace {
using Microsoft::WRL::ComPtr;
void check(bool value,const char* message) {if (!value) throw render::Error(message);}
void require(HRESULT result,const char* message) {if (FAILED(result)) throw render::Error(message);}
constexpr std::uint64_t sampling = BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP;
std::uint64_t visual_state(domain::BlendMode mode) {
    constexpr auto write = BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A;
    if (mode == domain::BlendMode::add) return write|BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_INV_SRC_ALPHA);
    if (mode == domain::BlendMode::screen) return write|BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_INV_SRC_COLOR,BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_INV_SRC_ALPHA);
    return write|BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_INV_SRC_ALPHA);
}
struct Callbacks : bgfx::CallbackI {
    std::array<char,65536> trace{}; std::size_t used = 0;
    void fatal(const char*,std::uint16_t,bgfx::Fatal::Enum,const char* message) override {
        std::fputs(message,stderr); if (used) std::fwrite(trace.data(),1,used,stderr); std::_Exit(1);
    }
    void traceVargs(const char*,std::uint16_t,const char* format,va_list args) override {
        if (used+1 >= trace.size()) return;
        const auto size = std::vsnprintf(trace.data()+used,trace.size()-used,format,args);
        if (size > 0) used += std::min(static_cast<std::size_t>(size),trace.size()-used-1);
    }
    void profilerBegin(const char*,std::uint32_t,const char*,std::uint16_t) override {}
    void profilerBeginLiteral(const char*,std::uint32_t,const char*,std::uint16_t) override {}
    void profilerEnd() override {}
    std::uint32_t cacheReadSize(std::uint64_t) override {return 0;}
    bool cacheRead(std::uint64_t,void*,std::uint32_t) override {return false;}
    void cacheWrite(std::uint64_t,const void*,std::uint32_t) override {}
    void screenShot(const char*,std::uint32_t,std::uint32_t,std::uint32_t,const void*,std::uint32_t,bool) override {}
    void captureBegin(std::uint32_t,std::uint32_t,std::uint32_t,bgfx::TextureFormat::Enum,bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*,std::uint32_t) override {}
};
using Texture = GraphTexture;
std::vector<std::uint8_t> readback(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* texture) {
    D3D11_TEXTURE2D_DESC description{};texture->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;description.BindFlags = 0;description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;description.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;require(device->CreateTexture2D(&description,nullptr,&staging),"Offline compositor staging allocation failed");
    context->CopyResource(staging.Get(),texture);
    D3D11_QUERY_DESC query_description{D3D11_QUERY_EVENT,0};ComPtr<ID3D11Query> completion;require(device->CreateQuery(&query_description,&completion),"Offline compositor query allocation failed");
    context->End(completion.Get());context->Flush();const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds{2};
    for (;;) {
        BOOL done = FALSE;const auto status = context->GetData(completion.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);require(status,"Offline compositor completion failed");
        if (status == S_OK && done) break;check(std::chrono::steady_clock::now() < deadline,"Offline compositor completion timed out");std::this_thread::yield();
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};require(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Offline compositor map failed");
    struct Unmap {ID3D11DeviceContext* context;ID3D11Resource* resource;~Unmap(){context->Unmap(resource,0);}} unmap{context,staging.Get()};
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(description.Width)*description.Height*4);
    for (UINT y = 0; y < description.Height; ++y) std::memcpy(pixels.data()+static_cast<std::size_t>(y)*description.Width*4,static_cast<const std::uint8_t*>(mapped.pData)+static_cast<std::size_t>(y)*mapped.RowPitch,description.Width*4);
    require(device->GetDeviceRemovedReason(),"Native compositor device removed");return pixels;
}
}
struct CompositionRenderer::Impl : GraphResources {
    using Output = NativeOutput;
    Callbacks callbacks;
    SDL_Window* window = nullptr;
    bool initialized = false;
    std::shared_ptr<PreparationDevice> preparation_device;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Query> image_completion;
    std::unique_ptr<PreparedGeneration> image_copy;
    std::unique_ptr<PreparedGraph> old_graph;
    std::unique_ptr<PreparedWindows> old_windows;
    ImageHandoffState image_state{1,0,0};
    Texture* qualification_target = nullptr;
    std::array<std::uint16_t,2> qualification_size{};
    DXGI_ADAPTER_DESC1 identity{};
    bgfx::ProgramHandle composite = BGFX_INVALID_HANDLE, encode = BGFX_INVALID_HANDLE, mapping = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE, encoding = BGFX_INVALID_HANDLE, resample = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle mapping_rows = BGFX_INVALID_HANDLE, mapping_edges = BGFX_INVALID_HANDLE, mapping_identity = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle layer_visual = BGFX_INVALID_HANDLE, surface_rows = BGFX_INVALID_HANDLE, crop = BGFX_INVALID_HANDLE,
        mask_bounds = BGFX_INVALID_HANDLE, patch_visual = BGFX_INVALID_HANDLE, content_rows = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle transparent = BGFX_INVALID_HANDLE;
    using Input = GraphInput;
    std::vector<Output> outputs;
    std::uint64_t sequence = 0, draws = 0;
    std::uint64_t applied_revision = 0;
    std::chrono::steady_clock::time_point start;
    const std::thread::id owner = std::this_thread::get_id();
    explicit Impl(std::shared_ptr<const render::Plan> value,std::vector<OutputWindow> windows,std::shared_ptr<PreparationDevice> owned_device)
        : preparation_device(std::move(owned_device)) {
        plan = std::move(value);
        if (plan) applied_revision = plan->revision;
        check(windows.size() <= 64,"Native output window count exceeds configured capacity");
        std::set<domain::Id> ids;
        outputs.reserve(windows.size());
        for (auto& configured : windows) {
            check(ids.insert(configured.id).second,"Native output window identities must be unique");
            outputs.push_back(Output{std::move(configured)});
        }
    }
    void check_owner() const {check(std::this_thread::get_id() == owner,"Native compositor must use its GPU owner thread");}
    Output& output(const domain::Id& id) {
        check_owner();
        const auto found = std::find_if(outputs.begin(),outputs.end(),[&](const auto& item){return item.configured.id == id;});
        check(found != outputs.end(),"Native output identity is not prepared");return *found;
    }
    void create_window(Output& output) {
        const auto& config = output.configured;
        output.window = SDL_CreateWindow(config.name.c_str(),static_cast<int>(config.width),static_cast<int>(config.height),
            SDL_WINDOW_HIDDEN|(config.desktop_position ? SDL_WINDOW_BORDERLESS : 0));
        check(output.window != nullptr,SDL_GetError());
        if (config.desktop_position) {
            check(SDL_SetWindowPosition(output.window,(*config.desktop_position)[0],(*config.desktop_position)[1]),SDL_GetError());
            auto* handle = SDL_GetPointerProperty(SDL_GetWindowProperties(output.window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr);
            RECT rectangle{};check(handle && GetWindowRect(static_cast<HWND>(handle),&rectangle) != FALSE,"Native display rectangle query failed");
            check(rectangle.left == (*config.desktop_position)[0] && rectangle.top == (*config.desktop_position)[1] &&
                rectangle.right-rectangle.left == static_cast<LONG>(config.width) && rectangle.bottom-rectangle.top == static_cast<LONG>(config.height),
                "Native display window pixel rectangle differs from explicit target binding");
        }
    }
    static void destroy_output_handles(Output& output) {
        if (bgfx::isValid(output.swapchain)) {bgfx::destroy(output.swapchain);output.swapchain = BGFX_INVALID_HANDLE;}
        for (auto* texture : {&output.mapped,&output.held,&output.presented}) {
            if (bgfx::isValid(texture->framebuffer)) {bgfx::destroy(texture->framebuffer);texture->framebuffer = BGFX_INVALID_HANDLE;}
            if (bgfx::isValid(texture->texture)) {bgfx::destroy(texture->texture);texture->texture = BGFX_INVALID_HANDLE;}
        }
    }
    bool bind_windows(PreparedWindows& candidate) {
        if (candidate.last_step_frame == sequence) return false;candidate.last_step_frame = sequence;
        while (candidate.cursor < candidate.outputs.size() && candidate.reuse[candidate.cursor] != SIZE_MAX) ++candidate.cursor;
        if (candidate.cursor == candidate.outputs.size()) return true;
        auto& output = candidate.outputs[candidate.cursor];
        if (candidate.step == 0) create_window(output);
        else if (candidate.step == 1) {
            auto* handle = SDL_GetPointerProperty(SDL_GetWindowProperties(output.window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr);
            check(handle != nullptr,"Prepared output HWND unavailable");
            output.swapchain = bgfx::createFrameBuffer(handle,static_cast<std::uint16_t>(output.configured.width),static_cast<std::uint16_t>(output.configured.height));
            check(bgfx::isValid(output.swapchain),"Prepared output swapchain handle failed");
        } else {
            auto& texture = candidate.step < 4 ? output.mapped : candidate.step < 6 ? output.held : output.presented;
            const auto format = candidate.step < 4 ? bgfx::TextureFormat::RGBA16F : bgfx::TextureFormat::RGBA8;
            if (candidate.step%2 == 0) {
                texture.texture = bgfx::createTexture2D(1,1,false,1,format,BGFX_TEXTURE_RT|sampling);
                check(bgfx::isValid(texture.texture),"Prepared output placeholder failed");
            } else {
                const auto pointer = reinterpret_cast<std::uintptr_t>(texture.native.Get());
                check(bgfx::overrideInternal2D(texture.texture,pointer,static_cast<std::uint16_t>(output.configured.width),
                    static_cast<std::uint16_t>(output.configured.height)) == pointer,"Prepared output texture import failed");
                texture.framebuffer = bgfx::createFrameBuffer(1,&texture.texture,false);
                check(bgfx::isValid(texture.framebuffer),"Prepared output framebuffer handle failed");
            }
        }
        if (++candidate.step == 8) {candidate.step = 0;++candidate.cursor;}
        return false; // Realize queued owner/DXGI work with the old scene still active.
    }
    void apply_windows(std::unique_ptr<PreparedWindows>& candidate) {
        for (std::size_t i = 0; i < candidate->outputs.size(); ++i) {
            auto& next = candidate->outputs[i];const auto reuse = candidate->reuse[i];
            if (reuse != SIZE_MAX) {
                check(reuse < outputs.size(),"Prepared window reuse index invalid");auto& prior = outputs[reuse];
                next.window = std::exchange(prior.window,nullptr);std::swap(next.swapchain,prior.swapchain);
                std::swap(next.mapped,prior.mapped);std::swap(next.held,prior.held);std::swap(next.presented,prior.presented);std::swap(next.held_frame,prior.held_frame);
                next.show_pending = !next.configured.hidden && next.configured.controls.enabled && !prior.configured.controls.enabled;
                if (!next.configured.hidden && !next.configured.controls.enabled) check(SDL_HideWindow(next.window),SDL_GetError());
            } else {
                const auto prior = std::find_if(outputs.begin(),outputs.end(),[&](const auto& old){return old.configured.id == next.configured.id;});
                if (prior != outputs.end() && next.configured.controls.freeze && prior->held_frame) {
                    next.freeze_source = prior->held.texture;next.held_frame = prior->held_frame;
                    next.freeze_source_size = {prior->configured.width,prior->configured.height};
                }
                next.show_pending = !next.configured.hidden && next.configured.controls.enabled;
            }
        }
        outputs.swap(candidate->outputs);candidate->cursor = 0;candidate->step = 0;candidate->retiring = true;
        candidate->last_step_frame = UINT64_MAX;candidate->hide_pending = true;old_windows = std::move(candidate);
    }
    ~Impl() {
        if (initialized) {
            if (old_windows) {
                if (!old_windows->fence_submitted) {context->End(old_windows->completion.Get());old_windows->fence_submitted = true;}
                context->Flush();const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds{2};
                for (;;) {
                    BOOL done = FALSE;const auto result = context->GetData(old_windows->completion.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);
                    if (result == S_OK && done) break;
                    if (FAILED(result) || std::chrono::steady_clock::now() >= deadline) {
                        std::fputs("Fatal: window shutdown completion failed/timed out\n",stderr);std::_Exit(1);
                    }
                    std::this_thread::yield();
                }
                for (auto& output : old_windows->outputs) destroy_output_handles(output);
            }
            if (old_graph) {
                context->Flush();const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds{2};
                for (;;) {
                    BOOL done = FALSE;const auto result = context->GetData(old_graph->completion.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);
                    if (result == S_OK && done) break;
                    if (FAILED(result) || std::chrono::steady_clock::now() >= deadline) {
                        std::fputs("Fatal: graph shutdown completion failed/timed out\n",stderr);std::_Exit(1);
                    }
                    std::this_thread::yield();
                }
                for (std::size_t i = 0; i < old_graph->binding_count; ++i) {
                    auto& binding = old_graph->bindings[i];
                    if (binding.framebuffer && bgfx::isValid(*binding.framebuffer)) bgfx::destroy(*binding.framebuffer);
                    if (bgfx::isValid(*binding.handle)) bgfx::destroy(*binding.handle);
                }
            }
            if (image_copy) {
                // Shutdown only, outside program deadlines. Keep candidate/native
                // owners until the actual copy completes, or OS cleanup on fatal.
                context->Flush();const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds{2};
                for (;;) {
                    BOOL done = FALSE;const auto result = context->GetData(image_completion.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);
                    if (result == S_OK && done) break;
                    if (FAILED(result) || std::chrono::steady_clock::now() >= deadline) {
                        std::fputs("Fatal: native image copy shutdown completion failed/timed out\n",stderr);std::_Exit(1);
                    }
                    std::this_thread::yield();
                }
            }
            for (auto& output : outputs) {
                if (bgfx::isValid(output.swapchain)) bgfx::destroy(output.swapchain);
                for (auto* t : {&output.mapped,&output.held,&output.presented}) {
                    if (bgfx::isValid(t->framebuffer)) bgfx::destroy(t->framebuffer);
                    if (bgfx::isValid(t->texture)) bgfx::destroy(t->texture);
                    if (!t->borrowed) t->native.Reset();
                }
            }
            for (const auto& t : targets) {if (bgfx::isValid(t.framebuffer)) bgfx::destroy(t.framebuffer);if (bgfx::isValid(t.texture)) bgfx::destroy(t.texture);}
            for (const auto& t : exports) {if (bgfx::isValid(t.framebuffer)) bgfx::destroy(t.framebuffer);if (bgfx::isValid(t.texture)) bgfx::destroy(t.texture);}
            for (const auto& [id,t] : images) {static_cast<void>(id); bgfx::destroy(t.texture);}
            if (bgfx::isValid(composite)) bgfx::destroy(composite);
            if (bgfx::isValid(encode)) bgfx::destroy(encode);
            if (bgfx::isValid(mapping)) bgfx::destroy(mapping);
            if (bgfx::isValid(sampler)) bgfx::destroy(sampler);
            if (bgfx::isValid(encoding)) bgfx::destroy(encoding);
            if (bgfx::isValid(resample)) bgfx::destroy(resample);
            if (bgfx::isValid(mapping_rows)) bgfx::destroy(mapping_rows);
            if (bgfx::isValid(mapping_edges)) bgfx::destroy(mapping_edges);
            if (bgfx::isValid(mapping_identity)) bgfx::destroy(mapping_identity);
            for (const auto handle : {layer_visual,surface_rows,crop,mask_bounds,patch_visual,content_rows}) if (bgfx::isValid(handle)) bgfx::destroy(handle);
            if (bgfx::isValid(vertices)) bgfx::destroy(vertices);
            if (bgfx::isValid(transparent)) bgfx::destroy(transparent);
            // The size/format override creates a bgfx-owned resource. Drop our
            // extra capture references while its D3D11 DLL is still loaded;
            // bgfx retains ownership through queued destruction/shutdown.
            for (auto& t : targets) if (!t.borrowed) t.native.Reset();
            for (auto& t : exports) if (!t.borrowed) t.native.Reset();
            image_completion.Reset();context.Reset(); device.Reset(); bgfx::shutdown();
            // Imported textures are borrowed by bgfx. Keep their native owners
            // through shutdown, including queued use/destruction of their SRVs.
            images.clear();
            targets.clear();exports.clear();
            image_copy.reset();
            old_graph.reset();
        }
        if (old_windows) {for (auto& output : old_windows->outputs) if (output.window) SDL_DestroyWindow(output.window);old_windows.reset();}
        for (auto& output : outputs) if (output.window) SDL_DestroyWindow(output.window);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
    bgfx::ProgramHandle program(const unsigned char* fragment,std::uint32_t size) {
        const auto vs = bgfx::createShader(bgfx::copy(shaders::fullscreen,sizeof(shaders::fullscreen)));
        const auto fs = bgfx::createShader(bgfx::copy(fragment,size));
        check(bgfx::isValid(vs) && bgfx::isValid(fs),"Native compositor shader creation failed");
        const auto result = bgfx::createProgram(vs,fs,false);
        bgfx::destroy(vs);bgfx::destroy(fs);
        check(bgfx::isValid(result),"Native compositor program creation failed"); return result;
    }
    Texture target(std::uint32_t width,std::uint32_t height,bgfx::TextureFormat::Enum format) {
        Texture result;
        result.texture = bgfx::createTexture2D(static_cast<std::uint16_t>(width),static_cast<std::uint16_t>(height),false,1,format,BGFX_TEXTURE_RT|sampling);
        check(bgfx::isValid(result.texture),"Native compositor texture allocation failed");
        bgfx::frame(); // Preparation only: realize before the render-thread override.
        const auto native = bgfx::overrideInternal(result.texture,static_cast<std::uint16_t>(width),static_cast<std::uint16_t>(height),1,format,BGFX_TEXTURE_RT|sampling);
        if (!native) {bgfx::destroy(result.texture);throw render::Error("Native compositor target was not realized");}
        result.native = reinterpret_cast<ID3D11Texture2D*>(native);
        result.framebuffer = bgfx::createFrameBuffer(1,&result.texture,false);
        if (!bgfx::isValid(result.framebuffer)) {bgfx::destroy(result.texture);throw render::Error("Native compositor framebuffer allocation failed");}
        return result;
    }
    void initialize() {
        check(plan && !plan->nodes.empty(),"Native compositor plan is missing");
        check(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
        window = SDL_CreateWindow("MapGyver native composition context",64,64,SDL_WINDOW_HIDDEN);
        check(window != nullptr,SDL_GetError());
        auto* native = SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr);
        check(native != nullptr,"SDL native window is unavailable");
        if (!preparation_device) preparation_device = std::make_shared<PreparationDevice>();
        const auto& desired = preparation_device->identity();
        bgfx::Init init; init.type = bgfx::RendererType::Direct3D11; init.vendorId = static_cast<std::uint16_t>(desired.VendorId);init.deviceId = static_cast<std::uint16_t>(desired.DeviceId);
        init.platformData.nwh = native;init.platformData.context = preparation_device->native();
        init.resolution.width = 64;init.resolution.height = 64;init.resolution.reset = BGFX_RESET_NONE;init.callback = &callbacks;
        check(bgfx::init(init),"Native compositor bgfx initialization failed"); initialized = true;
        check(bgfx::getRendererType() == bgfx::RendererType::Direct3D11,"Native compositor selected another renderer");
        device = static_cast<ID3D11Device*>(bgfx::getInternalData()->context);
        check(device.Get() == preparation_device->native(),"Native compositor did not retain the owned preparation device");device->GetImmediateContext(&context);
        const D3D11_QUERY_DESC completion_description{D3D11_QUERY_EVENT,0};
        require(device->CreateQuery(&completion_description,&image_completion),"Native image copy completion allocation failed");
        ComPtr<IDXGIDevice> dxgi;require(device.As(&dxgi),"Native compositor DXGI interface missing");ComPtr<IDXGIAdapter> base;require(dxgi->GetAdapter(&base),"Native compositor actual adapter missing");
        ComPtr<IDXGIAdapter1> actual;require(base.As(&actual),"Native compositor adapter identity missing");require(actual->GetDesc1(&identity),"Native compositor adapter description failed");
        check(identity.AdapterLuid.HighPart == desired.AdapterLuid.HighPart && identity.AdapterLuid.LowPart == desired.AdapterLuid.LowPart && !(identity.Flags&DXGI_ADAPTER_FLAG_SOFTWARE),"Native compositor hardware identity differs from requested GPU");
        const auto* caps = bgfx::getCaps();
        check((caps->formats[bgfx::TextureFormat::RGBA16F]&BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0 &&
              (caps->formats[bgfx::TextureFormat::RGBA32F]&BGFX_CAPS_FORMAT_TEXTURE_2D) != 0,"Native compositor linear texture formats unavailable");
        check(outputs.empty() || (caps->supported&BGFX_CAPS_SWAP_CHAIN),"Native multiple-window swapchains unavailable");
        exported_nodes.push_back(plan->preview);
        for (const auto& surface : plan->surfaces) if (std::find(exported_nodes.begin(),exported_nodes.end(),surface.node) == exported_nodes.end()) exported_nodes.push_back(surface.node);
        views.resize(plan->nodes.size());
        for (std::size_t index = 0; index < plan->nodes.size(); ++index) if (plan->nodes[index].target) views[index] = static_cast<bgfx::ViewId>(render_views++);
        check(render_views+exported_nodes.size()+outputs.size()*4 <= caps->limits.maxViews,"Native compositor exceeds actual view capacity");
        std::uint64_t draw_count = exported_nodes.size();for (const auto& node : plan->nodes) draw_count += node.layers.size();
        for (const auto& output : plan->output_mappings) draw_count += output.patches.size();
        check(draw_count+outputs.size()*4 <= caps->limits.maxDrawCalls,"Native compositor exceeds actual draw capacity");
        check(plan->targets.size()+exported_nodes.size()+outputs.size()*4 <= caps->limits.maxFrameBuffers,"Native compositor exceeds actual framebuffer capacity");
        std::uint64_t payload = plan->target_bytes+plan->image_upload_bytes+plan->export_bytes+16;
        check(payload <= 536870912,"Native graph/global constant payload exceeds capacity");
        for (const auto& output : outputs) {
            const auto& config = output.configured;
            const auto mapped = std::find_if(plan->output_mappings.begin(),plan->output_mappings.end(),[&](const auto& item){return item.id == config.id;});
            check(mapped == plan->output_mappings.end() || mapped->reason.empty(),"Native output mapping is unavailable");
            check(std::find(exported_nodes.begin(),exported_nodes.end(),config.node) != exported_nodes.end() && config.width && config.height && config.width <= caps->limits.maxTextureSize && config.height <= caps->limits.maxTextureSize && config.width <= UINT16_MAX && config.height <= UINT16_MAX,"Native output node/dimensions exceed actual capacity");
            const auto size = static_cast<std::uint64_t>(config.width)*config.height*16;
            check(payload <= 536870912 && size <= 536870912-payload,"Native output textures exceed total payload capacity");payload += size;
        }
        owned_payload_bytes = payload;
        std::set<std::pair<domain::Id,std::uint64_t>> image_ids;
        std::uint64_t largest_image = 0;
        for (const auto& node : plan->nodes) if (node.image) {
            check(node.image->pixels->size() <= UINT32_MAX/sizeof(render::Pixel),"Native image upload exceeds bgfx memory capacity");
            image_ids.emplace(node.image->resource,node.image->generation);
            largest_image = std::max(largest_image,static_cast<std::uint64_t>(node.image->pixels->size())*sizeof(render::Pixel));
        }
        // During startup import, one realized bgfx placeholder briefly coexists
        // with its immutable native image. Exports/output textures follow imports.
        check(plan->target_bytes+plan->image_upload_bytes+plan->fallback_bytes <= 536870912-largest_image,
            "Native startup image import exceeds bounded transient payload capacity");
        check(image_ids.size()+plan->targets.size()+exported_nodes.size()+outputs.size()*3+1 <= caps->limits.maxTextures,
              "Native compositor exceeds actual texture handle capacity");
        for (const auto& node : plan->nodes) {
            check(node.width <= caps->limits.maxTextureSize && node.height <= caps->limits.maxTextureSize && node.width <= UINT16_MAX && node.height <= UINT16_MAX,"Native compositor exceeds actual texture dimension capacity");
            check(node.layer_visuals.empty() || node.layer_visuals.size() == node.layers.size(),"Native layer visual count differs from graph");
            for (const auto& visual : node.layer_visuals) domain::validate_visual(visual,node.source.first);
        }
        composite = program(shaders::composite,sizeof(shaders::composite)); encode = program(shaders::encode,sizeof(shaders::encode));
        mapping = program(shaders::mapping,sizeof(shaders::mapping));
        sampler = bgfx::createUniform("s_image",bgfx::UniformType::Sampler);encoding = bgfx::createUniform("u_encoding",bgfx::UniformType::Vec4);
        resample = bgfx::createUniform("u_resample",bgfx::UniformType::Vec4);
        mapping_rows = bgfx::createUniform("u_mappingRows",bgfx::UniformType::Vec4,3);
        mapping_edges = bgfx::createUniform("u_mappingEdges",bgfx::UniformType::Vec4,4);
        mapping_identity = bgfx::createUniform("u_mappingIdentity",bgfx::UniformType::Vec4);
        layer_visual = bgfx::createUniform("u_layerVisual",bgfx::UniformType::Vec4);
        surface_rows = bgfx::createUniform("u_surfaceRows",bgfx::UniformType::Vec4,3);
        crop = bgfx::createUniform("u_crop",bgfx::UniformType::Vec4);
        mask_bounds = bgfx::createUniform("u_maskBounds",bgfx::UniformType::Vec4);
        patch_visual = bgfx::createUniform("u_patchVisual",bgfx::UniformType::Vec4);
        content_rows = bgfx::createUniform("u_contentRows",bgfx::UniformType::Vec4,3);
        check(bgfx::isValid(sampler) && bgfx::isValid(encoding) && bgfx::isValid(resample),"Native compositor uniform creation failed");
        check(bgfx::isValid(mapping_rows) && bgfx::isValid(mapping_edges) && bgfx::isValid(mapping_identity),"Native mapping uniform creation failed");
        for (const auto handle : {layer_visual,surface_rows,crop,mask_bounds,patch_visual,content_rows}) check(bgfx::isValid(handle),"Native visual uniform creation failed");
        struct Vertex {float x,y,z,u,v;}; constexpr std::array triangle{Vertex{-1,-1,0,0,1},Vertex{3,-1,0,2,1},Vertex{-1,3,0,0,-1}};
        bgfx::VertexLayout layout; layout.begin(bgfx::RendererType::Direct3D11).add(bgfx::Attrib::Position,3,bgfx::AttribType::Float).add(bgfx::Attrib::TexCoord0,2,bgfx::AttribType::Float).end();
        vertices = bgfx::createVertexBuffer(bgfx::copy(triangle.data(),sizeof(triangle)),layout);check(bgfx::isValid(vertices),"Native compositor geometry allocation failed");
        targets.reserve(plan->targets.size()); for (const auto& slot : plan->targets) targets.push_back(target(slot.width,slot.height,bgfx::TextureFormat::RGBA16F));
        {
            constexpr render::Pixel zero{};transparent = bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA32F,sampling,bgfx::copy(&zero,sizeof(zero)));
            check(bgfx::isValid(transparent),"Native transparent fallback allocation failed");
        }
        inputs.reserve(plan->nodes.size());
        for (const auto& node : plan->nodes) {
            if (node.image) {
                const auto key = std::pair{node.image->resource,node.image->generation};
                auto found_image = images.find(key);
                if (found_image == images.end()) {
                    static_assert(sizeof(render::Pixel) == 16);
                    auto prepared = preparation_device->prepare(*node.image,true);
                    const auto texture = bgfx::createTexture2D(static_cast<std::uint16_t>(node.width),static_cast<std::uint16_t>(node.height),false,1,bgfx::TextureFormat::RGBA32F,sampling,
                        nullptr);
                    check(bgfx::isValid(texture),"Native compositor image handle preparation failed");
                    bgfx::frame(); // Startup preparation only; realize the import handle.
                    const auto native_image = reinterpret_cast<std::uintptr_t>(prepared.texture.Get());
                    if (bgfx::overrideInternal(texture,native_image) != native_image) {
                        bgfx::destroy(texture);throw render::Error("Native compositor prepared image import failed");
                    }
                    found_image = images.emplace(key,Input{texture,std::move(prepared)}).first;
                }
                inputs.push_back(found_image->second.texture);
            } else inputs.push_back(node.target ? targets.at(*node.target).texture : transparent);
        }
        exports.reserve(exported_nodes.size());for (const auto index : exported_nodes) exports.push_back(target(plan->nodes[index].width,plan->nodes[index].height,bgfx::TextureFormat::RGBA8));
        for (auto& output : outputs) {
            const auto& config = output.configured;
            output.mapped = target(config.width,config.height,bgfx::TextureFormat::RGBA16F);
            output.held = target(config.width,config.height,bgfx::TextureFormat::RGBA8);
            output.presented = target(config.width,config.height,bgfx::TextureFormat::RGBA8);
            output.window = SDL_CreateWindow(config.name.c_str(),static_cast<int>(config.width),static_cast<int>(config.height),
                SDL_WINDOW_HIDDEN|(config.desktop_position ? SDL_WINDOW_BORDERLESS : 0));
            check(output.window != nullptr,SDL_GetError());
            auto* handle = SDL_GetPointerProperty(SDL_GetWindowProperties(output.window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr);
            check(handle != nullptr,"Native output window handle unavailable");
            if (config.desktop_position) {
                check(SDL_SetWindowPosition(output.window,(*config.desktop_position)[0],(*config.desktop_position)[1]),SDL_GetError());
                RECT rectangle{};check(GetWindowRect(static_cast<HWND>(handle),&rectangle) != FALSE,"Native display rectangle query failed");
                check(rectangle.left == (*config.desktop_position)[0] && rectangle.top == (*config.desktop_position)[1] &&
                    rectangle.right-rectangle.left == static_cast<LONG>(config.width) && rectangle.bottom-rectangle.top == static_cast<LONG>(config.height),
                    "Native display window pixel rectangle differs from explicit target binding");
            }
            output.swapchain = bgfx::createFrameBuffer(handle,static_cast<std::uint16_t>(config.width),static_cast<std::uint16_t>(config.height));
            check(bgfx::isValid(output.swapchain),"Native output swapchain allocation failed");
        }
        bgfx::frame();bgfx::frame(); start = std::chrono::steady_clock::now();
        for (auto& output : outputs) output.show_pending = !output.configured.hidden && output.configured.controls.enabled;
    }
    void sampling_size(std::uint32_t source_width,std::uint32_t source_height,std::uint32_t width,std::uint32_t height) {
        const std::array<float,4> size{static_cast<float>(source_width),static_cast<float>(source_height),static_cast<float>(width),static_cast<float>(height)};
        bgfx::setUniform(resample,size.data());
    }
    RenderedFrame frame() {
        check_owner();
        SDL_PumpEvents();
        for (std::size_t index = 0; index < plan->nodes.size(); ++index) {
            const auto& node = plan->nodes[index]; if (!node.target) continue;
            const auto view = views[index];
            bgfx::setViewMode(view,bgfx::ViewMode::Sequential);bgfx::setViewFrameBuffer(view,targets[*node.target].framebuffer);
            bgfx::setViewRect(view,0,0,static_cast<std::uint16_t>(node.width),static_cast<std::uint16_t>(node.height));
            bgfx::setViewClear(view,BGFX_CLEAR_COLOR,0);bgfx::touch(view);
            for (std::size_t layer = 0; layer < node.layers.size(); ++layer) {
                const auto input = node.layers[layer];const auto visual = node.layer_visuals.empty() ? domain::LayerVisual{} : node.layer_visuals.at(layer);
                const std::array<float,4> parameters{static_cast<float>(visual.opacity),0,0,0};bgfx::setUniform(layer_visual,parameters.data());
                sampling_size(plan->nodes[input].width,plan->nodes[input].height,node.width,node.height);
                bgfx::setVertexBuffer(0,vertices);bgfx::setTexture(0,sampler,inputs[input]);
                bgfx::setState(visual_state(visual.blend));
                bgfx::submit(view,composite);++draws;
            }
        }
        for (std::size_t output = 0; output < exported_nodes.size(); ++output) {
            const auto index = exported_nodes[output];const auto& node = plan->nodes[index];const auto view = static_cast<bgfx::ViewId>(render_views+output);
            bgfx::setViewFrameBuffer(view,exports[output].framebuffer);bgfx::setViewRect(view,0,0,static_cast<std::uint16_t>(node.width),static_cast<std::uint16_t>(node.height));
            bgfx::setViewClear(view,BGFX_CLEAR_COLOR,0);const std::array<float,4> parameters{node.encoding.transfer == render::Transfer::srgb ? 1.F : 0.F,node.encoding.alpha == render::Alpha::premultiplied ? 1.F : 0.F,0,0};
            sampling_size(node.width,node.height,node.width,node.height);
            bgfx::setUniform(encoding,parameters.data());bgfx::setVertexBuffer(0,vertices);bgfx::setTexture(0,sampler,inputs[index]);bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);bgfx::submit(view,encode);++draws;
        }
        for (std::size_t index = 0; index < outputs.size(); ++index) {
            auto& output = outputs[index];const auto& config = output.configured;
            const auto base_view = render_views+exported_nodes.size()+index*4;
            const auto view = [&](std::size_t offset,bgfx::FrameBufferHandle framebuffer) {
                const auto id = static_cast<bgfx::ViewId>(base_view+offset);
                bgfx::setViewFrameBuffer(id,framebuffer);bgfx::setViewRect(id,0,0,static_cast<std::uint16_t>(config.width),static_cast<std::uint16_t>(config.height));
                bgfx::setViewClear(id,BGFX_CLEAR_COLOR,0x000000ff);bgfx::touch(id);return id;
            };
            const auto draw = [&](bgfx::ViewId id,bgfx::TextureHandle texture,bgfx::ProgramHandle program,std::uint32_t width,std::uint32_t height) {
                sampling_size(width,height,config.width,config.height);
                constexpr std::array<float,4> defaults{1,0,0,0};bgfx::setUniform(layer_visual,defaults.data());
                bgfx::setVertexBuffer(0,vertices);bgfx::setTexture(0,sampler,texture);bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);bgfx::submit(id,program);++draws;
            };
            bool carried = false;
            if (bgfx::isValid(output.freeze_source)) {
                if (config.controls.freeze) {draw(view(1,output.held.framebuffer),output.freeze_source,composite,output.freeze_source_size[0],output.freeze_source_size[1]);carried = true;}
                else output.held_frame = 0;
                output.freeze_source = BGFX_INVALID_HANDLE;
            }
            if (!carried && config.controls.enabled && !config.controls.freeze) {
                const auto mapped_view = view(0,output.mapped.framebuffer);
                bgfx::setViewMode(mapped_view,bgfx::ViewMode::Sequential);bgfx::setViewClear(mapped_view,BGFX_CLEAR_COLOR,0x000000ff);
                const auto configured = std::find_if(plan->output_mappings.begin(),plan->output_mappings.end(),[&](const auto& item){return item.id == config.id;});
                if (configured == plan->output_mappings.end()) {
                    sampling_size(plan->nodes[config.node].width,plan->nodes[config.node].height,config.width,config.height);
                    constexpr std::array<float,4> defaults{1,0,0,0};bgfx::setUniform(layer_visual,defaults.data());
                    bgfx::setVertexBuffer(0,vertices);bgfx::setTexture(0,sampler,inputs[config.node]);
                    bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);bgfx::submit(mapped_view,composite);++draws;
                } else {
                    for (const auto& patch : configured->patches) {
                        const auto& source = plan->nodes[patch.node];sampling_size(source.width,source.height,config.width,config.height);
                        bgfx::setUniform(mapping_rows,patch.rows.data(),3);bgfx::setUniform(mapping_edges,patch.edges.data(),4);
                        bgfx::setUniform(surface_rows,patch.surface_rows.data(),3);bgfx::setUniform(crop,patch.crop.data());
                        bgfx::setUniform(content_rows,patch.content_rows.data(),3);
                        bgfx::setUniform(mask_bounds,patch.mask_bounds.data());bgfx::setUniform(patch_visual,patch.visual.data());
                        const std::array<float,4> identity_parameters{patch.identity ? 1.F : 0.F,0,0,0};bgfx::setUniform(mapping_identity,identity_parameters.data());
                        bgfx::setVertexBuffer(0,vertices);bgfx::setTexture(0,sampler,inputs[patch.node]);
                        bgfx::setState(visual_state(patch.blend));
                        bgfx::submit(mapped_view,mapping);++draws;
                    }
                }
                const auto id = view(1,output.held.framebuffer);
                constexpr std::array<float,4> display_encoding{1,0,1,0};
                bgfx::setUniform(encoding,display_encoding.data());draw(id,output.mapped.texture,encode,config.width,config.height);output.held_frame = sequence+1;
            } else if (!carried && !output.held_frame) static_cast<void>(view(1,output.held.framebuffer));
            const auto presented = view(2,output.presented.framebuffer);
            if (config.controls.enabled && !config.controls.blackout && output.held_frame) draw(presented,output.held.texture,composite,config.width,config.height);
            draw(view(3,output.swapchain),output.presented.texture,composite,config.width,config.height);
        }
        if (qualification_target) {
            const auto view = static_cast<bgfx::ViewId>(render_views+exported_nodes.size()+outputs.size()*4);
            bgfx::setViewFrameBuffer(view,qualification_target->framebuffer);
            bgfx::setViewRect(view,0,0,qualification_size[0],qualification_size[1]);bgfx::setViewClear(view,BGFX_CLEAR_COLOR,0);
            const auto& node = plan->nodes[plan->preview];const std::array<float,4> parameters{
                node.encoding.transfer == render::Transfer::srgb ? 1.F : 0.F,node.encoding.alpha == render::Alpha::premultiplied ? 1.F : 0.F,0,0};
            sampling_size(node.width,node.height,qualification_size[0],qualification_size[1]);
            bgfx::setUniform(encoding,parameters.data());bgfx::setVertexBuffer(0,vertices);bgfx::setTexture(0,sampler,inputs[plan->preview]);
            bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);bgfx::submit(view,encode);++draws;
        }
        bgfx::frame();
        if (old_windows && !old_windows->fence_submitted) {
            context->End(old_windows->completion.Get());context->Flush();old_windows->fence_submitted = true;
        }
        for (auto& output : outputs) if (output.show_pending) {check(SDL_ShowWindow(output.window),SDL_GetError());output.show_pending = false;}
        if (old_windows && old_windows->hide_pending) {
            for (auto& output : old_windows->outputs) if (output.window && !output.configured.hidden) check(SDL_HideWindow(output.window),SDL_GetError());
            old_windows->hide_pending = false;
        }
        const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        return {++sequence,applied_revision,{ticks,{1,1000000000}}};
    }
};
CompositionRenderer::CompositionRenderer(std::shared_ptr<const render::Plan> plan,std::vector<OutputWindow> outputs,std::shared_ptr<PreparationDevice> device)
    : impl_(std::make_unique<Impl>(std::move(plan),std::move(outputs),std::move(device))) {impl_->initialize();}
CompositionRenderer::~CompositionRenderer() = default;
RenderedFrame CompositionRenderer::frame() {return impl_->frame();}
std::array<std::uint32_t,4> CompositionRenderer::adapter_identity() const {return {impl_->identity.VendorId,impl_->identity.DeviceId,static_cast<std::uint32_t>(impl_->identity.AdapterLuid.HighPart),impl_->identity.AdapterLuid.LowPart};}
std::uint64_t CompositionRenderer::submitted_draws() const noexcept {return impl_->draws;}
void CompositionRenderer::output_controls(const domain::Id& id,OutputControls controls) {
    auto& output = impl_->output(id);
    if (!output.configured.hidden && controls.enabled != output.configured.controls.enabled)
        if (controls.enabled) output.show_pending = true;
        else {check(SDL_HideWindow(output.window),SDL_GetError());output.show_pending = false;}
    output.configured.controls = controls;
}
std::uint64_t CompositionRenderer::held_frame(const domain::Id& id) const {return impl_->output(id).held_frame;}
void CompositionRenderer::apply_output_controls(std::uint64_t revision,std::span<const OutputControls> controls) {
    impl_->check_owner();check(controls.size() == impl_->outputs.size() && revision >= impl_->applied_revision,"Native control batch count/revision differs from prepared outputs");
    for (std::size_t i = 0; i < controls.size(); ++i) output_controls(impl_->outputs[i].configured.id,controls[i]);
    impl_->applied_revision = revision;
}
std::vector<std::uint8_t> CompositionRenderer::capture_for_test(std::uint32_t index) {
    impl_->check_owner();
    const auto found = std::find(impl_->exported_nodes.begin(),impl_->exported_nodes.end(),index);check(found != impl_->exported_nodes.end(),"Offline capture requested an unexported node");
    const auto slot = static_cast<std::size_t>(found-impl_->exported_nodes.begin());
    auto* texture = impl_->exports[slot].native.Get();check(texture != nullptr,"Native compositor output resource missing");
    return readback(impl_->device.Get(),impl_->context.Get(),texture);
}
std::vector<std::uint8_t> CompositionRenderer::capture_output_for_test(const domain::Id& id) {
    auto& output = impl_->output(id);
    return readback(impl_->device.Get(),impl_->context.Get(),output.presented.native.Get());
}
bool CompositionRenderer::apply_images(std::unique_ptr<PreparedGeneration>& candidate) {
    impl_->check_owner();
    if (impl_->image_copy || impl_->old_graph || impl_->old_windows) return false;
    check(candidate && candidate->generation > impl_->image_state.generation && candidate->plan &&
        candidate->plan->revision >= impl_->applied_revision && candidate->images.size() == impl_->images.size() && !candidate->retired_plan,
        "Native image candidate generation/revision/count is stale or invalid");
    // Validate the entire handoff before the first GPU command. Preparation
    // establishes layout; native descriptions/device checks protect this seam.
    auto source = candidate->images.begin();
    std::uint64_t bytes = 0;
    for (const auto& [key,input] : impl_->images) {
        check(source->resource == key.first && source->generation > input.image.generation && source->width == input.image.width &&
            source->height == input.image.height && source->texture,"Native image resource generation/dimensions differ from allocated inputs");
        ComPtr<ID3D11Device> device;source->texture->GetDevice(&device);
        check(device.Get() == impl_->device.Get(),"Native image handoff requires the same owned device");
        bytes += static_cast<std::uint64_t>(source->width)*source->height*sizeof(render::Pixel);
        ++source;
    }
    check(bytes <= 536870912-impl_->owned_payload_bytes,"Native image handoff exceeds combined active/candidate payload capacity");
    source = candidate->images.begin();
    for (auto& [key,input] : impl_->images) {
        static_cast<void>(key);impl_->context->CopyResource(input.image.texture.Get(),source->texture.Get());
        input.image.generation = source->generation;
        ++impl_->image_state.gpu_copy_submissions;
        impl_->image_state.copied_payload_bytes += static_cast<std::uint64_t>(source->width)*source->height*sizeof(render::Pixel);
        ++source;
    }
    // Copies and subsequent draws use the same immediate-context command order.
    // The event covers candidate source reads; sampled destinations stay owned.
    impl_->context->End(impl_->image_completion.Get());
    impl_->context->Flush(); // Submit the copy event; never wait for completion here.
    candidate->retired_plan = std::move(impl_->plan);impl_->plan = candidate->plan;
    impl_->applied_revision = candidate->plan->revision;impl_->image_state.generation = candidate->generation;
    impl_->image_copy = std::move(candidate);return true;
}
std::unique_ptr<PreparedGeneration> CompositionRenderer::retire_images() {
    impl_->check_owner();if (!impl_->image_copy) return {};
    BOOL done = FALSE;const auto result = impl_->context->GetData(impl_->image_completion.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);
    require(result,"Native image copy completion failed");if (result != S_OK || !done) return {};
    return std::move(impl_->image_copy);
}
ImageHandoffState CompositionRenderer::image_handoff_state() const {impl_->check_owner();return impl_->image_state;}
std::uint64_t CompositionRenderer::resource_payload_bytes() const {impl_->check_owner();return impl_->owned_payload_bytes;}
std::uint64_t CompositionRenderer::constant_upload_bytes() const noexcept {return sizeof(render::Pixel);}
GraphLimits CompositionRenderer::graph_limits() const {
    impl_->check_owner();const auto& limits = bgfx::getCaps()->limits;
    std::uint64_t output_payload = 0;
    for (const auto& output : impl_->outputs) output_payload += static_cast<std::uint64_t>(output.configured.width)*output.configured.height*16;
    return {limits.maxTextureSize,limits.maxViews,limits.maxTextures,limits.maxFrameBuffers,limits.maxDrawCalls,
        impl_->targets.size()+impl_->exports.size()+impl_->images.size()+impl_->outputs.size()*3+1,
        impl_->targets.size()+impl_->exports.size()+impl_->outputs.size()*4,impl_->outputs.size(),impl_->owned_payload_bytes,output_payload};
}
bool CompositionRenderer::stage_graph(std::unique_ptr<PreparedGraph>& candidate) {
    std::unique_ptr<PreparedWindows> no_windows;return stage_scene(candidate,no_windows);
}
bool CompositionRenderer::stage_scene(std::unique_ptr<PreparedGraph>& candidate,std::unique_ptr<PreparedWindows>& windows) {
    impl_->check_owner();if (impl_->old_graph || impl_->old_windows || impl_->image_copy) return false;
    const auto desired_windows = windows ? windows->outputs.size() : impl_->outputs.size();
    check(candidate && candidate->generation > impl_->image_state.generation && candidate->resources.plan &&
        candidate->resources.plan->revision >= impl_->applied_revision && candidate->output_nodes.size() == desired_windows &&
        candidate->owner_device.Get() == impl_->device.Get() && !candidate->retiring,"Prepared graph generation/revision/routes/device invalid");
    if (windows) {
        check(windows->generation == candidate->generation && windows->owner_device.Get() == impl_->device.Get() && !windows->retiring,
            "Prepared window generation/device invalid");
    }
    for (const auto& output : windows ? windows->outputs : impl_->outputs) {
        const auto& mappings = candidate->resources.plan->output_mappings;
        const auto mapped = std::find_if(mappings.begin(),mappings.end(),[&](const auto& item){return item.id == output.configured.id;});
        check(mapped == mappings.end() || mapped->reason.empty(),"Prepared native output mapping is unavailable");
    }
    if (windows && !impl_->bind_windows(*windows)) return false;
    if (candidate->last_step_frame == impl_->sequence) return false;
    candidate->last_step_frame = impl_->sequence;
    if (candidate->cursor < candidate->binding_count) {
        auto& binding = candidate->bindings[candidate->cursor];
        if (!candidate->imported) {
            *binding.handle = bgfx::createTexture2D(1,1,false,1,binding.format,sampling|(binding.framebuffer ? BGFX_TEXTURE_RT : 0));
            check(bgfx::isValid(*binding.handle),"Prepared graph placeholder allocation failed");candidate->imported = true;
        } else {
            const auto pointer = reinterpret_cast<std::uintptr_t>(binding.native);
            ComPtr<ID3D11Device> device;binding.native->GetDevice(&device);check(device.Get() == impl_->device.Get(),"Prepared graph belongs to another device");
            check(bgfx::overrideInternal2D(*binding.handle,pointer,binding.width,binding.height) == pointer,"Prepared graph native texture import failed");
            if (binding.framebuffer) {
                *binding.framebuffer = bgfx::createFrameBuffer(1,binding.handle,false);
                check(bgfx::isValid(*binding.framebuffer),"Prepared graph framebuffer allocation failed");
            }
            ++candidate->cursor;candidate->imported = false;
        }
        return false; // Realize commands while the old graph submits this frame.
    }
    auto& next = candidate->resources;
    for (std::size_t i = 0; i < next.plan->nodes.size(); ++i) {
        const auto& node = next.plan->nodes[i];
        next.inputs[i] = node.image ? next.images.at({node.image->resource,node.image->generation}).texture :
            node.target ? next.targets[*node.target].texture : impl_->transparent;
    }
    const auto revision = next.plan->revision;
    const auto previous_views = impl_->render_views+impl_->exported_nodes.size()+impl_->outputs.size()*4;
    impl_->context->End(candidate->completion.Get());impl_->context->Flush(); // Covers last reads of the old graph.
    if (windows) impl_->apply_windows(windows);
    static_cast<GraphResources&>(*impl_).swap(next);
    const auto current_views = impl_->render_views+impl_->exported_nodes.size()+impl_->outputs.size()*4;
    for (auto view = current_views; view < previous_views; ++view) bgfx::resetView(static_cast<bgfx::ViewId>(view));
    for (std::size_t i = 0; i < impl_->outputs.size(); ++i) impl_->outputs[i].configured.node = candidate->output_nodes[i];
    impl_->applied_revision = revision;impl_->image_state.generation = candidate->generation;
    candidate->list_bindings();candidate->retiring = true;candidate->last_step_frame = UINT64_MAX;
    impl_->old_graph = std::move(candidate);return true;
}
void CompositionRenderer::cancel_graph(std::unique_ptr<PreparedGraph>& candidate) {
    std::unique_ptr<PreparedWindows> no_windows;cancel_scene(candidate,no_windows);
}
void CompositionRenderer::cancel_scene(std::unique_ptr<PreparedGraph>& candidate,std::unique_ptr<PreparedWindows>& windows) {
    impl_->check_owner();check(candidate && candidate->owner_device.Get() == impl_->device.Get() && !impl_->old_graph && !impl_->image_copy,
        "Graph cancellation ownership/device is busy/invalid");
    check(!impl_->old_windows && (!windows || windows->owner_device.Get() == impl_->device.Get()),"Window cancellation ownership/device invalid");
    impl_->context->End(candidate->completion.Get());impl_->context->Flush();
    candidate->retiring = true;candidate->cursor = 0;candidate->last_step_frame = UINT64_MAX;impl_->old_graph = std::move(candidate);
    if (windows) {
        impl_->context->End(windows->completion.Get());impl_->context->Flush();windows->fence_submitted = true;
        windows->retiring = true;windows->cursor = 0;windows->step = 0;windows->last_step_frame = UINT64_MAX;impl_->old_windows = std::move(windows);
    }
}
std::unique_ptr<PreparedGraph> CompositionRenderer::retire_graph() {
    impl_->check_owner();if (!impl_->old_graph) return {};
    auto& old = *impl_->old_graph;
    if (!old.completion_observed) {
        BOOL done = FALSE;const auto result = impl_->context->GetData(old.completion.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        require(result,"Old graph completion failed");if (result != S_OK || !done) return {};
        old.completion_observed = true;
    }
    if (old.last_step_frame == impl_->sequence) return {};
    old.last_step_frame = impl_->sequence;
    if (old.cursor < old.binding_count) {
        auto& binding = old.bindings[old.cursor++];
        if (binding.framebuffer && bgfx::isValid(*binding.framebuffer)) {bgfx::destroy(*binding.framebuffer);*binding.framebuffer = BGFX_INVALID_HANDLE;}
        if (bgfx::isValid(*binding.handle)) {bgfx::destroy(*binding.handle);*binding.handle = BGFX_INVALID_HANDLE;}
        old.destroyed_frame = impl_->sequence+1;return {};
    }
    if (old.destroyed_frame != UINT64_MAX && impl_->sequence < old.destroyed_frame) return {};
    return std::move(impl_->old_graph);
}
std::unique_ptr<PreparedWindows> CompositionRenderer::retire_windows() {
    impl_->check_owner();if (!impl_->old_windows) return {};auto& old = *impl_->old_windows;
    if (!old.fence_submitted) return {};
    if (!old.completion_observed) {
        BOOL done = FALSE;const auto result = impl_->context->GetData(old.completion.Get(),&done,sizeof(done),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        require(result,"Old window completion failed");if (result != S_OK || !done) return {};old.completion_observed = true;
    }
    if (old.last_step_frame == impl_->sequence) return {};old.last_step_frame = impl_->sequence;
    while (old.cursor < old.outputs.size() && !old.outputs[old.cursor].window) ++old.cursor;
    if (old.cursor == old.outputs.size()) return std::move(impl_->old_windows);
    auto& output = old.outputs[old.cursor];
    if (old.step == 0) {
        Impl::destroy_output_handles(output);old.destroyed_frame = impl_->sequence+1;old.step = 1;
    } else if (impl_->sequence >= old.destroyed_frame) {
        SDL_DestroyWindow(output.window);output.window = nullptr;old.step = 0;++old.cursor;
    }
    return {};
}
std::vector<std::uint8_t> CompositionRenderer::qualify_borrowed_target(PreparedTarget target) {
    impl_->check_owner();
    check(target.texture && target.format == DXGI_FORMAT_R8G8B8A8_UNORM && target.width && target.height,
        "Borrowed target experiment requires a prepared RGBA8 render target");
    D3D11_TEXTURE2D_DESC description{};target.texture->GetDesc(&description);
    check(description.Width == target.width && description.Height == target.height && description.Format == target.format &&
        description.ArraySize == 1 && description.MipLevels == 1 && description.SampleDesc.Count == 1 &&
        (description.BindFlags&(D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET)) == (D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET),
        "Borrowed target native description differs from its prepared metadata");
    ComPtr<ID3D11Device> device;target.texture->GetDevice(&device);check(device.Get() == impl_->device.Get(),"Borrowed target belongs to another device");
    const auto* caps = bgfx::getCaps();
    check(impl_->render_views+impl_->exported_nodes.size()+impl_->outputs.size()*4 < caps->limits.maxViews &&
        target.width <= caps->limits.maxTextureSize && target.height <= caps->limits.maxTextureSize &&
        static_cast<std::uint64_t>(target.width)*target.height*4 <= 536870912-impl_->owned_payload_bytes,
        "Borrowed target experiment exceeds actual view/dimension/payload capacity");
    Texture borrowed;borrowed.native = std::move(target.texture);
    borrowed.texture = bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT|sampling);
    check(bgfx::isValid(borrowed.texture),"Borrowed target placeholder allocation failed");
    struct Cleanup {
        Impl& impl;Texture& target;
        ~Cleanup() {
            impl.qualification_target = nullptr;
            bgfx::resetView(static_cast<bgfx::ViewId>(impl.render_views+impl.exported_nodes.size()+impl.outputs.size()*4));
            if (bgfx::isValid(target.framebuffer)) bgfx::destroy(target.framebuffer);
            if (bgfx::isValid(target.texture)) bgfx::destroy(target.texture);
            static_cast<void>(impl.frame()); // Drain queued owner-side destruction while native ownership survives.
        }
    } cleanup{*impl_,borrowed};
    static_cast<void>(impl_->frame()); // Realize only the 1x1 placeholder while old scene continues.
    const auto pointer = reinterpret_cast<std::uintptr_t>(borrowed.native.Get());
    check(bgfx::overrideInternal2D(borrowed.texture,pointer,0,static_cast<std::uint16_t>(target.height)) == 0 &&
        bgfx::overrideInternal2D(borrowed.texture,0,static_cast<std::uint16_t>(target.width),static_cast<std::uint16_t>(target.height)) == 0,
        "Borrowed target metadata import accepted invalid dimensions/native pointer");
    check(bgfx::overrideInternal2D(borrowed.texture,pointer,static_cast<std::uint16_t>(target.width),static_cast<std::uint16_t>(target.height)) == pointer,
        "Borrowed target native import failed");
    borrowed.framebuffer = bgfx::createFrameBuffer(1,&borrowed.texture,false);
    check(bgfx::isValid(borrowed.framebuffer),"Borrowed target framebuffer handle creation failed");
    check(bgfx::overrideInternal2D(borrowed.texture,pointer,static_cast<std::uint16_t>(target.width),static_cast<std::uint16_t>(target.height)) == 0,
        "Borrowed target metadata import accepted a framebuffer-attached texture");
    static_cast<void>(impl_->frame()); // Realize its RTV; old frame still renders normally.
    impl_->qualification_size = {static_cast<std::uint16_t>(target.width),static_cast<std::uint16_t>(target.height)};
    impl_->qualification_target = &borrowed;static_cast<void>(impl_->frame());
    return readback(impl_->device.Get(),impl_->context.Get(),borrowed.native.Get());
}
}
