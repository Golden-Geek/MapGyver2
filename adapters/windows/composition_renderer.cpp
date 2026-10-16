// SPDX-License-Identifier: GPL-3.0-or-later
#include "composition_renderer.hpp"
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

namespace mapgyver::windows {
namespace {
using Microsoft::WRL::ComPtr;
void check(bool value,const char* message) {if (!value) throw render::Error(message);}
void require(HRESULT result,const char* message) {if (FAILED(result)) throw render::Error(message);}
constexpr std::uint64_t sampling = BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP;
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
struct Texture {
    bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle framebuffer = BGFX_INVALID_HANDLE;
    ComPtr<ID3D11Texture2D> native; // Extra capture reference; bgfx owns the resource.
};
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
struct CompositionRenderer::Impl {
    struct Output {
        OutputWindow configured;
        SDL_Window* window = nullptr;
        bgfx::FrameBufferHandle swapchain = BGFX_INVALID_HANDLE;
        Texture held, presented;
        std::uint64_t held_frame = 0;
    };
    std::shared_ptr<const render::Plan> plan;
    Callbacks callbacks;
    SDL_Window* window = nullptr;
    bool initialized = false;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    DXGI_ADAPTER_DESC1 identity{};
    bgfx::ProgramHandle composite = BGFX_INVALID_HANDLE, encode = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE, encoding = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle transparent = BGFX_INVALID_HANDLE;
    std::vector<Texture> targets, exports;
    std::vector<bgfx::TextureHandle> inputs;
    std::map<std::pair<domain::Id,std::uint64_t>,bgfx::TextureHandle> images;
    std::vector<std::uint32_t> exported_nodes;
    std::vector<bgfx::ViewId> views;
    std::vector<Output> outputs;
    std::uint32_t render_views = 0;
    std::uint64_t sequence = 0, draws = 0;
    std::chrono::steady_clock::time_point start;
    const std::thread::id owner = std::this_thread::get_id();
    explicit Impl(std::shared_ptr<const render::Plan> value,std::vector<OutputWindow> windows) : plan(std::move(value)) {
        check(windows.size() <= 64,"Native output window count exceeds configured capacity");
        std::set<domain::Id> ids;
        outputs.reserve(windows.size());
        for (auto& configured : windows) {
            check(ids.insert(configured.id).second,"Native output window identities must be unique");
            outputs.push_back(Output{std::move(configured),nullptr,BGFX_INVALID_HANDLE,{},{},0});
        }
    }
    void check_owner() const {check(std::this_thread::get_id() == owner,"Native compositor must use its GPU owner thread");}
    Output& output(const domain::Id& id) {
        check_owner();
        const auto found = std::find_if(outputs.begin(),outputs.end(),[&](const auto& item){return item.configured.id == id;});
        check(found != outputs.end(),"Native output identity is not prepared");return *found;
    }
    ~Impl() {
        if (initialized) {
            for (auto& output : outputs) {
                if (bgfx::isValid(output.swapchain)) bgfx::destroy(output.swapchain);
                for (auto* t : {&output.held,&output.presented}) {
                    if (bgfx::isValid(t->framebuffer)) bgfx::destroy(t->framebuffer);
                    if (bgfx::isValid(t->texture)) bgfx::destroy(t->texture);
                    t->native.Reset();
                }
            }
            for (const auto& t : targets) {if (bgfx::isValid(t.framebuffer)) bgfx::destroy(t.framebuffer);if (bgfx::isValid(t.texture)) bgfx::destroy(t.texture);}
            for (const auto& t : exports) {if (bgfx::isValid(t.framebuffer)) bgfx::destroy(t.framebuffer);if (bgfx::isValid(t.texture)) bgfx::destroy(t.texture);}
            for (const auto& [id,t] : images) {static_cast<void>(id); bgfx::destroy(t);}
            if (bgfx::isValid(composite)) bgfx::destroy(composite);
            if (bgfx::isValid(encode)) bgfx::destroy(encode);
            if (bgfx::isValid(sampler)) bgfx::destroy(sampler);
            if (bgfx::isValid(encoding)) bgfx::destroy(encoding);
            if (bgfx::isValid(vertices)) bgfx::destroy(vertices);
            if (bgfx::isValid(transparent)) bgfx::destroy(transparent);
            // The size/format override creates a bgfx-owned resource. Drop our
            // extra capture references while its D3D11 DLL is still loaded;
            // bgfx retains ownership through queued destruction/shutdown.
            for (auto& t : targets) t.native.Reset();
            for (auto& t : exports) t.native.Reset();
            context.Reset(); device.Reset(); bgfx::shutdown();
        }
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
        ComPtr<IDXGIFactory1> factory; require(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Create native compositor factory failed");
        DXGI_ADAPTER_DESC1 desired{}; bool found = false;
        for (UINT index = 0;; ++index) {
            ComPtr<IDXGIAdapter1> adapter; const auto status = factory->EnumAdapters1(index,&adapter);
            if (status == DXGI_ERROR_NOT_FOUND) break; require(status,"Enumerate compositor adapters failed");
            DXGI_ADAPTER_DESC1 description{}; require(adapter->GetDesc1(&description),"Read compositor adapter failed");
            if (!(description.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) && (!found || description.DedicatedVideoMemory > desired.DedicatedVideoMemory)) {desired = description;found = true;}
        }
        check(found,"Native compositor requires an actual hardware GPU");
        bgfx::Init init; init.type = bgfx::RendererType::Direct3D11; init.vendorId = static_cast<std::uint16_t>(desired.VendorId);init.deviceId = static_cast<std::uint16_t>(desired.DeviceId);
        init.platformData.nwh = native; init.resolution.width = 64;init.resolution.height = 64;init.resolution.reset = BGFX_RESET_NONE;init.callback = &callbacks;
        check(bgfx::init(init),"Native compositor bgfx initialization failed"); initialized = true;
        check(bgfx::getRendererType() == bgfx::RendererType::Direct3D11,"Native compositor selected another renderer");
        device = static_cast<ID3D11Device*>(bgfx::getInternalData()->context);check(device != nullptr,"Native compositor device missing");device->GetImmediateContext(&context);
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
        check(render_views+exported_nodes.size()+outputs.size()*3 <= caps->limits.maxViews,"Native compositor exceeds actual view capacity");
        std::uint64_t draw_count = exported_nodes.size();for (const auto& node : plan->nodes) draw_count += node.layers.size();
        check(draw_count+outputs.size()*3 <= caps->limits.maxDrawCalls,"Native compositor exceeds actual draw capacity");
        check(plan->targets.size()+exported_nodes.size()+outputs.size()*3 <= caps->limits.maxFrameBuffers,"Native compositor exceeds actual framebuffer capacity");
        std::uint64_t payload = plan->target_bytes+plan->image_upload_bytes+plan->export_bytes+plan->fallback_bytes;
        for (const auto& output : outputs) {
            const auto& config = output.configured;
            check(std::find(exported_nodes.begin(),exported_nodes.end(),config.node) != exported_nodes.end() && config.width && config.height && config.width <= caps->limits.maxTextureSize && config.height <= caps->limits.maxTextureSize && config.width <= UINT16_MAX && config.height <= UINT16_MAX,"Native output node/dimensions exceed actual capacity");
            const auto size = static_cast<std::uint64_t>(config.width)*config.height*8;
            check(payload <= 536870912 && size <= 536870912-payload,"Native output textures exceed total payload capacity");payload += size;
        }
        std::set<std::pair<domain::Id,std::uint64_t>> image_ids;
        for (const auto& node : plan->nodes) if (node.image) {
            check(node.image->pixels->size() <= UINT32_MAX/sizeof(render::Pixel),"Native image upload exceeds bgfx memory capacity");
            image_ids.emplace(node.image->resource,node.image->generation);
        }
        check(image_ids.size()+plan->targets.size()+exported_nodes.size()+outputs.size()*2+(plan->fallback_bytes ? 1U : 0U) <= caps->limits.maxTextures,
              "Native compositor exceeds actual texture handle capacity");
        for (const auto& node : plan->nodes) check(node.width <= caps->limits.maxTextureSize && node.height <= caps->limits.maxTextureSize && node.width <= UINT16_MAX && node.height <= UINT16_MAX,"Native compositor exceeds actual texture dimension capacity");
        composite = program(shaders::composite,sizeof(shaders::composite)); encode = program(shaders::encode,sizeof(shaders::encode));
        sampler = bgfx::createUniform("s_image",bgfx::UniformType::Sampler);encoding = bgfx::createUniform("u_encoding",bgfx::UniformType::Vec4);
        check(bgfx::isValid(sampler) && bgfx::isValid(encoding),"Native compositor uniform creation failed");
        struct Vertex {float x,y,z,u,v;}; constexpr std::array triangle{Vertex{-1,-1,0,0,1},Vertex{3,-1,0,2,1},Vertex{-1,3,0,0,-1}};
        bgfx::VertexLayout layout; layout.begin(bgfx::RendererType::Direct3D11).add(bgfx::Attrib::Position,3,bgfx::AttribType::Float).add(bgfx::Attrib::TexCoord0,2,bgfx::AttribType::Float).end();
        vertices = bgfx::createVertexBuffer(bgfx::copy(triangle.data(),sizeof(triangle)),layout);check(bgfx::isValid(vertices),"Native compositor geometry allocation failed");
        targets.reserve(plan->targets.size()); for (const auto& slot : plan->targets) targets.push_back(target(slot.width,slot.height,bgfx::TextureFormat::RGBA16F));
        if (plan->fallback_bytes) {
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
                    const auto texture = bgfx::createTexture2D(static_cast<std::uint16_t>(node.width),static_cast<std::uint16_t>(node.height),false,1,bgfx::TextureFormat::RGBA32F,sampling,
                        bgfx::copy(node.image->pixels->data(),static_cast<std::uint32_t>(node.image->pixels->size()*sizeof(render::Pixel))));
                    check(bgfx::isValid(texture),"Native compositor image upload failed"); found_image = images.emplace(key,texture).first;
                }
                inputs.push_back(found_image->second);
            } else inputs.push_back(node.target ? targets.at(*node.target).texture : transparent);
        }
        exports.reserve(exported_nodes.size());for (const auto index : exported_nodes) exports.push_back(target(plan->nodes[index].width,plan->nodes[index].height,bgfx::TextureFormat::RGBA8));
        for (auto& output : outputs) {
            const auto& config = output.configured;
            output.held = target(config.width,config.height,bgfx::TextureFormat::RGBA8);
            output.presented = target(config.width,config.height,bgfx::TextureFormat::RGBA8);
            output.window = SDL_CreateWindow(config.name.c_str(),static_cast<int>(config.width),static_cast<int>(config.height),SDL_WINDOW_HIDDEN);
            check(output.window != nullptr,SDL_GetError());
            auto* handle = SDL_GetPointerProperty(SDL_GetWindowProperties(output.window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr);
            check(handle != nullptr,"Native output window handle unavailable");
            output.swapchain = bgfx::createFrameBuffer(handle,static_cast<std::uint16_t>(config.width),static_cast<std::uint16_t>(config.height));
            check(bgfx::isValid(output.swapchain),"Native output swapchain allocation failed");
        }
        bgfx::frame();bgfx::frame(); start = std::chrono::steady_clock::now();
        for (const auto& output : outputs) if (!output.configured.hidden && output.configured.controls.enabled) check(SDL_ShowWindow(output.window),SDL_GetError());
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
            for (const auto input : node.layers) {
                bgfx::setVertexBuffer(0,vertices);bgfx::setTexture(0,sampler,inputs[input]);
                bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_INV_SRC_ALPHA));
                bgfx::submit(view,composite);++draws;
            }
        }
        for (std::size_t output = 0; output < exported_nodes.size(); ++output) {
            const auto index = exported_nodes[output];const auto& node = plan->nodes[index];const auto view = static_cast<bgfx::ViewId>(render_views+output);
            bgfx::setViewFrameBuffer(view,exports[output].framebuffer);bgfx::setViewRect(view,0,0,static_cast<std::uint16_t>(node.width),static_cast<std::uint16_t>(node.height));
            bgfx::setViewClear(view,BGFX_CLEAR_COLOR,0);const std::array<float,4> parameters{node.encoding.transfer == render::Transfer::srgb ? 1.F : 0.F,node.encoding.alpha == render::Alpha::premultiplied ? 1.F : 0.F,0,0};
            bgfx::setUniform(encoding,parameters.data());bgfx::setVertexBuffer(0,vertices);bgfx::setTexture(0,sampler,inputs[index]);bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);bgfx::submit(view,encode);++draws;
        }
        for (std::size_t index = 0; index < outputs.size(); ++index) {
            auto& output = outputs[index];const auto& config = output.configured;
            const auto base_view = render_views+exported_nodes.size()+index*3;
            const auto view = [&](std::size_t offset,bgfx::FrameBufferHandle framebuffer) {
                const auto id = static_cast<bgfx::ViewId>(base_view+offset);
                bgfx::setViewFrameBuffer(id,framebuffer);bgfx::setViewRect(id,0,0,static_cast<std::uint16_t>(config.width),static_cast<std::uint16_t>(config.height));
                bgfx::setViewClear(id,BGFX_CLEAR_COLOR,0x000000ff);bgfx::touch(id);return id;
            };
            const auto draw = [&](bgfx::ViewId id,bgfx::TextureHandle texture,bgfx::ProgramHandle program) {
                bgfx::setVertexBuffer(0,vertices);bgfx::setTexture(0,sampler,texture);bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);bgfx::submit(id,program);++draws;
            };
            if (config.controls.enabled && !config.controls.freeze) {
                const auto id = view(0,output.held.framebuffer);
                constexpr std::array<float,4> display_encoding{1,0,1,0};
                bgfx::setUniform(encoding,display_encoding.data());draw(id,inputs[config.node],encode);output.held_frame = sequence+1;
            } else if (!output.held_frame) static_cast<void>(view(0,output.held.framebuffer));
            const auto presented = view(1,output.presented.framebuffer);
            if (config.controls.enabled && !config.controls.blackout && output.held_frame) draw(presented,output.held.texture,composite);
            draw(view(2,output.swapchain),output.presented.texture,composite);
        }
        bgfx::frame();
        const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        return {++sequence,plan->revision,{ticks,{1,1000000000}}};
    }
};
CompositionRenderer::CompositionRenderer(std::shared_ptr<const render::Plan> plan,std::vector<OutputWindow> outputs) : impl_(std::make_unique<Impl>(std::move(plan),std::move(outputs))) {impl_->initialize();}
CompositionRenderer::~CompositionRenderer() = default;
RenderedFrame CompositionRenderer::frame() {return impl_->frame();}
std::array<std::uint32_t,4> CompositionRenderer::adapter_identity() const {return {impl_->identity.VendorId,impl_->identity.DeviceId,static_cast<std::uint32_t>(impl_->identity.AdapterLuid.HighPart),impl_->identity.AdapterLuid.LowPart};}
std::uint64_t CompositionRenderer::submitted_draws() const noexcept {return impl_->draws;}
void CompositionRenderer::output_controls(const domain::Id& id,OutputControls controls) {
    auto& output = impl_->output(id);
    if (!output.configured.hidden && controls.enabled != output.configured.controls.enabled)
        check(controls.enabled ? SDL_ShowWindow(output.window) : SDL_HideWindow(output.window),SDL_GetError());
    output.configured.controls = controls;
}
std::uint64_t CompositionRenderer::held_frame(const domain::Id& id) const {return impl_->output(id).held_frame;}
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
}
