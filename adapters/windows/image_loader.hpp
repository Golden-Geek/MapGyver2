// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mapgyver/render/plan.hpp>
#include <atomic>
#include <functional>
namespace mapgyver::windows {
struct ImageLoadLimits {std::uint64_t encoded_bytes = 67108864,cache_bytes = 268435456;std::size_t cache_entries = 64;std::uint32_t dimension = 16384;};
struct ImageLoadCounters {
    std::atomic<std::uint64_t> read_bytes{0},decoded_rgba8_bytes{0},decoded_files{0},cache_hits{0},cache_evictions{0},peak_cache_bytes{0};
};
struct ImageFile {
    std::shared_ptr<const render::Image> image;
    std::string locator,sha256,container,colour_policy;
    std::uint64_t encoded_bytes;
    unsigned orientation;
};
class ImageLoadCancelled : public std::runtime_error {public: ImageLoadCancelled() : std::runtime_error("image_load_cancelled") {}};
// One preparation-thread owner. IO, WIC/COM, hashing, conversion and eviction
// never run on the deadline owner. No native SDK types cross this header.
class ImageLoader {
public:
    ImageLoader(std::string root,ImageLoadCounters& counters,ImageLoadLimits limits = {});
    ~ImageLoader();
    [[nodiscard]] ImageFile load(domain::Id asset,std::uint64_t generation,const std::string& locator,std::uint64_t pixel_byte_limit,
        const std::function<bool()>& cancelled = {},std::uint64_t read_budget = UINT64_MAX);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
