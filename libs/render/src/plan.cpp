// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/render/plan.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace mapgyver::render {
namespace {
void require(bool value,const char* reason) {if (!value) throw Error(reason);}
std::uint64_t bytes(std::uint32_t width,std::uint32_t height,std::uint32_t stride) {
    require(width > 0 && height > 0,"Image/canvas dimensions must be positive");
    const auto pixels = static_cast<std::uint64_t>(width)*height;
    require(pixels <= std::numeric_limits<std::uint64_t>::max()/stride,"Image byte size overflow");
    require(pixels*stride <= std::numeric_limits<std::size_t>::max(),"Image byte size exceeds host address capacity");
    return pixels*stride;
}
float linear(float value) {return value <= .04045F ? value/12.92F : std::pow((value+.055F)/1.055F,2.4F);}
float srgb(float value) {return value <= .0031308F ? value*12.92F : 1.055F*std::pow(value,1.F/2.4F)-.055F;}
Encoding encoding(const domain::Canvas& canvas) {
    require(canvas.pixel_format == "rgba8-unorm","Unsupported configured canvas pixel format");
    require(canvas.colour_space == "srgb" || canvas.colour_space == "linear-srgb","Unsupported configured canvas colour space");
    require(canvas.alpha == "straight" || canvas.alpha == "premultiplied","Unsupported configured canvas alpha convention");
    return {canvas.colour_space == "srgb" ? Transfer::srgb : Transfer::linear,
            canvas.alpha == "straight" ? Alpha::straight : Alpha::premultiplied};
}
const domain::Port& port(const domain::InstancePool& pool,const PortKey& key) {
    return pool.definitions().output({pool.instance(key.first).asset,key.second});
}
std::vector<PortKey> children(const domain::InstancePool& pool,const PortKey& key) {
    const auto& instance = pool.instance(key.first);
    const auto& asset = pool.definitions().asset(instance.asset);
    std::vector<PortKey> result;
    if (const auto* nested = std::get_if<domain::CompositionSource>(&asset.source)) {
        const auto& composition = pool.definitions().composition(nested->composition);
        const auto canvas = composition.canvases.find(key.second);
        if (canvas == composition.canvases.end()) return result;
        try {static_cast<void>(encoding(canvas->second));} catch (const Error&) {return result;}
        for (const auto& layer : composition.layers) {
            if (layer.kind != domain::StreamKind::video) continue;
            const auto* input = std::get_if<domain::InputPort>(&layer.source);
            const auto& bindings = input ? instance.inputs : instance.layers;
            const auto found = bindings.find(input ? input->port : layer.id);
            if (found != bindings.end()) result.emplace_back(found->second.instance,found->second.port);
        }
    }
    return result;
}
void validate_image(const Image& image,const Limits& limits) {
    require(image.generation != 0 && image.pixels && image.width <= limits.dimension && image.height <= limits.dimension,"Invalid prepared image generation/storage/dimensions");
    require(bytes(image.width,image.height,16) <= limits.image_bytes,"Prepared image exceeds byte capacity");
    require(image.pixels->size() == static_cast<std::uint64_t>(image.width)*image.height,"Prepared image storage differs from dimensions");
    for (const auto& p : *image.pixels)
        require(std::isfinite(p.r) && std::isfinite(p.g) && std::isfinite(p.b) && std::isfinite(p.a) && p.a >= 0 && p.a <= 1 &&
            p.r >= 0 && p.g >= 0 && p.b >= 0 && p.r <= p.a && p.g <= p.a && p.b <= p.a,"Prepared SDR image is not finite linear premultiplied data");
}
}
std::shared_ptr<const Plan> compile(const control::ProjectState& project,std::uint64_t revision,const Images& images,Limits limits) {
    require(limits.nodes > 0 && limits.nodes <= std::numeric_limits<std::uint32_t>::max() && limits.layers > 0 &&
            limits.targets > 0 && limits.targets <= std::numeric_limits<std::uint32_t>::max() && limits.dimension > 0,"Render capacities must be positive and indexable");
    const auto& pool = project.instances;
    const auto& root = pool.placements().at(project.root_placement).source;
    std::vector<PortKey> roots{{root.instance,root.port}};
    std::vector<std::pair<Id,PortKey>> surfaces;
    for (const auto& [id,surface] : project.stage.data().surfaces) if (surface.content_placement) {
        const auto& source = pool.placements().at(*surface.content_placement).source;
        roots.emplace_back(source.instance,source.port); surfaces.emplace_back(id,roots.back());
    }
    auto plan = std::make_shared<Plan>(Plan{revision,{},{},{},0,{}});
    std::map<PortKey,std::uint32_t> indices;
    std::set<PortKey> visiting;
    struct Visit {PortKey key; std::vector<PortKey> children; std::size_t next = 0;};
    std::size_t edges = 0;
    std::map<std::pair<Id,std::uint64_t>,const Image*> resources;
    for (const auto& start : roots) {
        if (indices.contains(start)) continue;
        std::vector<Visit> stack;
        visiting.insert(start); stack.push_back({start,children(pool,start)});
        while (!stack.empty()) {
            require(stack.size()+indices.size() <= limits.nodes,"Active render graph exceeds node capacity");
            auto& current = stack.back();
            if (current.next < current.children.size()) {
                const auto child = current.children[current.next++];
                if (indices.contains(child)) continue;
                require(visiting.insert(child).second,"Live render graph contains a cycle");
                stack.push_back({child,children(pool,child)}); continue;
            }
            require(port(pool,current.key).kind == domain::StreamKind::video,"Render root/branch is not a video port");
            const auto& instance = pool.instance(current.key.first);
            const auto& asset = pool.definitions().asset(instance.asset);
            Node node{current.key,1,1,{},{},{},{}};
            if (const auto* composition = std::get_if<domain::CompositionSource>(&asset.source)) {
                const auto& canvases = pool.definitions().composition(composition->composition).canvases;
                const auto found = canvases.find(current.key.second);
                if (found == canvases.end()) plan->issues.push_back({current.key,"canvas_unconfigured"});
                else {
                    node.width = found->second.width; node.height = found->second.height;
                    require(node.width <= limits.dimension && node.height <= limits.dimension,"Configured canvas exceeds render dimension capacity");
                    try {node.encoding = encoding(found->second);}
                    catch (const Error& error) {plan->issues.push_back({current.key,error.what()});}
                    for (const auto& child : current.children) {
                        const auto input = indices.at(child);
                        if (plan->nodes[input].image || !plan->nodes[input].layers.empty()) node.layers.push_back(input);
                    }
                    require(node.layers.size() <= limits.layers-edges,"Active render graph exceeds layer capacity"); edges += node.layers.size();
                }
            } else {
                const auto found = images.find(current.key);
                if (found == images.end()) plan->issues.push_back({current.key,"source_frame_unavailable"});
                else {
                    require(static_cast<bool>(found->second),"Prepared source frame is null");
                    node.image = found->second;
                    node.width = node.image->width; node.height = node.image->height;
                    const auto identity = std::pair{node.image->resource,node.image->generation};
                    const auto [prior,inserted] = resources.emplace(identity,node.image.get());
                    require(inserted || prior->second == node.image.get(),"Resource generation identifies conflicting prepared images");
                    if (inserted) {
                        validate_image(*node.image,limits);
                        const auto size = bytes(node.width,node.height,16);
                        require(size <= limits.image_bytes-plan->image_upload_bytes,"Active prepared images exceed upload capacity");
                        plan->image_upload_bytes += size;
                    }
                }
            }
            const auto index = static_cast<std::uint32_t>(plan->nodes.size());
            indices.emplace(current.key,index); visiting.erase(current.key);
            plan->nodes.push_back(std::move(node)); stack.pop_back();
        }
    }
    plan->preview = indices.at(roots.front());
    for (const auto& [id,key] : surfaces) plan->surfaces.push_back({id,indices.at(key)});
    // Interval-colour compatible targets. Roots stay live through downstream
    // surface/export consumers; a dependency cannot alias its own render target.
    std::vector<std::size_t> last(plan->nodes.size());
    for (std::size_t index = 0; index < last.size(); ++index) {
        last[index] = std::max(last[index],index);
        for (const auto child : plan->nodes[index].layers) last[child] = std::max(last[child],index);
    }
    for (const auto& key : roots) last[indices.at(key)] = plan->nodes.size();
    std::vector<std::size_t> occupied;
    for (std::size_t index = 0; index < plan->nodes.size(); ++index) {
        auto& node = plan->nodes[index];
        if (node.image || node.layers.empty()) continue;
        std::optional<std::size_t> slot;
        for (std::size_t candidate = 0; candidate < plan->targets.size(); ++candidate)
            if (occupied[candidate] < index && plan->targets[candidate].width == node.width && plan->targets[candidate].height == node.height) {slot = candidate; break;}
        if (!slot) {
            require(plan->targets.size() < limits.targets,"Render target pool exceeds slot capacity");
            const auto size = bytes(node.width,node.height,8);
            require(size <= limits.target_bytes-plan->target_bytes,"Render target pool exceeds byte capacity");
            plan->target_bytes += size; slot = plan->targets.size();
            plan->targets.push_back({node.width,node.height}); occupied.push_back(0);
        }
        node.target = static_cast<std::uint32_t>(*slot); occupied[*slot] = last[index];
    }
    std::set<std::uint32_t> exports;
    for (const auto& key : roots) exports.insert(indices.at(key));
    for (const auto index : exports) {
        const auto size = bytes(plan->nodes[index].width,plan->nodes[index].height,4);
        require(size <= limits.total_payload_bytes-plan->export_bytes,"Encoded output targets exceed payload capacity");
        plan->export_bytes += size;
    }
    if (std::any_of(plan->nodes.begin(),plan->nodes.end(),[](const auto& node){return !node.image && !node.target;})) plan->fallback_bytes = 16;
    require(plan->target_bytes <= limits.total_payload_bytes-plan->export_bytes &&
        plan->image_upload_bytes <= limits.total_payload_bytes-plan->export_bytes-plan->target_bytes &&
        plan->fallback_bytes <= limits.total_payload_bytes-plan->export_bytes-plan->target_bytes-plan->image_upload_bytes,"Native texture payload exceeds total capacity");
    return plan;
}
std::shared_ptr<const Image> rgba8(Id id,std::uint64_t generation,std::uint32_t width,std::uint32_t height,
    std::span<const std::uint8_t> input,Encoding code,std::uint64_t limit) {
    require(generation != 0 && bytes(width,height,16) <= limit,"Prepared image exceeds generation/byte capacity");
    require(input.size() == bytes(width,height,4),"RGBA image byte count differs from dimensions");
    auto pixels = std::make_shared<std::vector<Pixel>>(); pixels->reserve(static_cast<std::size_t>(width)*height);
    for (std::size_t offset = 0; offset < input.size(); offset += 4) {
        const float alpha = input[offset+3]/255.F;
        std::array<float,3> colour{input[offset]/255.F,input[offset+1]/255.F,input[offset+2]/255.F};
        for (auto& component : colour) {
            if (code.alpha == Alpha::premultiplied) {
                require(component <= alpha,"Premultiplied RGBA input contains RGB above alpha");
                component = alpha > 0 ? component/alpha : 0.F;
            }
            if (code.transfer == Transfer::srgb) component = linear(component);
            component *= alpha;
        }
        pixels->push_back({colour[0],colour[1],colour[2],alpha});
    }
    return std::make_shared<const Image>(Image{std::move(id),generation,width,height,std::move(pixels)});
}
std::shared_ptr<const Image> pattern(Id id,std::uint64_t generation,std::uint32_t width,std::uint32_t height,std::string_view name,std::uint64_t limit) {
    require(bytes(width,height,16) <= limit,"Pattern exceeds byte capacity");
    require(name == "bars" || name == "checker" || name == "alpha" || name == "gradient","Unknown test-pattern name");
    constexpr std::array<std::array<std::uint8_t,3>,8> bars{{{255,255,255},{255,255,0},{0,255,255},{0,255,0},{255,0,255},{255,0,0},{0,0,255},{0,0,0}}};
    std::vector<std::uint8_t> data(static_cast<std::size_t>(bytes(width,height,4)));
    for (std::uint32_t y = 0; y < height; ++y) for (std::uint32_t x = 0; x < width; ++x) {
        const auto index = (static_cast<std::size_t>(y)*width+x)*4;
        const auto colour = bars[static_cast<std::size_t>(static_cast<std::uint64_t>(x)*8/width)];
        for (std::size_t c = 0; c < 3; ++c) data[index+c] = name == "checker" ? static_cast<std::uint8_t>((x/8+y/8)%2 ? 255 : 0) : colour[c];
        if (name == "gradient") {data[index] = static_cast<std::uint8_t>(static_cast<std::uint64_t>(x)*255/std::max(1U,width-1));data[index+1] = static_cast<std::uint8_t>(static_cast<std::uint64_t>(y)*255/std::max(1U,height-1));data[index+2] = 128;}
        data[index+3] = name == "alpha" || name == "gradient" ? static_cast<std::uint8_t>(static_cast<std::uint64_t>(y)*255/std::max(1U,height-1)) : 255;
    }
    return rgba8(std::move(id),generation,width,height,data,{},limit);
}
Pixel over(Pixel a,Pixel b) noexcept {return {a.r+b.r*(1-a.a),a.g+b.g*(1-a.a),a.b+b.b*(1-a.a),a.a+b.a*(1-a.a)};}
std::vector<std::uint8_t> reference(const Plan& plan,std::uint32_t result) {
    require(result < plan.nodes.size(),"Reference output node is missing");
    std::vector<std::vector<Pixel>> targets;
    for (const auto& target : plan.targets) targets.emplace_back(static_cast<std::size_t>(target.width)*target.height);
    const auto pixel = [&](std::uint32_t index,std::uint32_t x,std::uint32_t y)->Pixel {
        const auto& source = plan.nodes[index];
        if (!source.image && !source.target) return {};
        return source.image ? source.image->pixels->at(static_cast<std::size_t>(y)*source.width+x) : targets.at(*source.target).at(static_cast<std::size_t>(y)*source.width+x);
    };
    for (std::size_t index = 0; index <= result; ++index) {
        const auto& node = plan.nodes[index]; if (!node.target) continue;
        auto& output = targets.at(*node.target); std::fill(output.begin(),output.end(),Pixel{});
        for (const auto input : node.layers) {
            const auto& child = plan.nodes[input];
            for (std::uint32_t y = 0; y < node.height; ++y) for (std::uint32_t x = 0; x < node.width; ++x) {
                const auto sx = static_cast<std::uint32_t>((static_cast<std::uint64_t>(x)*2+1)*child.width/(static_cast<std::uint64_t>(node.width)*2));
                const auto sy = static_cast<std::uint32_t>((static_cast<std::uint64_t>(y)*2+1)*child.height/(static_cast<std::uint64_t>(node.height)*2));
                auto& p = output[static_cast<std::size_t>(y)*node.width+x]; p = over(pixel(input,sx,sy),p);
            }
        }
    }
    const auto& node = plan.nodes[result]; std::vector<std::uint8_t> output(static_cast<std::size_t>(node.width)*node.height*4);
    for (std::uint32_t y = 0; y < node.height; ++y) for (std::uint32_t x = 0; x < node.width; ++x) {
        const auto p = pixel(result,x,y); std::array<float,3> colour{p.r,p.g,p.b};
        const auto offset = (static_cast<std::size_t>(y)*node.width+x)*4;
        for (std::size_t c = 0; c < 3; ++c) {
            auto value = p.a > 0 ? colour[c]/p.a : 0.F;
            if (node.encoding.transfer == Transfer::srgb) value = srgb(value);
            if (node.encoding.alpha == Alpha::premultiplied) value *= p.a;
            output[offset+c] = static_cast<std::uint8_t>(std::lround(std::clamp(value,0.F,1.F)*255));
        }
        output[offset+3] = static_cast<std::uint8_t>(std::lround(std::clamp(p.a,0.F,1.F)*255));
    }
    return output;
}
}
