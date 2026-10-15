// SPDX-License-Identifier: GPL-3.0-or-later
// Offline M01 probe: encoding, allocations, JSON and readback are never show work.
#include "bgfx_probe_device.hpp"
#include <mapgyver/build_info.hpp>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <psapi.h>
#include <bcrypt.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using nlohmann::json;
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
constexpr int frame_count = 18;
constexpr std::size_t byte_capacity = 8 * 1024 * 1024;
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void require(HRESULT status, const char* operation) {
    if (FAILED(status)) {
        std::ostringstream error;
        error << operation << " HRESULT=0x" << std::hex << static_cast<unsigned long>(status);
        throw std::runtime_error(error.str());
    }
}
void ff(int status, const char* operation) {
    if (status < 0) {
        std::array<char, AV_ERROR_MAX_STRING_SIZE> description{};
        av_strerror(status, description.data(), description.size());
        throw std::runtime_error(std::string{operation} + ": " + description.data());
    }
}
double microseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}
struct FreeCodec { void operator()(AVCodecContext* p) const { avcodec_free_context(&p); } };
struct FreeFrame { void operator()(AVFrame* p) const { av_frame_free(&p); } };
struct FreePacket { void operator()(AVPacket* p) const { av_packet_free(&p); } };
struct FreeBuffer { void operator()(AVBufferRef* p) const { av_buffer_unref(&p); } };
using Codec = std::unique_ptr<AVCodecContext, FreeCodec>;
using Frame = std::unique_ptr<AVFrame, FreeFrame>;
using Packet = std::unique_ptr<AVPacket, FreePacket>;
using Buffer = std::unique_ptr<AVBufferRef, FreeBuffer>;
Frame frame() { Frame result{av_frame_alloc()}; check(result != nullptr, "Frame allocation failed"); return result; }
Packet packet() { Packet result{av_packet_alloc()}; check(result != nullptr, "Packet allocation failed"); return result; }
Codec codec(const AVCodec* definition) {
    check(definition != nullptr, "Required codec unavailable");
    Codec result{avcodec_alloc_context3(definition)};
    check(result != nullptr, "Codec allocation failed");
    result->thread_count = 1;
    return result;
}
std::string sha256(const std::vector<std::uint8_t>& bytes) {
    // CNG pseudo-handle requires no provider object/cleanup on supported Windows.
    std::array<unsigned char, 32> digest{};
    check(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(bytes.data()),
                     static_cast<ULONG>(bytes.size()), digest.data(), static_cast<ULONG>(digest.size())) >= 0,
          "Fixture SHA256 failed");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : digest) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
}
struct Encoded {
    std::vector<Packet> packets;
    std::vector<std::uint8_t> bytes;
};
Encoded make_fixture(int side) {
    auto encoder = codec(avcodec_find_encoder(AV_CODEC_ID_MPEG2VIDEO));
    encoder->width = side; encoder->height = side;
    encoder->time_base = {1, 30}; encoder->framerate = {30, 1};
    encoder->pix_fmt = AV_PIX_FMT_YUV420P; encoder->max_b_frames = 0;
    encoder->gop_size = 6; encoder->bit_rate = 2'000'000;
    encoder->color_range = AVCOL_RANGE_MPEG; encoder->colorspace = AVCOL_SPC_SMPTE170M;
    ff(avcodec_open2(encoder.get(), encoder->codec, nullptr), "Open fixture encoder");
    auto source = frame(); source->format = encoder->pix_fmt;
    source->width = side; source->height = side;
    ff(av_frame_get_buffer(source.get(), 32), "Allocate fixture pixels");
    Encoded result;
    const auto drain = [&] {
        for (;;) {
            auto output = packet();
            const int status = avcodec_receive_packet(encoder.get(), output.get());
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) break;
            ff(status, "Receive fixture packet");
            check(output->size > 0 && result.packets.size() < 64 &&
                  result.bytes.size() + static_cast<std::size_t>(output->size) <= byte_capacity, "Fixture capacity exceeded");
            result.bytes.insert(result.bytes.end(), output->data, output->data + output->size);
            result.packets.push_back(std::move(output));
        }
    };
    for (int index = 0; index < frame_count; ++index) {
        ff(av_frame_make_writable(source.get()), "Make fixture writable");
        // Flat macroblocks avoid an arbitrary codec-error tolerance at edges.
        // Each frame/region has different luma; chroma and ordering also vary.
        for (int plane = 0; plane < 3; ++plane) {
            const int width = side >> (plane == 0 ? 0 : 1);
            for (int y = 0; y < width; ++y)
                for (int x = 0; x < width; ++x)
                    source->data[plane][y * source->linesize[plane] + x] = static_cast<std::uint8_t>(
                        plane == 0 ? 32 + (index * 7 + (x / 16) * 16 + (y / 16) * 16) % 176 :
                        80 + (index * (plane == 1 ? 3 : 5)) % 96);
        }
        source->pts = index;
        ff(avcodec_send_frame(encoder.get(), source.get()), "Encode fixture frame"); drain();
    }
    ff(avcodec_send_frame(encoder.get(), nullptr), "Flush fixture encoder"); drain();
    check(result.packets.size() == frame_count, "Fixture packet count mismatch");
    return result;
}
AVPixelFormat software_format(AVCodecContext*, const AVPixelFormat* formats) {
    for (const auto* p = formats; *p != AV_PIX_FMT_NONE; ++p) if (*p == AV_PIX_FMT_YUV420P) return *p;
    return AV_PIX_FMT_NONE;
}
AVPixelFormat hardware_format(AVCodecContext* context, const AVPixelFormat* formats) {
    const auto reject = *static_cast<const bool*>(context->opaque);
    if (!reject)
        for (const auto* p = formats; *p != AV_PIX_FMT_NONE; ++p) if (*p == AV_PIX_FMT_D3D11) return *p;
    return AV_PIX_FMT_NONE; // Strict hardware request: never silently use a software format.
}
void decode(AVCodecContext* decoder, const Encoded& fixture, const std::function<void(AVFrame*, double)>& consume) {
    auto output = frame();
    const auto drain = [&] {
        for (;;) {
            const auto started = Clock::now();
            const auto status = avcodec_receive_frame(decoder, output.get());
            const auto elapsed = microseconds(started);
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) break;
            ff(status, "Receive decoded frame");
            consume(output.get(), elapsed);
            av_frame_unref(output.get());
        }
    };
    for (const auto& input : fixture.packets) { ff(avcodec_send_packet(decoder, input.get()), "Send compressed packet"); drain(); }
    ff(avcodec_send_packet(decoder, nullptr), "Flush decoder"); drain();
}
void wait_gpu(ID3D11Device* device, ID3D11DeviceContext* context) {
    D3D11_QUERY_DESC description{D3D11_QUERY_EVENT, 0};
    ComPtr<ID3D11Query> completion;
    require(device->CreateQuery(&description, &completion), "Create GPU completion query");
    context->End(completion.Get()); context->Flush();
    const auto limit = Clock::now() + std::chrono::seconds(2);
    for (;;) {
        BOOL complete = FALSE;
        const auto status = context->GetData(completion.Get(), &complete, sizeof(complete), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        require(status, "GPU completion query");
        if (status == S_OK && complete) break;
        check(Clock::now() < limit, "GPU completion timed out");
        std::this_thread::yield();
    }
    require(device->GetDeviceRemovedReason(), "Decoder device health");
}
json memory(IDXGIAdapter3* adapter) {
    PROCESS_MEMORY_COUNTERS_EX process{};
    process.cb = sizeof(process);
    check(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process), sizeof(process)) != 0,
          "Process memory query failed");
    DXGI_QUERY_VIDEO_MEMORY_INFO local{}, nonlocal{};
    require(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local), "Local video memory query");
    require(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonlocal), "Nonlocal video memory query");
    return {{"workingSetBytes", process.WorkingSetSize}, {"processPeakWorkingSetBytes", process.PeakWorkingSetSize},
            {"privateBytes", process.PrivateUsage}, {"localVideoUsageBytes", local.CurrentUsage},
            {"localVideoBudgetBytes", local.Budget}, {"nonlocalVideoUsageBytes", nonlocal.CurrentUsage}};
}
json percentiles(std::vector<double> values) {
    check(!values.empty(), "Empty timing samples");
    std::sort(values.begin(), values.end());
    const auto rank = [&](double p) { return values[static_cast<std::size_t>(std::ceil(p * static_cast<double>(values.size()))) - 1]; };
    return {{"samples", values.size()}, {"method", "nearest rank"}, {"p50", rank(.5)}, {"p95", rank(.95)},
            {"p99", rank(.99)}, {"max", values.back()}};
}
int clipped(double value) { return std::clamp(static_cast<int>(std::lround(value)), 0, 255); }

