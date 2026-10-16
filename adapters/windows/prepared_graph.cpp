// SPDX-License-Identifier: GPL-3.0-or-later
#include "prepared_graph.hpp"
#include <algorithm>
#include <set>

namespace mapgyver::windows {
namespace {
void check(bool value,const char* message) {if (!value) throw render::Error(message);}
}
void GraphResources::swap(GraphResources& other) noexcept {
    plan.swap(other.plan);targets.swap(other.targets);exports.swap(other.exports);inputs.swap(other.inputs);images.swap(other.images);
    exported_nodes.swap(other.exported_nodes);views.swap(other.views);
    std::swap(render_views,other.render_views);std::swap(owned_payload_bytes,other.owned_payload_bytes);
}
void PreparedGraph::list_bindings() {
    binding_count = 0;
    for (auto* group : {&resources.targets,&resources.exports}) for (auto& texture : *group) {
        D3D11_TEXTURE2D_DESC description{};texture.native->GetDesc(&description);
        const auto format = description.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? bgfx::TextureFormat::RGBA16F : bgfx::TextureFormat::RGBA8;
        bindings[binding_count++] = {&texture.texture,&texture.framebuffer,texture.native.Get(),format,
            static_cast<std::uint16_t>(description.Width),static_cast<std::uint16_t>(description.Height)};
    }
    for (auto& [key,input] : resources.images) {
        static_cast<void>(key);
        bindings[binding_count++] = {&input.texture,nullptr,input.image.texture.Get(),bgfx::TextureFormat::RGBA32F,
            static_cast<std::uint16_t>(input.image.width),static_cast<std::uint16_t>(input.image.height)};
    }
    cursor = 0;imported = false;
}
bool same_graph_layout(const render::Plan& first,const render::Plan& second) {
    if (first.nodes.size() != second.nodes.size() || first.targets.size() != second.targets.size() ||
        first.surfaces.size() != second.surfaces.size() || first.preview != second.preview || first.output_mappings != second.output_mappings) return false;
    for (std::size_t i = 0; i < first.targets.size(); ++i) if (first.targets[i].width != second.targets[i].width || first.targets[i].height != second.targets[i].height) return false;
    for (std::size_t i = 0; i < first.surfaces.size(); ++i) if (first.surfaces[i].id != second.surfaces[i].id || first.surfaces[i].node != second.surfaces[i].node) return false;
    for (std::size_t i = 0; i < first.nodes.size(); ++i) {
        const auto& a = first.nodes[i];const auto& b = second.nodes[i];
        if (a.source != b.source || a.width != b.width || a.height != b.height || a.target != b.target || a.layers != b.layers ||
            static_cast<bool>(a.image) != static_cast<bool>(b.image) || (a.image && a.image->resource != b.image->resource)) return false;
    }
    return true;
}
std::unique_ptr<PreparedGraph> prepare_graph(const PreparationDevice& device,std::shared_ptr<const render::Plan> plan,
    std::uint64_t generation,GraphLimits limits,std::vector<std::uint32_t> output_nodes) {
    check(plan && generation && !plan->nodes.empty() && plan->nodes.size() <= render::Limits{}.nodes &&
        plan->targets.size() <= render::Limits{}.targets && plan->preview < plan->nodes.size(),"Prepared graph is invalid/over capacity");
    auto result = std::unique_ptr<PreparedGraph>{new PreparedGraph{generation}};auto& resources = result->resources;
    resources.plan = std::move(plan);const auto& value = *resources.plan;
    resources.exported_nodes.push_back(value.preview);
    for (const auto& surface : value.surfaces) {
        check(surface.node < value.nodes.size(),"Prepared graph surface refers to an absent node");
        if (std::find(resources.exported_nodes.begin(),resources.exported_nodes.end(),surface.node) == resources.exported_nodes.end()) resources.exported_nodes.push_back(surface.node);
    }
    check(output_nodes.size() == limits.windows,"Prepared graph output routes differ from existing windows");
    for (const auto node : output_nodes) check(std::find(resources.exported_nodes.begin(),resources.exported_nodes.end(),node) != resources.exported_nodes.end(),"Prepared graph routes an unexported output");
    result->output_nodes = std::move(output_nodes);
    std::map<std::pair<domain::Id,std::uint64_t>,std::shared_ptr<const render::Image>> images;
    std::uint64_t image_bytes = 0, target_bytes = 0, export_bytes = 0, draw_count = resources.exported_nodes.size();std::size_t layer_count = 0;
    resources.views.resize(value.nodes.size());resources.inputs.resize(value.nodes.size(),BGFX_INVALID_HANDLE);
    for (std::size_t i = 0; i < value.nodes.size(); ++i) {
        const auto& node = value.nodes[i];
        check(node.width && node.height && node.width <= limits.dimension && node.height <= limits.dimension,"Prepared graph node dimensions exceed actual capacity");
        check(node.layers.size() <= render::Limits{}.layers-layer_count,"Prepared graph layer capacity exceeded");layer_count += node.layers.size();
        draw_count += node.layers.size();for (const auto child : node.layers) check(child < i,"Prepared graph dependency order invalid");
        if (node.target) {
            check(*node.target < value.targets.size() && !node.image,"Prepared graph node target is invalid");
            check(value.targets[*node.target].width == node.width && value.targets[*node.target].height == node.height,"Prepared graph target dimensions differ from node");
            resources.views[i] = static_cast<bgfx::ViewId>(resources.render_views++);
        }
        if (node.image) {
            check(node.image->width == node.width && node.image->height == node.height,"Prepared graph image dimensions differ from node");
            const auto [found,inserted] = images.emplace(std::pair{node.image->resource,node.image->generation},node.image);
            check(inserted || found->second == node.image,"Prepared graph image identity conflicts");
            if (inserted) image_bytes += static_cast<std::uint64_t>(node.width)*node.height*16;
        }
    }
    for (const auto& target : value.targets) {
        check(target.width && target.height && target.width <= limits.dimension && target.height <= limits.dimension,"Prepared graph target dimensions exceed actual capacity");
        target_bytes += static_cast<std::uint64_t>(target.width)*target.height*8;
    }
    for (const auto node : resources.exported_nodes) export_bytes += static_cast<std::uint64_t>(value.nodes[node].width)*value.nodes[node].height*4;
    const auto count = value.targets.size()+resources.exported_nodes.size()+images.size();
    for (const auto& output : value.output_mappings) draw_count += output.patches.size();
    check(images.size() <= 256 && resources.render_views+resources.exported_nodes.size()+limits.windows*4 <= limits.views &&
        draw_count+limits.windows*4 <= limits.draws && limits.active_textures+count <= limits.textures &&
        limits.active_framebuffers+value.targets.size()+resources.exported_nodes.size() <= limits.framebuffers,"Prepared graph exceeds actual concurrent handle/view/draw capacity");
    const auto candidate_bytes = target_bytes+export_bytes+image_bytes;
    check(target_bytes <= render::Limits{}.target_bytes && image_bytes <= render::Limits{}.image_bytes &&
        limits.active_payload <= 536870896 && candidate_bytes <= 536870896-limits.active_payload,
        "Prepared graph active/candidate/placeholder payload exceeds 512 MiB");
    resources.owned_payload_bytes = candidate_bytes+limits.output_payload+16; // Persistent transparent constant.
    result->bindings.resize(std::max(count,limits.active_textures));
    resources.targets.reserve(value.targets.size());resources.exports.reserve(resources.exported_nodes.size());
    for (const auto& target : value.targets) {
        auto native = device.target(target.width,target.height,DXGI_FORMAT_R16G16B16A16_FLOAT);
        resources.targets.push_back({BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,std::move(native.texture),true});
    }
    for (const auto node : resources.exported_nodes) {
        auto native = device.target(value.nodes[node].width,value.nodes[node].height,DXGI_FORMAT_R8G8B8A8_UNORM);
        resources.exports.push_back({BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,std::move(native.texture),true});
    }
    for (const auto& [key,image] : images) resources.images.emplace(key,GraphInput{BGFX_INVALID_HANDLE,device.prepare(*image,true)});
    const D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
    check(SUCCEEDED(device.native()->CreateQuery(&query,&result->completion)),"Prepared graph completion allocation failed");
    result->owner_device = device.native();result->list_bindings();return result;
}
}
