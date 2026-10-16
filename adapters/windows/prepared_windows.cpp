// SPDX-License-Identifier: GPL-3.0-or-later
#include "prepared_windows.hpp"
#include <algorithm>
#include <set>

namespace mapgyver::windows {
namespace {
void check(bool value,const char* message) {if (!value) throw render::Error(message);}
bool footprint(const OutputWindow& a,const OutputWindow& b) {
    return a.id == b.id && a.width == b.width && a.height == b.height && a.hidden == b.hidden && a.desktop_position == b.desktop_position;
}
}
bool same_windows(const std::vector<OutputWindow>& first,const std::vector<OutputWindow>& second) {
    if (first.size() != second.size()) return false;
    for (std::size_t i = 0; i < first.size(); ++i) if (!footprint(first[i],second[i])) return false;
    return true;
}
std::unique_ptr<PreparedWindows> prepare_windows(const PreparationDevice& device,const std::vector<OutputWindow>& current,
    std::vector<OutputWindow> desired,std::uint64_t generation,GraphLimits limits) {
    check(generation && desired.size() <= 64,"Prepared window generation/count invalid");
    auto result = std::unique_ptr<PreparedWindows>{new PreparedWindows{generation}};
    result->outputs.reserve(desired.size());result->reuse.reserve(desired.size());std::set<domain::Id> ids;
    std::uint64_t new_payload = 0, desired_payload = 0;std::size_t created = 0;
    for (auto& config : desired) {
        check(ids.insert(config.id).second && config.width && config.height && config.width <= limits.dimension && config.height <= limits.dimension,
            "Prepared window identity/dimensions invalid");
        check(config.name.size() <= 65536,"Prepared window title exceeds bounded capacity");
        const auto bytes = static_cast<std::uint64_t>(config.width)*config.height*8;
        check(bytes <= 536870912-desired_payload,"Prepared window payload exceeds capacity");desired_payload += bytes;
        const auto found = std::find_if(current.begin(),current.end(),[&](const auto& old){return footprint(old,config);});
        const auto reuse = found == current.end() ? SIZE_MAX : static_cast<std::size_t>(found-current.begin());
        if (reuse == SIZE_MAX) {++created;new_payload += bytes;}
        result->outputs.push_back({std::move(config)});result->reuse.push_back(reuse);
    }
    check(limits.active_payload <= 536870896 && new_payload <= 536870896-limits.active_payload &&
        limits.active_textures+created*2 <= limits.textures && limits.active_framebuffers+created*3 <= limits.framebuffers,
        "Prepared windows exceed concurrent payload/handle capacity");
    result->combined_limits = limits;result->combined_limits.windows = result->outputs.size();
    result->combined_limits.output_payload = desired_payload;result->combined_limits.active_payload += new_payload;
    result->combined_limits.active_textures += created*2;result->combined_limits.active_framebuffers += created*3;
    for (std::size_t i = 0; i < result->outputs.size(); ++i) if (result->reuse[i] == SIZE_MAX) {
        auto& output = result->outputs[i];
        auto held = device.target(output.configured.width,output.configured.height,DXGI_FORMAT_R8G8B8A8_UNORM);
        auto presented = device.target(output.configured.width,output.configured.height,DXGI_FORMAT_R8G8B8A8_UNORM);
        output.held.native = std::move(held.texture);output.held.borrowed = true;
        output.presented.native = std::move(presented.texture);output.presented.borrowed = true;
    }
    const D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
    check(SUCCEEDED(device.native()->CreateQuery(&query,&result->completion)),"Prepared window completion allocation failed");
    result->owner_device = device.native();return result;
}
}