json run_case(mapgyver::windows::BgfxProbeDevice& renderer, IDXGIAdapter3* adapter, int side,
              int generation, bool reject_hardware, const std::filesystem::path& fixture_directory) {
    auto fixture = make_fixture(side);
    if (!fixture_directory.empty()) {
        const auto path = fixture_directory / ("mpeg2-" + std::to_string(generation) + ".m2v");
        std::ofstream output{path, std::ios::binary};
        output.write(reinterpret_cast<const char*>(fixture.bytes.data()), static_cast<std::streamsize>(fixture.bytes.size()));
        check(output.good(), "Write fixture failed");
    }
    const auto* definition = avcodec_find_decoder(AV_CODEC_ID_MPEG2VIDEO);
    auto software = codec(definition); software->get_format = software_format;
    ff(avcodec_open2(software.get(), definition, nullptr), "Open software reference decoder");
    std::vector<Frame> references;
    decode(software.get(), fixture, [&](AVFrame* output, double) {
        check(references.size() < frame_count, "Software frame capacity exceeded");
        check(output->format == AV_PIX_FMT_YUV420P && output->width == side && output->height == side,
              "Software reference format/dimensions mismatch");
        Frame retained{av_frame_clone(output)}; check(retained != nullptr, "Retain reference frame failed");
        references.push_back(std::move(retained));
    });
    check(references.size() == frame_count, "Software reference frame count mismatch");
    const auto before = memory(adapter);
    const auto setup_started = Clock::now();
    auto* device = renderer.device();
    ComPtr<ID3D11DeviceContext> context; device->GetImmediateContext(&context);
    Buffer hardware_device{av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA)};
    check(hardware_device != nullptr, "Hardware device context allocation failed");
    auto* device_context = reinterpret_cast<AVHWDeviceContext*>(hardware_device->data);
    auto* native = static_cast<AVD3D11VADeviceContext*>(device_context->hwctx);
    native->device = device; device->AddRef(); // FFmpeg owns/releases this reference.
    ff(av_hwdevice_ctx_init(hardware_device.get()), "Initialize actual D3D11VA device context");
    auto hardware = codec(definition);
    hardware->hw_device_ctx = av_buffer_ref(hardware_device.get());
    check(hardware->hw_device_ctx != nullptr, "Hardware device reference allocation failed");
    hardware->get_format = hardware_format; hardware->opaque = &reject_hardware;
    ff(avcodec_open2(hardware.get(), definition, nullptr), "Open strict D3D11VA decoder");
    ComPtr<ID3D11VideoDevice> video_device; require(device->QueryInterface(IID_PPV_ARGS(&video_device)), "Video device interface");
    ComPtr<ID3D11VideoContext> video_context; require(context.As(&video_context), "Video context interface");
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputWidth = static_cast<UINT>(side); content.InputHeight = static_cast<UINT>(side);
    content.OutputWidth = static_cast<UINT>(side); content.OutputHeight = static_cast<UINT>(side);
    content.InputFrameRate = {30, 1}; content.OutputFrameRate = {30, 1};
    content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
    require(video_device->CreateVideoProcessorEnumerator(&content, &enumerator), "Create video processor enumerator");
    UINT input_support = 0, output_support = 0;
    require(enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_NV12, &input_support), "Check NV12 video input");
    require(enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_B8G8R8A8_UNORM, &output_support), "Check BGRA video output");
    check((input_support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) != 0 &&
          (output_support & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) != 0, "Required video conversion unsupported");
    ComPtr<ID3D11VideoProcessor> processor;
    require(video_device->CreateVideoProcessor(enumerator.Get(), 0, &processor), "Create native video processor");
    D3D11_TEXTURE2D_DESC description{};
    description.Width = static_cast<UINT>(side); description.Height = static_cast<UINT>(side);
    description.MipLevels = 1; description.ArraySize = 1; description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1; description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> converted, staging;
    require(device->CreateTexture2D(&description, nullptr, &converted), "Create GPU conversion target");
    description.Usage = D3D11_USAGE_STAGING; description.BindFlags = 0; description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    require(device->CreateTexture2D(&description, nullptr, &staging), "Create validation staging target");
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_description{};
    output_description.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11VideoProcessorOutputView> output_view;
    require(video_device->CreateVideoProcessorOutputView(converted.Get(), enumerator.Get(), &output_description, &output_view), "Create processor output view");
    const RECT rectangle{0, 0, side, side};
    video_context->VideoProcessorSetStreamFrameFormat(processor.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    video_context->VideoProcessorSetStreamSourceRect(processor.Get(), 0, TRUE, &rectangle);
    video_context->VideoProcessorSetStreamDestRect(processor.Get(), 0, TRUE, &rectangle);
    video_context->VideoProcessorSetOutputTargetRect(processor.Get(), TRUE, &rectangle);
    video_context->VideoProcessorSetStreamAutoProcessingMode(processor.Get(), 0, FALSE);
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE input_color{}; input_color.YCbCr_Matrix = 0; input_color.Nominal_Range = 1;
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE output_color{}; output_color.RGB_Range = 0; output_color.Nominal_Range = 2;
    video_context->VideoProcessorSetStreamColorSpace(processor.Get(), 0, &input_color);
    video_context->VideoProcessorSetOutputColorSpace(processor.Get(), &output_color);
    const auto setup_us = microseconds(setup_started);
    json frames = json::array(); std::vector<double> timings;
    std::uint64_t peak_video = before.at("localVideoUsageBytes").get<std::uint64_t>();
    int max_error = 0;
    decode(hardware.get(), fixture, [&](AVFrame* output, double receive_us) {
        const auto started = Clock::now();
        check(frames.size() < frame_count && output->format == AV_PIX_FMT_D3D11 && output->hw_frames_ctx != nullptr,
              "Strict decoder did not return a bounded D3D11 frame");
        const auto& reference = references[frames.size()];
        check(output->pts == reference->pts && output->pts == static_cast<std::int64_t>(frames.size()), "Decoded rational frame identity mismatch");
        check(output->width == side && output->height == side, "Decoded dimensions mismatch");
        auto* texture = reinterpret_cast<ID3D11Texture2D*>(output->data[0]);
        check(texture != nullptr, "Missing decoder texture");
        ComPtr<ID3D11Device> actual; texture->GetDevice(&actual);
        check(actual.Get() == device, "Decoder used another GPU device");
        D3D11_TEXTURE2D_DESC decoded{}; texture->GetDesc(&decoded);
        const auto slice = reinterpret_cast<std::uintptr_t>(output->data[1]);
        check(decoded.Format == DXGI_FORMAT_NV12 && (decoded.BindFlags & D3D11_BIND_DECODER) != 0 && decoded.ArraySize <= 16 &&
              slice < decoded.ArraySize && decoded.MipLevels == 1, "Unexpected native decoder surface/array");
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_description{};
        input_description.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        input_description.Texture2D.ArraySlice = static_cast<UINT>(slice);
        ComPtr<ID3D11VideoProcessorInputView> input_view;
        require(video_device->CreateVideoProcessorInputView(texture, enumerator.Get(), &input_description, &input_view), "Create decoder input view");
        D3D11_VIDEO_PROCESSOR_STREAM stream{}; stream.Enable = TRUE; stream.pInputSurface = input_view.Get();
        require(video_context->VideoProcessorBlt(processor.Get(), output_view.Get(), 0, 1, &stream), "Convert decoded NV12 on GPU");
        const auto sampled = renderer.sample(converted.Get());
        context->CopyResource(staging.Get(), sampled.texture);
        wait_gpu(device, context.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        require(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map decoder validation pixels");
        int frame_error = 0;
        for (int y = 0; y < side; ++y) {
            const auto* row = static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
            for (int x = 0; x < side; ++x) {
                const double luma = reference->data[0][y * reference->linesize[0] + x] - 16.0;
                const double u = reference->data[1][(y/2) * reference->linesize[1] + x/2] - 128.0;
                const double v = reference->data[2][(y/2) * reference->linesize[2] + x/2] - 128.0;
                const std::array expected{clipped(1.1643835616*luma + 2.0172321429*u),
                    clipped(1.1643835616*luma - .3917622901*u - .8129676472*v),
                    clipped(1.1643835616*luma + 1.5960267857*v), 255};
                for (std::size_t channel = 0; channel < 4; ++channel)
                    frame_error = std::max(frame_error, std::abs(static_cast<int>(row[static_cast<std::size_t>(x)*4+channel]) - expected[channel]));
            }
        }
        context->Unmap(staging.Get(), 0);
        renderer.retire(); // All GPU use completes before AVFrame/array slice can be reused.
        max_error = std::max(max_error, frame_error);
        check(frame_error <= 2, "GPU decoded/converted pixels differ from software BT.601 reference by more than two code values");
        const auto elapsed = microseconds(started); timings.push_back(elapsed);
        const auto snapshot = memory(adapter);
        peak_video = std::max(peak_video, snapshot.at("localVideoUsageBytes").get<std::uint64_t>());
        frames.push_back({{"pts", output->pts}, {"timeBase", {1,30}}, {"arraySlice", slice},
            {"arraySize", decoded.ArraySize}, {"appliedBgfxFrame", sampled.applied_frame},
            {"receiveFrameCallMicroseconds", receive_us}, {"conversionRenderValidationMicroseconds", elapsed},
            {"maxChannelCodeError", frame_error}, {"memory", snapshot}});
    });
    check(frames.size() == frame_count, "Hardware decoder frame count mismatch");
    const auto allocated = memory(adapter);
    hardware.reset(); hardware_device.reset();
    processor.Reset(); enumerator.Reset(); // No decoder slice is retained by this scope.
    output_view.Reset(); converted.Reset(); staging.Reset();
    wait_gpu(device, context.Get());
    std::vector<double> warm{timings.begin()+1, timings.end()};
    return {{"generation", generation}, {"side", side}, {"codec", "mpeg2video"},
        {"fixtureSha256", sha256(fixture.bytes)}, {"fixtureBytes", fixture.bytes.size()},
        {"decodePath", "strict D3D11VA; software fallback forbidden"}, {"nativeFormat", "NV12"},
        {"gpuConversion", "D3D11 video processor: BT.601 limited NV12 to full BGRA8"},
        {"maxChannelCodeError", max_error}, {"channelCodeTolerance", 2}, {"setupMicroseconds", setup_us},
        {"firstFrameConversionRenderValidationMicroseconds", timings.front()}, {"warmConversionRenderValidationMicroseconds", percentiles(warm)},
        {"memoryBefore", before}, {"memoryAllocated", allocated}, {"memoryAfterRetirement", memory(adapter)},
        {"sampledPeakLocalVideoUsageBytes", peak_video}, {"decodedFrames", frame_count},
        {"candidateDrawSubmissions", frame_count}, {"videoProcessorBltSubmissions", frame_count},
        {"applicationDecodedCpuUploadCalls", 0}, {"applicationDecodeReadbacks", 0},
        {"validationGpuCopySubmissions", frame_count}, {"validationCpuReadbacks", frame_count}, {"frames", frames}};
}
} // namespace

int main(int argc, char** argv) {
    try {
        bool reject = false; std::filesystem::path fixtures;
        if (argc == 2 && std::string_view{argv[1]} == "--version") {
            std::cout << "MapGyver M01 decoder probe FFmpeg " << av_version_info() << '\n'; return 0;
        }
        check(argc >= 2 && std::string_view{argv[1]} == "--json", "Usage: decoder-probe --json [--reject-hardware-format] [--fixture-directory path]");
        for (int index = 2; index < argc; ++index) {
            if (std::string_view{argv[index]} == "--reject-hardware-format") reject = true;
            else if (std::string_view{argv[index]} == "--fixture-directory" && index + 1 < argc) fixtures = argv[++index];
            else throw std::runtime_error("Unknown decoder probe argument");
        }
        if (!fixtures.empty()) std::filesystem::create_directories(fixtures);
        mapgyver::windows::BgfxProbeDevice renderer;
        ComPtr<IDXGIDevice> dxgi; require(renderer.device()->QueryInterface(IID_PPV_ARGS(&dxgi)), "Decoder DXGI device");
        ComPtr<IDXGIAdapter> base; require(dxgi->GetAdapter(&base), "Decoder adapter");
        ComPtr<IDXGIAdapter3> adapter; require(base.As(&adapter), "Decoder memory adapter");
        DXGI_ADAPTER_DESC description{}; require(base->GetDesc(&description), "Decoder adapter identity");
        const auto build = mapgyver::build_info();
        json cases = json::array();
        int generation = 0;
        for (const int side : {128,256,128}) cases.push_back(run_case(renderer, adapter.Get(), side, ++generation, reject, fixtures));
        json report{{"schemaVersion",1}, {"evidenceClass","native-gpu-d3d11va-bgfx-decoder-import"}, {"result","passed"},
            {"sourceRevision",build.revision}, {"sourceDirty",build.dirty.has_value() ? json(*build.dirty) : json(nullptr)},
            {"buildConfiguration",build.configuration}, {"ffmpegVersion",av_version_info()}, {"ffmpegLicense",avcodec_license()},
            {"ffmpegConfiguration",avcodec_configuration()},
            {"adapter",{{"vendorId",description.VendorId},{"deviceId",description.DeviceId},
                        {"luidHigh",description.AdapterLuid.HighPart},{"luidLow",description.AdapterLuid.LowPart}}},
            {"timingMeaning","Offline CPU wall times include allocations, GPU waits and readback validation; not show deadline/scanout latency"},
            {"counterMeaning","Explicit application API submissions; no physical decoder/copy-engine counters or claim of hidden SDK copies"},
            {"physicalOutputs","not_tested"}, {"productionDeadlineBudget","not_qualified"}, {"cases",cases}};
        std::cout << report.dump(2) << '\n'; return 0;
    } catch (const std::exception& error) {
        std::cerr << "Decoder probe failed: " << error.what() << '\n'; return 1;
    }
}
