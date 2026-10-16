// SPDX-License-Identifier: GPL-3.0-or-later
#include "image_loader.hpp"
#include <windows.h>
#include <wincodec.h>
#include <bcrypt.h>
#include <wrl/client.h>
#include <filesystem>
#include <algorithm>
#include <array>
#include <cstring>
#include <thread>

namespace mapgyver::windows {
namespace {
using Microsoft::WRL::ComPtr;
void check(bool value,const char* reason) {if (!value) throw render::Error(reason);}
void hr(HRESULT value,const char* reason) {check(SUCCEEDED(value),reason);}
void cancel(const std::function<bool()>& callback) {if (callback && callback()) throw ImageLoadCancelled{};}
struct Handle {HANDLE value = INVALID_HANDLE_VALUE;~Handle(){if (value != INVALID_HANDLE_VALUE) CloseHandle(value);}};
struct Hash {BCRYPT_HASH_HANDLE value = nullptr;~Hash(){if (value) BCryptDestroyHash(value);}};
struct Variant {PROPVARIANT value{};~Variant(){PropVariantClear(&value);}};
std::uint32_t be32(const std::uint8_t* p) {return (static_cast<std::uint32_t>(p[0])<<24)|(static_cast<std::uint32_t>(p[1])<<16)|(static_cast<std::uint32_t>(p[2])<<8)|p[3];}
struct PngPolicy {bool srgb = false,icc = false;std::uint32_t gamma = 0;};
PngPolicy png_policy(const std::vector<std::uint8_t>& bytes,const std::function<bool()>& cancelled) {
    PngPolicy policy;check(bytes.size() >= 33 && be32(bytes.data()+8) == 13 && std::memcmp(bytes.data()+12,"IHDR",4) == 0,"image_png_invalid");
    check(bytes[24] == 8 || (bytes[25] == 3 && (bytes[24] == 1 || bytes[24] == 2 || bytes[24] == 4)),"image_bit_depth_unavailable");
    std::size_t offset = 8,count = 0;
    while (offset+12 <= bytes.size()) {
        cancel(cancelled);check(++count <= 65536,"image_metadata_capacity");const auto size = be32(bytes.data()+offset);
        check(size <= bytes.size()-offset-12,"image_png_invalid");const auto* type = bytes.data()+offset+4;
        check(std::memcmp(type,"acTL",4) != 0,"image_animation_unavailable");
        check(std::memcmp(type,"eXIf",4) != 0,"image_png_orientation_unavailable");
        if (std::memcmp(type,"sRGB",4) == 0) {check(!policy.srgb && size == 1 && type[4] <= 3,"image_png_invalid");policy.srgb = true;}
        if (std::memcmp(type,"iCCP",4) == 0) policy.icc = true;
        if (std::memcmp(type,"gAMA",4) == 0) {check(!policy.gamma && size == 4,"image_png_invalid");policy.gamma = be32(type+4);check(policy.gamma != 0,"image_png_invalid");}
        offset += size+12;if (std::memcmp(type,"IEND",4) == 0) break;
    }
    check(policy.srgb || policy.icc || policy.gamma == 0 || policy.gamma == 100000,"image_transfer_unavailable");return policy;
}
}
struct ImageLoader::Impl {
    std::filesystem::path root;ImageLoadCounters& counters;ImageLoadLimits limits;
    ComPtr<IWICImagingFactory> factory;BCRYPT_ALG_HANDLE algorithm = nullptr;DWORD hash_size = 0;
    bool com = false;const std::thread::id owner = std::this_thread::get_id();
    struct Cached {ImageFile file;std::uint64_t serial,bytes;};
    std::map<std::string,Cached> cache;std::uint64_t serial = 0,cache_bytes = 0;
    Impl(std::string path,ImageLoadCounters& statistics,ImageLoadLimits capacity) : root(std::u8string{path.begin(),path.end()}),counters(statistics),limits(capacity) {
        check(limits.encoded_bytes && limits.encoded_bytes <= UINT32_MAX && limits.dimension && limits.dimension <= 16384,"image_loader_capacity_invalid");
        const auto result = CoInitializeEx(nullptr,COINIT_MULTITHREADED);hr(result,"image_com_unavailable");com = true;
        try {
            hr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"image_wic_unavailable");
            check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0) >= 0,"image_hash_unavailable");
            DWORD written = 0;check(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&hash_size),sizeof(hash_size),&written,0) >= 0 && hash_size <= 65536,"image_hash_capacity");
        } catch (...) {factory.Reset();if (algorithm) BCryptCloseAlgorithmProvider(algorithm,0);CoUninitialize();com = false;throw;}
    }
    ~Impl() {cache.clear();factory.Reset();if (algorithm) BCryptCloseAlgorithmProvider(algorithm,0);if (com) CoUninitialize();}
    ImageFile load(domain::Id asset,std::uint64_t generation,const std::string& locator,std::uint64_t limit,const std::function<bool()>& cancelled,std::uint64_t read_budget) {
        check(owner == std::this_thread::get_id(),"image_loader_foreign_owner");cancel(cancelled);
        check(!locator.empty() && locator.size() <= 65536 && generation,"image_locator_invalid");
        auto path = std::filesystem::path{std::u8string{locator.begin(),locator.end()}};if (path.is_relative()) path = root/path;
        auto native = std::filesystem::absolute(path).lexically_normal().native();
        if (!native.starts_with(L"\\\\?\\")) native = native.starts_with(L"\\\\") ? L"\\\\?\\UNC\\"+native.substr(2) : L"\\\\?\\"+native;
        Handle file{CreateFileW(native.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr)};
        check(file.value != INVALID_HANDLE_VALUE,GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND ? "image_file_missing" : "image_file_unavailable");
        LARGE_INTEGER size{};check(GetFileSizeEx(file.value,&size) && size.QuadPart > 0 && static_cast<std::uint64_t>(size.QuadPart) <= limits.encoded_bytes,"image_encoded_capacity");
        check(static_cast<std::uint64_t>(size.QuadPart) <= read_budget,"image_request_read_capacity");
        std::vector<std::uint8_t> encoded(static_cast<std::size_t>(size.QuadPart)),hash_object(hash_size);Hash hash;
        check(BCryptCreateHash(algorithm,&hash.value,hash_object.data(),hash_size,nullptr,0,0) >= 0,"image_hash_unavailable");
        for (std::size_t offset = 0; offset < encoded.size();) {
            cancel(cancelled);DWORD read = 0;const auto count = static_cast<DWORD>(std::min<std::size_t>(65536,encoded.size()-offset));
            check(ReadFile(file.value,encoded.data()+offset,count,&read,nullptr) && read,"image_read_failed");counters.read_bytes.fetch_add(read);
            check(BCryptHashData(hash.value,encoded.data()+offset,read,0) >= 0,"image_hash_failed");offset += read;
        }
        CloseHandle(file.value);file.value = INVALID_HANDLE_VALUE;
        cancel(cancelled);std::array<std::uint8_t,32> digest{};check(BCryptFinishHash(hash.value,digest.data(),static_cast<ULONG>(digest.size()),0) >= 0,"image_hash_failed");
        constexpr char hex[] = "0123456789abcdef";std::string key;key.reserve(64);for (const auto byte : digest) {key += hex[byte>>4];key += hex[byte&15];}
        if (const auto found = cache.find(key);found != cache.end()) {
            check(found->second.bytes <= limit,"image_pixel_capacity");found->second.serial = ++serial;counters.cache_hits.fetch_add(1);
            auto result = found->second.file;result.locator = locator;result.image = std::make_shared<const render::Image>(render::Image{std::move(asset),generation,result.image->width,result.image->height,result.image->pixels});return result;
        }
        constexpr std::array<std::uint8_t,8> signature{137,80,78,71,13,10,26,10};
        const bool png = encoded.size() >= 8 && std::equal(signature.begin(),signature.end(),encoded.begin());
        check(png || (encoded.size() >= 2 && encoded[0] == 255 && encoded[1] == 216),"image_format_unavailable");
        const auto policy = png ? png_policy(encoded,cancelled) : PngPolicy{};
        if (png) {
            const auto width = be32(encoded.data()+16),height = be32(encoded.data()+20);
            check(width && height && width <= limits.dimension && height <= limits.dimension && static_cast<std::uint64_t>(width)*height*16 <= limit,"image_pixel_capacity");
        }
        ComPtr<IWICStream> stream;hr(factory->CreateStream(&stream),"image_stream_failed");
        hr(stream->InitializeFromMemory(encoded.data(),static_cast<DWORD>(encoded.size())),"image_stream_failed");
        ComPtr<IWICBitmapDecoder> decoder;hr(factory->CreateDecoder(png ? GUID_ContainerFormatPng : GUID_ContainerFormatJpeg,&GUID_VendorMicrosoft,&decoder),"image_decoder_unavailable");
        ComPtr<IWICBitmapDecoderInfo> info;hr(decoder->GetDecoderInfo(&info),"image_decoder_unavailable");GUID vendor{};hr(info->GetVendorGUID(&vendor),"image_decoder_unavailable");check(vendor == GUID_VendorMicrosoft,"image_decoder_vendor_unavailable");
        hr(decoder->Initialize(stream.Get(),WICDecodeMetadataCacheOnDemand),"image_decode_failed");UINT frames = 0;hr(decoder->GetFrameCount(&frames),"image_decode_failed");check(frames == 1,"image_animation_unavailable");
        ComPtr<IWICBitmapFrameDecode> frame;hr(decoder->GetFrame(0,&frame),"image_decode_failed");UINT width = 0,height = 0;hr(frame->GetSize(&width,&height),"image_decode_failed");
        const auto payload = static_cast<std::uint64_t>(width)*height*16;
        check(width && height && width <= limits.dimension && height <= limits.dimension && payload <= limit,"image_pixel_capacity");
        WICPixelFormatGUID format{};hr(frame->GetPixelFormat(&format),"image_format_unavailable");
        check(format == GUID_WICPixelFormat24bppBGR || format == GUID_WICPixelFormat24bppRGB || format == GUID_WICPixelFormat32bppBGR ||
            format == GUID_WICPixelFormat32bppBGRA || format == GUID_WICPixelFormat32bppRGBA || format == GUID_WICPixelFormat8bppGray ||
            format == GUID_WICPixelFormat8bppIndexed,"image_pixel_format_unavailable");
        unsigned orientation = 1;
        if (!png) {
            ComPtr<IWICMetadataQueryReader> reader;const auto available = frame->GetMetadataQueryReader(&reader);
            if (SUCCEEDED(available)) {
                Variant field;const auto read = reader->GetMetadataByName(L"/app1/ifd/{ushort=274}",&field.value);
                const auto& value = field.value;
                if (SUCCEEDED(read)) {check(value.vt == VT_UI2 || value.vt == VT_UI4,"image_orientation_invalid");orientation = value.vt == VT_UI2 ? value.uiVal : value.ulVal;}
                else check(read == WINCODEC_ERR_PROPERTYNOTFOUND || read == WINCODEC_ERR_UNSUPPORTEDOPERATION,"image_metadata_invalid");
                check(orientation >= 1 && orientation <= 8,"image_orientation_invalid");
            } else check(available == WINCODEC_ERR_UNSUPPORTEDOPERATION,"image_metadata_invalid");
        }
        ComPtr<IWICBitmapSource> source;ComPtr<IWICFormatConverter> converter;
        hr(factory->CreateFormatConverter(&converter),"image_conversion_failed");
        hr(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"image_conversion_failed");source = converter;
        std::string colour = policy.gamma == 100000 && !policy.srgb && !policy.icc ? "linear_png" : "srgb_untagged";
        UINT contexts = 0;const auto context_result = frame->GetColorContexts(0,nullptr,&contexts);
        check(SUCCEEDED(context_result) || context_result == WINCODEC_ERR_UNSUPPORTEDOPERATION,"image_colour_metadata_invalid");check(contexts <= 4,"image_colour_metadata_capacity");
        if (contexts) {
            std::array<ComPtr<IWICColorContext>,4> owners;std::array<IWICColorContext*,4> pointers{};
            for (UINT i = 0; i < contexts; ++i) {hr(factory->CreateColorContext(&owners[i]),"image_colour_unavailable");pointers[i] = owners[i].Get();}
            UINT actual = 0;hr(frame->GetColorContexts(contexts,pointers.data(),&actual),"image_colour_unavailable");check(actual == contexts,"image_colour_metadata_invalid");
            ComPtr<IWICColorContext> destination;hr(factory->CreateColorContext(&destination),"image_colour_unavailable");hr(destination->InitializeFromExifColorSpace(1),"image_colour_unavailable");
            ComPtr<IWICColorTransform> transform;hr(factory->CreateColorTransformer(&transform),"image_colour_unavailable");
            hr(transform->Initialize(converter.Get(),owners[0].Get(),destination.Get(),GUID_WICPixelFormat32bppRGBA),"image_colour_unavailable");source = transform;colour = "wic_srgb_transform";
        } else {check(!policy.icc,"image_colour_unavailable");if (policy.srgb) colour = "srgb_declared";}
        const UINT output_width = orientation >= 5 ? height : width,output_height = orientation >= 5 ? width : height;
        auto pixels = std::make_shared<std::vector<render::Pixel>>(static_cast<std::size_t>(width)*height);
        std::vector<std::uint8_t> stripe(static_cast<std::size_t>(width)*std::min(64U,height)*4);
        for (UINT y = 0; y < height; y += 64) {
            cancel(cancelled);const auto rows = std::min(64U,height-y);WICRect rect{0,static_cast<INT>(y),static_cast<INT>(width),static_cast<INT>(rows)};
            const auto count = width*rows*4;hr(source->CopyPixels(&rect,width*4,count,stripe.data()),"image_decode_failed");counters.decoded_rgba8_bytes.fetch_add(count);
            const auto linear = render::rgba8(asset,generation,width,rows,std::span{stripe.data(),count},{colour == "linear_png" ? render::Transfer::linear : render::Transfer::srgb,render::Alpha::straight},limit);
            for (UINT row = 0; row < rows; ++row) for (UINT x = 0; x < width; ++x) {
                const auto sy = y+row;UINT dx = x,dy = sy;
                switch (orientation) {
                case 2: dx = width-1-x;break;case 3: dx = width-1-x;dy = height-1-sy;break;case 4: dy = height-1-sy;break;
                case 5: dx = sy;dy = x;break;case 6: dx = height-1-sy;dy = x;break;
                case 7: dx = height-1-sy;dy = width-1-x;break;case 8: dx = sy;dy = width-1-x;break;default:break;
                }
                (*pixels)[static_cast<std::size_t>(dy)*output_width+dx] = (*linear->pixels)[static_cast<std::size_t>(row)*width+x];
            }
        }
        cancel(cancelled);counters.decoded_files.fetch_add(1);
        ImageFile result{std::make_shared<const render::Image>(render::Image{std::move(asset),generation,output_width,output_height,std::move(pixels)}),locator,key,png ? "png" : "jpeg",colour,encoded.size(),orientation};
        if (limits.cache_entries && payload <= limits.cache_bytes) {
            while (!cache.empty() && (cache.size() >= limits.cache_entries || payload > limits.cache_bytes-cache_bytes)) {
                auto victim = std::min_element(cache.begin(),cache.end(),[](const auto& a,const auto& b){return a.second.serial < b.second.serial;});cache_bytes -= victim->second.bytes;cache.erase(victim);counters.cache_evictions.fetch_add(1);
            }
            cache.emplace(key,Cached{result,++serial,payload});cache_bytes += payload;
            counters.peak_cache_bytes.store(std::max(counters.peak_cache_bytes.load(),cache_bytes));
        }
        return result;
    }
};
ImageLoader::ImageLoader(std::string root,ImageLoadCounters& counters,ImageLoadLimits limits) : impl_(std::make_unique<Impl>(std::move(root),counters,limits)) {}
ImageLoader::~ImageLoader() = default;
ImageFile ImageLoader::load(domain::Id asset,std::uint64_t generation,const std::string& locator,std::uint64_t limit,const std::function<bool()>& cancelled,std::uint64_t read_budget) {
    return impl_->load(std::move(asset),generation,locator,limit,cancelled,read_budget);
}
}
