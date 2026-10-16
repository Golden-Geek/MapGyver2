// SPDX-License-Identifier: GPL-3.0-or-later
#include "adapters/windows/image_loader.hpp"
#include <mapgyver/build_info.hpp>
#include <nlohmann/json.hpp>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
using namespace mapgyver;
using Microsoft::WRL::ComPtr;
namespace {
void check(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
void hr(HRESULT value,const char* message) {check(SUCCEEDED(value),message);}
void jpeg(const std::filesystem::path& path,unsigned orientation) {
    ComPtr<IWICImagingFactory> factory;hr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"JPEG factory failed");
    ComPtr<IWICStream> stream;hr(factory->CreateStream(&stream),"JPEG stream failed");hr(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),"JPEG output failed");
    ComPtr<IWICBitmapEncoder> encoder;hr(factory->CreateEncoder(GUID_ContainerFormatJpeg,&GUID_VendorMicrosoft,&encoder),"JPEG encoder unavailable");hr(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"JPEG encoder failed");
    ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> options;hr(encoder->CreateNewFrame(&frame,&options),"JPEG frame failed");hr(frame->Initialize(options.Get()),"JPEG frame initialize failed");
    hr(frame->SetSize(16,8),"JPEG size failed");WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;hr(frame->SetPixelFormat(&format),"JPEG format failed");check(format == GUID_WICPixelFormat24bppBGR,"JPEG pixel format changed");
    ComPtr<IWICMetadataQueryWriter> metadata;hr(frame->GetMetadataQueryWriter(&metadata),"JPEG metadata unavailable");PROPVARIANT value{};value.vt = VT_UI2;value.uiVal = static_cast<USHORT>(orientation);
    hr(metadata->SetMetadataByName(L"/app1/ifd/{ushort=274}",&value),"JPEG orientation write failed");
    std::array<BYTE,16*8*3> pixels{};for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 16; ++x) {const auto i = (y*16+x)*3;pixels[i] = 128;pixels[i+1] = static_cast<BYTE>(y*255/7);pixels[i+2] = static_cast<BYTE>(x*255/15);}
    hr(frame->WritePixels(8,48,static_cast<UINT>(pixels.size()),pixels.data()),"JPEG pixel write failed");hr(frame->Commit(),"JPEG commit failed");hr(encoder->Commit(),"JPEG encoder commit failed");
}
nlohmann::json run(const std::filesystem::path& root) {
    const auto root_bytes = root.u8string();std::ifstream input{root/"fixtures.json"};nlohmann::json fixtures;input >> fixtures;
    windows::ImageLoadCounters counters;windows::ImageLoader loader{{root_bytes.begin(),root_bytes.end()},counters};
    auto load = [&](const std::string& name,std::uint64_t generation = 1) {return loader.load(domain::Id{"image"},generation,name,134217728);};
    const auto first = load("first.png");check(first.sha256 == fixtures["first.png"]["sha256"].get<std::string>() && first.encoded_bytes == fixtures["first.png"]["bytes"].get<std::uint64_t>(),"Actual SHA-256/read size differs from fixture bytes");
    check(first.image->width == 257 && first.image->height == 131 && first.image->pixels->back().r == 1 && first.image->pixels->front().a == 0,"PNG dimensions/straight alpha/source colour changed");
    const auto reused = load("first.png",2);check(reused.image->pixels == first.image->pixels && reused.image->generation == 2 && counters.cache_hits == 1,"Content cache failed immutable generation reuse");
    const auto unicode = load("éclairage_雪.png",3);check(unicode.sha256 == first.sha256,"Unicode filename changed content identity");
    const auto long_path = load(fixtures["longPath"].get<std::string>(),4);check(long_path.sha256 == first.sha256,"Long relative path failed private native resolution");
    const auto declared = load("declared.png",5);check(declared.image->pixels->back().r == 1,"Declared sRGB conversion failed");
    const auto linear = load("linear.png",6);check(linear.image->pixels->front().r > .50F && linear.image->pixels->front().r < .51F,"Linear PNG gamma was treated as sRGB");
    const auto capacity_reads = counters.read_bytes.load(),capacity_decodes = counters.decoded_files.load();
    bool read_capacity = false,pixel_capacity = false;
    try {static_cast<void>(loader.load(domain::Id{"capacity"},1,"first.png",134217728,{},first.encoded_bytes-1));}
    catch (const render::Error& error) {read_capacity = std::string_view{error.what()} == "image_request_read_capacity";}
    check(read_capacity && counters.read_bytes == capacity_reads,"Request read capacity did not reject before reading cached content");
    try {static_cast<void>(loader.load(domain::Id{"capacity"},1,"first.png",257*131*16-1));}
    catch (const render::Error& error) {pixel_capacity = std::string_view{error.what()} == "image_pixel_capacity";}
    check(pixel_capacity && counters.decoded_files == capacity_decodes,"Cached content bypassed the request pixel capacity");
    const std::array names{"missing.png","corrupt.png","sixteen.png","unsupported-gamma.png","wide.png"};unsigned rejected = 0;
    for (const auto* name : names) {try {static_cast<void>(load(name));} catch (const render::Error&) {++rejected;}}
    check(rejected == names.size(),"Missing/corrupt/depth/transfer/dimension requests were accepted");
    bool before = false;try {static_cast<void>(loader.load(domain::Id{"cancel"},1,"first.png",134217728,[]{return true;}));} catch (const windows::ImageLoadCancelled&) {before = true;}
    const auto read_before = counters.read_bytes.load();unsigned calls = 0;bool during = false;
    try {static_cast<void>(loader.load(domain::Id{"cancel"},1,"noise.png",134217728,[&]{return ++calls == 3;}));} catch (const windows::ImageLoadCancelled&) {during = true;}
    check(before && during && counters.read_bytes-read_before == 65536,"Cancellation did not stop between bounded read chunks");
    bool foreign = false;std::thread other{[&]{try {static_cast<void>(load("first.png"));} catch (const render::Error&) {foreign = true;}}};other.join();check(foreign,"Foreign thread used decoder/COM/cache ownership");
    jpeg(root/"base.jpg",1);jpeg(root/"rotated.jpg",6);
    const auto base = load("base.jpg",7),rotated = load("rotated.jpg",8);
    check(base.container == "jpeg" && base.image->width == 16 && rotated.image->width == 8 && rotated.image->height == 16 && rotated.orientation == 6,"JPEG dimensions/orientation unavailable");
    for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 16; ++x)
        check((*base.image->pixels)[y*16+x] == (*rotated.image->pixels)[x*8+(7-y)],"EXIF rotation did not preserve decoded pixels");
    constexpr std::array<std::array<unsigned,3>,8> corners{{{0,15,112},{15,0,127},{127,112,15},{112,127,0},
        {0,112,15},{112,0,127},{127,15,112},{15,127,0}}};
    for (unsigned orientation = 2; orientation <= 8; ++orientation) {
        const auto name = "orientation-"+std::to_string(orientation)+".jpg";jpeg(root/name,orientation);const auto result = load(name,20+orientation);
        const auto w = orientation >= 5 ? 8U : 16U,h = orientation >= 5 ? 16U : 8U;
        check(result.image->width == w && result.image->height == h && result.orientation == orientation,"EXIF orientation dimensions changed");
        check(result.image->pixels->at(0) == base.image->pixels->at(corners[orientation-1][0]) &&
            result.image->pixels->at(w-1) == base.image->pixels->at(corners[orientation-1][1]) &&
            result.image->pixels->at(static_cast<std::size_t>(w)*(h-1)) == base.image->pixels->at(corners[orientation-1][2]),"EXIF corner orientation differs from its specified transform");
    }
    windows::ImageLoadCounters bounded;windows::ImageLoadLimits limits;limits.cache_entries = 1;limits.cache_bytes = 600000;
    windows::ImageLoader tiny{{root_bytes.begin(),root_bytes.end()},bounded,limits};
    const auto retained = tiny.load(domain::Id{"a"},1,"first.png",134217728);
    static_cast<void>(tiny.load(domain::Id{"b"},2,"second.png",134217728));static_cast<void>(tiny.load(domain::Id{"a"},3,"first.png",134217728));
    check(bounded.cache_evictions == 2 && bounded.peak_cache_bytes <= limits.cache_bytes && retained.image->pixels->back().r == 1,"Cache capacity/retained ownership failed");
    std::filesystem::copy_file(root/"first.png",root/"changed.png",std::filesystem::copy_options::overwrite_existing);
    const auto original = load("changed.png",9);std::filesystem::copy_file(root/"second.png",root/"changed.png",std::filesystem::copy_options::overwrite_existing);
    const auto changed = load("changed.png",10);check(changed.sha256 != original.sha256 && changed.image->pixels->back().r == 0 && original.image->pixels->back().r == 1,"Changed file reused stale content or mutated an old frame");
    std::array<wchar_t,32768> module_path{};const auto module = GetModuleHandleW(L"windowscodecs.dll");
    check(module != nullptr,"Actual WIC module identity unavailable");const auto length = GetModuleFileNameW(module,module_path.data(),static_cast<DWORD>(module_path.size()));
    check(length && length < module_path.size(),"Actual WIC module path unavailable");const auto module_utf8 = std::filesystem::path{std::wstring{module_path.data(),length}}.u8string();
    return {{"evidenceClass","native-Windows-WIC-CPU-image-decode"},{"decodePath","wic_microsoft_cpu_rgba8"},{"pngSha256",first.sha256},
        {"codecModule",std::string{module_utf8.begin(),module_utf8.end()}},{"qualifiedExifOrientations",{1,2,3,4,5,6,7,8}},
        {"rejectedRequests",rejected},{"requestCapacityRejected",read_capacity && pixel_capacity},{"cancelBeforeRead",before},{"cancelDuringRead",during},{"foreignOwnerRejected",foreign},
        {"unicodeAndLongRelativePaths",true},{"jpegExifRotation",true},{"contentHashInvalidation",true},{"cacheEvictions",bounded.cache_evictions.load()},
        {"peakBoundedCachePayloadBytes",bounded.peak_cache_bytes.load()},{"fileReadCpuBytes",counters.read_bytes.load()},
        {"decodedRgba8CpuBytes",counters.decoded_rgba8_bytes.load()},{"decodedFiles",counters.decoded_files.load()},
        {"scope","Actual built-in WIC PNG/JPEG software decode, SHA-256, colour/alpha, relative/unicode/long paths, EXIF rotation, bounded cache, immutable ownership, rejection and cooperative cancellation. No GPU decode, file watcher, browser upload, video playback or hardware qualification."}};
}
}
int main(int argc,char** argv) {try {
    if (argc == 2 && std::string_view{argv[1]} == "--version") {std::cout << "MapGyver WIC image loader qualification\n";return 0;}
    if (argc != 3 || std::string_view{argv[2]} != "--json") return 2;
    auto result = run(std::filesystem::path{std::u8string{argv[1],argv[1]+std::char_traits<char>::length(argv[1])}});
    const auto build = build_info();result["sourceRevision"] = build.revision;result["sourceDirty"] = build.dirty ? nlohmann::json(*build.dirty) : nlohmann::json(nullptr);result["configuration"] = build.configuration;
    std::cout << result.dump(2) << '\n';return std::cout ? 0 : 1;
} catch (const std::exception& error) {std::cerr << error.what() << '\n';return 1;}}
