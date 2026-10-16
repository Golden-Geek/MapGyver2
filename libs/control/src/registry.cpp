// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/control/registry.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <type_traits>

namespace mapgyver::control {
namespace {
void require(bool valid, RegistryErrorCode code, const char* message) {
    if (!valid) throw RegistryError(code,message);
}
template<class T> T& find(std::vector<T>& values, const Id& id) {
    const auto found = std::find_if(values.begin(),values.end(),[&](const T& value){return value.id == id;});
    if (found == values.end()) throw ModelError(ErrorCode::missing_reference,"Registered field owner is missing",{id});
    return *found;
}
template<class T> void reorder(std::vector<T>& values, const std::vector<Id>& order) {
    if (order.size() != values.size()) throw ModelError(ErrorCode::invalid_definition,"Order must contain every existing ID once");
    std::set<Id> seen;
    std::vector<T> next; next.reserve(values.size());
    for (const auto& id : order) {
        if (!seen.insert(id).second) throw ModelError(ErrorCode::duplicate_id,"Order repeats an ID",{id});
        next.push_back(find(values,id));
    }
    values = std::move(next);
}
template<class T> std::vector<Id> order_of(const std::vector<T>& values) {
    std::vector<Id> order; order.reserve(values.size());
    for (const auto& value : values) order.push_back(value.id);
    return order;
}
ParameterType type_of(const ParameterValue& value) {
    // Types and alternatives deliberately share this exhaustive order.
    static_assert(std::variant_size_v<ParameterValue> == static_cast<std::size_t>(ParameterType::mask_shape)+1);
    return static_cast<ParameterType>(value.index());
}
template<class T, std::size_t Index = 0> constexpr std::size_t command_index() {
    if constexpr (std::is_same_v<T,std::variant_alternative_t<Index,Command>>) return Index;
    else {
        static_assert(Index+1 < std::variant_size_v<Command>,"Action payload is not a typed command");
        return command_index<T,Index+1>();
    }
}
void validate_value(const ParameterDescriptor& descriptor, const ParameterValue& value) {
    require(type_of(value) == descriptor.type,RegistryErrorCode::type_mismatch,"Parameter value has the wrong type");
    std::visit([&](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T,double>) require(std::isfinite(item),RegistryErrorCode::out_of_range,"Parameter value must be finite");
        if constexpr (std::is_same_v<T,Vec2>) require(std::isfinite(item.x) && std::isfinite(item.y),RegistryErrorCode::out_of_range,"Point coordinates must be finite");
        if constexpr (std::is_same_v<T,double> || std::is_same_v<T,std::uint64_t>) {
            if (descriptor.range) require(static_cast<long double>(item) >= descriptor.range->minimum &&
                static_cast<long double>(item) <= descriptor.range->maximum,RegistryErrorCode::out_of_range,"Parameter value is outside its registered range");
        }
    },value);
}
}
struct ParameterRegistry::Edit {
    Definitions definitions;
    Stage stage;
    const InstancePool& instances;
    bool definitions_dirty = false, stage_dirty = false;
    std::map<Id,TransportIntent> transports;
    std::map<Id,bool> keep_alive;
    explicit Edit(const ProjectState& state) : definitions(state.instances.definitions().data()), stage(state.stage.data()), instances(state.instances) {}
    TransportIntent& transport(const Id& id) {
        return transports.try_emplace(id,instances.instance(id).transport).first->second;
    }
};
void ParameterRegistry::add(ParameterDescriptor descriptor, std::function<void(Edit&,const ParameterValue&)> setter) {
    require(parameters_.size() < limits_.parameters,RegistryErrorCode::capacity_exceeded,"Registry parameter capacity exceeded");
    require(descriptor.access == Access::read_only ? !setter : static_cast<bool>(setter),RegistryErrorCode::type_mismatch,"Registry access/setter mismatch");
    validate_value(descriptor,descriptor.value);
    if (descriptor.default_value) validate_value(descriptor,*descriptor.default_value);
    const auto address = descriptor.address;
    require(parameters_.emplace(address,Binding{std::move(descriptor),std::move(setter)}).second,
            RegistryErrorCode::type_mismatch,"Duplicate canonical registry address");
}
ParameterRegistry::ParameterRegistry(const ProjectController& controller, RegistryLimits limits)
    : limits_(limits), snapshot_(controller.snapshot()), revision_(controller.revision()) {
    const auto field = [&]<class T, class Setter>(const Id& owner, const char* parameter, const std::string& base,
        const T& value, std::optional<ParameterValue> initial, const char* unit, const char* description, Setter setter,
        Persistence persistence = Persistence::authored, std::optional<NumericRange> range = std::nullopt) {
        ParameterValue current{value};
        add({Id{parameter},owner,base+"/"+parameter,type_of(current),current,std::move(initial),range,unit,description,
             Access::read_write,persistence},[setter](Edit& edit,const ParameterValue& next){setter(edit,std::get<T>(next));});
    };
    const auto readonly = [&](const Id& owner, const char* parameter, const std::string& base, ParameterValue value, const char* description) {
        add({Id{parameter},owner,base+"/"+parameter,type_of(value),std::move(value),std::nullopt,std::nullopt,"",description,
             Access::read_only,Persistence::transient},{});
    };
    const auto uint_range = NumericRange{1,static_cast<double>(std::numeric_limits<std::uint32_t>::max())};
    for (const auto& [id,asset] : snapshot_->instances.definitions().data().assets) {
        const auto base = "/library/assets/"+id.str();
        field(id,"name",base,asset.name,ParameterValue{std::string{"Asset"}},"","Asset display name",
              [id](Edit& edit,const auto& value){edit.definitions.assets.at(id).name = value; edit.definitions_dirty = true;});
        if (const auto* external = std::get_if<ExternalSource>(&asset.source)) {
            readonly(id,"sourceKind",base,std::string{"external"},"Source variant; replace definitions to change kind");
            field(id,"adapter",base,external->adapter,std::nullopt,"","Configured adapter; availability is reported separately",
                [id](Edit& edit,const auto& value){std::get<ExternalSource>(edit.definitions.assets.at(id).source).adapter = value; edit.definitions_dirty = true;});
            field(id,"locator",base,external->locator,ParameterValue{std::string{}},"","Portable configured source locator",
                [id](Edit& edit,const auto& value){std::get<ExternalSource>(edit.definitions.assets.at(id).source).locator = value; edit.definitions_dirty = true;});
        } else {
            readonly(id,"sourceKind",base,std::string{"composition"},"Source variant; replace definitions to change kind");
            field(id,"composition",base,std::get<CompositionSource>(asset.source).composition,std::nullopt,"","Referenced composition definition",
                [id](Edit& edit,const auto& value){std::get<CompositionSource>(edit.definitions.assets.at(id).source).composition = value; edit.definitions_dirty = true;});
        }
    }
    const auto ports = [&](const Id& owner, const std::string& base, const std::vector<Port>& values, auto select) {
        field(owner,"order",base,order_of(values),std::nullopt,"","Stable port IDs in display order",
            [select](Edit& edit,const auto& order){reorder(select(edit),order); edit.definitions_dirty = true;});
        for (const auto& port : values) {
            const auto path = base+"/"+port.id.str();
            field(port.id,"name",path,port.name,ParameterValue{std::string{"Port"}},"","Port display name",
                [select,id = port.id](Edit& edit,const auto& value){find(select(edit),id).name = value; edit.definitions_dirty = true;});
            field(port.id,"kind",path,port.kind,ParameterValue{StreamKind::video},"","Typed stream kind",
                [select,id = port.id](Edit& edit,const auto& value){find(select(edit),id).kind = value; edit.definitions_dirty = true;});
        }
    };
    for (const auto& [id,asset] : snapshot_->instances.definitions().data().assets)
        if (const auto* external = std::get_if<ExternalSource>(&asset.source))
            ports(id,"/library/assets/"+id.str()+"/outputs",external->outputs,
                [id](Edit& edit)->auto& {return std::get<ExternalSource>(edit.definitions.assets.at(id).source).outputs;});
    for (const auto& [id,composition] : snapshot_->instances.definitions().data().compositions) {
        const auto base = "/show/compositions/"+id.str();
        field(id,"name",base,composition.name,ParameterValue{std::string{"Composition"}},"","Composition display name",
            [id](Edit& edit,const auto& value){edit.definitions.compositions.at(id).name = value; edit.definitions_dirty = true;});
        ports(id,base+"/inputs",composition.inputs,[id](Edit& edit)->auto& {return edit.definitions.compositions.at(id).inputs;});
        ports(id,base+"/outputs",composition.outputs,[id](Edit& edit)->auto& {return edit.definitions.compositions.at(id).outputs;});
        field(id,"order",base+"/layers",order_of(composition.layers),std::nullopt,"","Stable layer IDs in authored order",
            [id](Edit& edit,const auto& value){reorder(edit.definitions.compositions.at(id).layers,value); edit.definitions_dirty = true;});
        for (const auto& layer : composition.layers) {
            const auto path = base+"/layers/"+layer.id.str();
            const auto select = [id,child = layer.id](Edit& edit)->auto& {edit.definitions_dirty = true; return find(edit.definitions.compositions.at(id).layers,child);};
            field(layer.id,"name",path,layer.name,ParameterValue{std::string{"Layer"}},"","Layer display name",[select](Edit& edit,const auto& value){select(edit).name = value;});
            field(layer.id,"kind",path,layer.kind,ParameterValue{StreamKind::video},"","Layer stream kind",[select](Edit& edit,const auto& value){select(edit).kind = value;});
            field(layer.id,"source",path,layer.source,std::nullopt,"","Typed asset-port or composition-input binding",[select](Edit& edit,const auto& value){select(edit).source = value;});
            field(layer.id,"sharedGroup",path,layer.shared_group,ParameterValue{std::optional<Id>{}},"","Explicit parent-local sharing group",[select](Edit& edit,const auto& value){select(edit).shared_group = value;});
            const auto visual = layer.visual.value_or(LayerVisual{});
            const auto visual_select = [select](Edit& edit)->auto& {auto& value = select(edit).visual;if (!value) value.emplace();return *value;};
            field(layer.id,"opacity",path+"/visual",visual.opacity,ParameterValue{1.0},"ratio","Linear premultiplied layer opacity",[visual_select](Edit& edit,auto value){visual_select(edit).opacity = value;},Persistence::authored,NumericRange{0,1});
            field(layer.id,"blend",path+"/visual",visual.blend,ParameterValue{BlendMode::over},"","Linear layer blend rule",[visual_select](Edit& edit,auto value){visual_select(edit).blend = value;});
            field(layer.id,"fit",path+"/visual",visual.fit,ParameterValue{ContentFit::stretch},"","Fit the source frame to this composition canvas",[visual_select](Edit& edit,auto value){visual_select(edit).fit = value;});
        }
        for (const auto& [port,canvas] : composition.canvases) {
            const auto path = base+"/canvases/"+port.str();
            const auto select = [id,port](Edit& edit)->auto& {edit.definitions_dirty = true; return edit.definitions.compositions.at(id).canvases.at(port);};
            field(port,"width",path,static_cast<std::uint64_t>(canvas.width),ParameterValue{std::uint64_t{1920}},"pixels","Logical canvas width",[select](Edit& edit,auto value){select(edit).width = static_cast<std::uint32_t>(value);},Persistence::authored,uint_range);
            field(port,"height",path,static_cast<std::uint64_t>(canvas.height),ParameterValue{std::uint64_t{1080}},"pixels","Logical canvas height",[select](Edit& edit,auto value){select(edit).height = static_cast<std::uint32_t>(value);},Persistence::authored,uint_range);
            field(port,"pixelFormat",path,canvas.pixel_format,ParameterValue{Canvas{}.pixel_format},"","Configured logical pixel format",[select](Edit& edit,const auto& value){select(edit).pixel_format = value;});
            field(port,"colourSpace",path,canvas.colour_space,ParameterValue{Canvas{}.colour_space},"","Configured logical colour space",[select](Edit& edit,const auto& value){select(edit).colour_space = value;});
            field(port,"alpha",path,canvas.alpha,ParameterValue{Canvas{}.alpha},"","Configured alpha convention",[select](Edit& edit,const auto& value){select(edit).alpha = value;});
        }
    }
    for (const auto& [id,node] : snapshot_->instances.instances()) {
        const auto base = "/show/sources/"+id.str();
        readonly(id,"asset",base,node.asset,"Reusable asset definition");
        readonly(id,"ownerReferences",base,static_cast<std::uint64_t>(node.owner_references),"Derived ownership count; not adapter readiness");
        field(id,"keepAlive",base,node.keep_alive,ParameterValue{false},"","Retain source intent without consumers",
            [id](Edit& edit,bool value){edit.keep_alive.insert_or_assign(id,value);});
        const auto path = base+"/transport";
        field(id,"position",path,node.transport.position,ParameterValue{MediaTime{0,{1,1}}},"media-time","Requested position; not decoded playhead",
            [id](Edit& edit,const auto& value){edit.transport(id).position = value;},Persistence::resume_checkpoint);
        field(id,"rate",path,node.transport.rate,ParameterValue{Rational{1,1}},"ratio","Requested rational playback rate",
            [id](Edit& edit,const auto& value){edit.transport(id).rate = value;});
        field(id,"playing",path,node.transport.playing,ParameterValue{false},"","Requested playback state; not readiness",
            [id](Edit& edit,bool value){edit.transport(id).playing = value;},Persistence::resume_checkpoint);
        field(id,"loop",path,node.transport.loop,ParameterValue{false},"","Requested loop policy",
            [id](Edit& edit,bool value){edit.transport(id).loop = value;});
        for (const auto& [layer,source] : node.layers) readonly(layer,"source",base+"/layers/"+layer.str(),source,"Definition-owned child binding");
        for (const auto& [input,source] : node.inputs) readonly(input,"source",base+"/inputs/"+input.str(),source,"Live content binding; use bind/unbind action");
    }
    for (const auto& [id,placement] : snapshot_->instances.placements())
        readonly(id,"source","/show/placements/"+id.str(),placement.source,"Placement source; use placement actions to change ownership");
    // Stage fields use the same registration path, including stable point IDs.
    const auto named = [&](const auto& values,const std::string& base,auto select) {
        for (const auto& [id,value] : values) field(id,"name",base+"/"+id.str(),value.name,ParameterValue{std::string{"Object"}},"","Display name",
            [select,id](Edit& edit,const auto& next){select(edit).at(id).name = next; edit.stage_dirty = true;});
    };
    const auto& stage = snapshot_->stage.data();
    named(stage.surfaces,"/stage/surfaces",[](Edit& edit)->auto& {return edit.stage.surfaces;});
    named(stage.groups,"/stage/groups",[](Edit& edit)->auto& {return edit.stage.groups;});
    named(stage.layouts,"/stage/layouts",[](Edit& edit)->auto& {return edit.stage.layouts;});
    named(stage.outputs,"/outputs",[](Edit& edit)->auto& {return edit.stage.outputs;});
    named(stage.patches,"/stage/patches",[](Edit& edit)->auto& {return edit.stage.patches;});
    for (const auto& [id,surface] : stage.surfaces) {
        const auto base = "/stage/surfaces/"+id.str();
        readonly(id,"contentPlacement",base,surface.content_placement,"Surface ownership lease; use assign-surface-content action");
        const auto content = surface.content.value_or(SurfaceContent{});
        const auto content_select = [id](Edit& edit)->auto& {edit.stage_dirty = true;auto& value = edit.stage.surfaces.at(id).content;if (!value) value.emplace();return *value;};
        field(id,"cropMinimum",base+"/content",content.crop_minimum,ParameterValue{Vec2{0,0}},"normalized-uv","Composition crop minimum",[content_select](Edit& edit,auto value){content_select(edit).crop_minimum = value;});
        field(id,"cropMaximum",base+"/content",content.crop_maximum,ParameterValue{Vec2{1,1}},"normalized-uv","Composition crop maximum",[content_select](Edit& edit,auto value){content_select(edit).crop_maximum = value;});
        field(id,"fit",base+"/content",content.fit,ParameterValue{ContentFit::stretch},"","Fit cropped content to the logical surface aspect",[content_select](Edit& edit,auto value){content_select(edit).fit = value;});
        field(id,"opacity",base+"/content",content.opacity,ParameterValue{1.0},"ratio","Surface content opacity",[content_select](Edit& edit,auto value){content_select(edit).opacity = value;},Persistence::authored,NumericRange{0,1});
        const auto geometry = [id](Edit& edit)->auto& {edit.stage_dirty = true; return edit.stage.surfaces.at(id).geometry;};
        field(id,"triangles",base+"/geometry",surface.geometry.triangles,std::nullopt,"","Stable point-ID topology",[geometry](Edit& edit,const auto& value){geometry(edit).triangles = value;});
        for (const auto& point : surface.geometry.vertices) {
            const auto path = base+"/geometry/points/"+point.id.str();
            const auto select = [geometry,point = point.id](Edit& edit)->auto& {return find(geometry(edit).vertices,point);};
            field(point.id,"position",path,point.local,std::nullopt,"surface-local","Logical surface position",[select](Edit& edit,auto value){select(edit).local = value;});
            field(point.id,"uv",path,point.uv,std::nullopt,"normalized-uv","Surface UV position",[select](Edit& edit,auto value){select(edit).uv = value;});
        }
    }
    for (const auto& [id,group] : stage.groups) {
        const auto base = "/stage/groups/"+id.str();
        const auto select = [id](Edit& edit)->auto& {edit.stage_dirty = true; return edit.stage.groups.at(id);};
        field(id,"surfaces",base,group.surfaces,std::nullopt,"","Operational surface members",[select](Edit& edit,const auto& value){select(edit).surfaces = value;});
        field(id,"seams",base,group.seams,ParameterValue{std::vector<Seam>{}},"","Optional boundary-edge seam metadata",[select](Edit& edit,const auto& value){select(edit).seams = value;});
    }
    for (const auto& [id,layout] : stage.layouts) {
        const auto base = "/stage/layouts/"+id.str();
        const auto select = [id](Edit& edit)->auto& {edit.stage_dirty = true; return edit.stage.layouts.at(id);};
        field(id,"group",base,layout.group,std::nullopt,"","Layout surface group",[select](Edit& edit,const auto& value){select(edit).group = value;});
        field(id,"mode",base,layout.mode,ParameterValue{LayoutMode::independent},"","Independent or continuous layout",[select](Edit& edit,auto value){select(edit).mode = value;});
        for (const auto& chart : layout.charts) {
            const auto path = base+"/charts/"+chart.surface.str();
            const auto chart_select = [select,surface = chart.surface](Edit& edit)->auto& {
                auto& charts = select(edit).charts;
                const auto found = std::find_if(charts.begin(),charts.end(),[&](const auto& value){return value.surface == surface;});
                if (found == charts.end()) throw ModelError(ErrorCode::missing_reference,"Registered chart is missing",{surface});
                return *found;
            };
            field(chart.surface,"triangles",path,chart.triangles,std::nullopt,"","Chart point-ID topology",[chart_select](Edit& edit,const auto& value){chart_select(edit).triangles = value;});
            for (const auto& point : chart.vertices) {
                const auto point_path = path+"/points/"+point.id.str();
                const auto point_select = [chart_select,id = point.id](Edit& edit)->auto& {return find(chart_select(edit).vertices,id);};
                field(point.id,"surfaceUv",point_path,point.surface_uv,std::nullopt,"normalized-uv","Surface chart UV",[point_select](Edit& edit,auto value){point_select(edit).surface_uv = value;});
                field(point.id,"compositionUv",point_path,point.composition_uv,std::nullopt,"normalized-uv","Composition chart UV",[point_select](Edit& edit,auto value){point_select(edit).composition_uv = value;});
            }
        }
    }
    for (const auto& [id,output] : stage.outputs) {
        const auto base = "/outputs/"+id.str();
        const auto select = [id](Edit& edit)->auto& {edit.stage_dirty = true; return edit.stage.outputs.at(id);};
        field(id,"adapter",base,output.adapter,std::nullopt,"","Configured output adapter; not a runtime capability claim",[select](Edit& edit,const auto& value){select(edit).adapter = value;});
        field(id,"endpoint",base,output.endpoint,ParameterValue{std::string{}},"","Persistent desired output binding",[select](Edit& edit,const auto& value){select(edit).endpoint = value;});
        field(id,"enabled",base,output.enabled,ParameterValue{true},"","Desired native output enable",[select](Edit& edit,auto value){select(edit).enabled = value;});
        field(id,"blackout",base,output.blackout,ParameterValue{false},"","Black output without changing content or calibration",[select](Edit& edit,auto value){select(edit).blackout = value;});
        field(id,"freeze",base,output.freeze,ParameterValue{false},"","Hold this output's last complete frame",[select](Edit& edit,auto value){select(edit).freeze = value;});
        field(id,"width",base+"/desired",static_cast<std::uint64_t>(output.desired.width),ParameterValue{std::uint64_t{1920}},"pixels","Desired output width",[select](Edit& edit,auto value){select(edit).desired.width = static_cast<std::uint32_t>(value);},Persistence::authored,uint_range);
        field(id,"height",base+"/desired",static_cast<std::uint64_t>(output.desired.height),ParameterValue{std::uint64_t{1080}},"pixels","Desired output height",[select](Edit& edit,auto value){select(edit).desired.height = static_cast<std::uint32_t>(value);},Persistence::authored,uint_range);
        field(id,"refreshHz",base+"/desired",output.desired.refresh_hz,ParameterValue{Rational{60,1}},"Hz","Desired rational refresh; not measured scanout",[select](Edit& edit,auto value){select(edit).desired.refresh_hz = value;});
    }
    for (const auto& [id,patch] : stage.patches) {
        const auto base = "/stage/patches/"+id.str();
        const auto select = [id](Edit& edit)->auto& {edit.stage_dirty = true; return edit.stage.patches.at(id);};
        field(id,"surface",base,patch.surface,std::nullopt,"","Projected surface",[select](Edit& edit,const auto& value){select(edit).surface = value;});
        field(id,"output",base,patch.output,std::nullopt,"","Physical output target",[select](Edit& edit,const auto& value){select(edit).output = value;});
        const auto visual = patch.visual.value_or(PatchVisual{});
        const auto visual_select = [select](Edit& edit)->auto& {auto& value = select(edit).visual;if (!value) value.emplace();return *value;};
        const auto visual_path = base+"/visual";
        field(id,"opacity",visual_path,visual.opacity,ParameterValue{1.0},"ratio","Patch opacity",[visual_select](Edit& edit,auto value){visual_select(edit).opacity = value;},Persistence::authored,NumericRange{0,1});
        field(id,"blend",visual_path,visual.blend,ParameterValue{BlendMode::over},"","Linear patch blend rule",[visual_select](Edit& edit,auto value){visual_select(edit).blend = value;});
        const auto mask_path = visual_path+"/mask";
        field(id,"shape",mask_path,visual.mask.shape,ParameterValue{MaskShape::none},"","Surface-UV mask shape",[visual_select](Edit& edit,auto value){visual_select(edit).mask.shape = value;});
        field(id,"minimum",mask_path,visual.mask.minimum,ParameterValue{Vec2{0,0}},"normalized-uv","Mask minimum",[visual_select](Edit& edit,auto value){visual_select(edit).mask.minimum = value;});
        field(id,"maximum",mask_path,visual.mask.maximum,ParameterValue{Vec2{1,1}},"normalized-uv","Mask maximum",[visual_select](Edit& edit,auto value){visual_select(edit).mask.maximum = value;});
        field(id,"feather",mask_path,visual.mask.feather,ParameterValue{0.0},"ratio","Inward mask feather",[visual_select](Edit& edit,auto value){visual_select(edit).mask.feather = value;},Persistence::authored,NumericRange{0,.5});
        field(id,"inverted",mask_path,visual.mask.inverted,ParameterValue{false},"","Invert mask coverage",[visual_select](Edit& edit,auto value){visual_select(edit).mask.inverted = value;});
        const auto path = base+"/calibration";
        field(id,"mode",path,patch.calibration.mode,ParameterValue{WarpMode::homography},"","Homography or mesh calibration",[select](Edit& edit,auto value){select(edit).calibration.mode = value;});
        field(id,"referenceWidth",path,static_cast<std::uint64_t>(patch.calibration.reference_width),ParameterValue{std::uint64_t{1920}},"pixels","Calibration reference width; never follows desired output mode automatically",[select](Edit& edit,auto value){select(edit).calibration.reference_width = static_cast<std::uint32_t>(value);},Persistence::authored,uint_range);
        field(id,"referenceHeight",path,static_cast<std::uint64_t>(patch.calibration.reference_height),ParameterValue{std::uint64_t{1080}},"pixels","Calibration reference height",[select](Edit& edit,auto value){select(edit).calibration.reference_height = static_cast<std::uint32_t>(value);},Persistence::authored,uint_range);
        field(id,"boundary",path,patch.calibration.boundary,std::nullopt,"","Stable calibration boundary IDs",[select](Edit& edit,const auto& value){select(edit).calibration.boundary = value;});
        field(id,"triangles",path,patch.calibration.triangles,std::nullopt,"","Calibration control topology",[select](Edit& edit,const auto& value){select(edit).calibration.triangles = value;});
        for (const auto& point : patch.calibration.vertices) {
            const auto point_path = path+"/points/"+point.id.str();
            const auto point_select = [select,id = point.id](Edit& edit)->auto& {return find(select(edit).calibration.vertices,id);};
            field(point.id,"surfaceUv",point_path,point.surface_uv,std::nullopt,"normalized-uv","Calibration source UV",[point_select](Edit& edit,auto value){point_select(edit).surface_uv = value;});
            field(point.id,"projectorPixels",point_path,point.projector_pixels,std::nullopt,"pixels","Physical calibration control",[point_select](Edit& edit,auto value){point_select(edit).projector_pixels = value;});
        }
    }
    const auto action = [&]<class T>(const char* id,const char* path,const char* type,const char* description) {
        command_actions_.emplace(command_index<T>(),actions_.size());
        actions_.push_back({Id{id},std::string{"/commands/"}+path,type,description});
    };
    action.operator()<ReplaceDefinitions>("definitions-replace","definitions/replace","ReplaceDefinitions","Validate and reconcile reusable definitions");
    action.operator()<ReplaceStage>("stage-replace","stage/replace","ReplaceStage","Validate a structural stage edit");
    action.operator()<PlaceSource>("sources-place","sources/place","PlaceSource","Create an independent placement");
    action.operator()<ShareSource>("sources-share","sources/share","ShareSource","Create an explicitly shared placement");
    action.operator()<RemovePlacement>("placements-remove","placements/remove","RemovePlacement","Release a placement owner");
    action.operator()<BindInput>("inputs-bind","inputs/bind","BindInput","Bind independent or explicitly shared input content");
    action.operator()<UnbindInput>("inputs-unbind","inputs/unbind","UnbindInput","Release a live input owner");
    action.operator()<SetTransport>("transport-set","transport/set","SetTransport","Set complete rational transport intent");
    action.operator()<SetKeepAlive>("sources-keepAlive","sources/keepAlive","SetKeepAlive","Change source retention policy");
    action.operator()<AssignSurfaceContent>("surfaces-assign","surfaces/assign","AssignSurfaceContent","Assign composition content with its own lease");
    require(command_actions_.size() == std::variant_size_v<Command>,RegistryErrorCode::missing_address,"Typed command is missing from registry inventory");
    actions_.push_back({Id{"history-undo"},"/commands/history/undo","expectedRevision","Undo an authored transaction"});
    actions_.push_back({Id{"history-redo"},"/commands/history/redo","expectedRevision","Redo an authored transaction"});
}
const ParameterDescriptor& ParameterRegistry::parameter(const std::string& address) const {
    const auto found = parameters_.find(address);
    require(found != parameters_.end(),RegistryErrorCode::missing_address,"Parameter address is missing");
    return found->second.descriptor;
}
std::vector<ParameterDescriptor> ParameterRegistry::inventory() const {
    std::vector<ParameterDescriptor> result; result.reserve(parameters_.size());
    for (const auto& [path,binding] : parameters_) {static_cast<void>(path); result.push_back(binding.descriptor);}
    return result;
}
void ParameterRegistry::check_revision(const ProjectController& controller, std::uint64_t revision) const {
    require(revision == revision_ && revision == controller.revision() && controller.snapshot() == snapshot_,
            RegistryErrorCode::stale_snapshot,"Registry inventory is from another authored revision");
}
AuthoredChange ParameterRegistry::write(ProjectController& controller, std::uint64_t revision, const std::vector<ParameterWrite>& writes) const {
    check_revision(controller,revision);
    require(writes.size() <= limits_.writes,RegistryErrorCode::capacity_exceeded,"Parameter batch capacity exceeded");
    Edit edit{*snapshot_};
    for (const auto& write : writes) {
        const auto& descriptor = parameter(write.address);
        require(descriptor.access == Access::read_write,RegistryErrorCode::read_only,"Parameter is read-only");
        validate_value(descriptor,write.value);
        parameters_.at(write.address).set(edit,write.value);
    }
    std::vector<Command> commands;
    if (edit.definitions_dirty) commands.emplace_back(ReplaceDefinitions{std::move(edit.definitions)});
    if (edit.stage_dirty) commands.emplace_back(ReplaceStage{std::move(edit.stage)});
    for (const auto& [id,value] : edit.transports) commands.emplace_back(SetTransport{id,value});
    for (const auto& [id,value] : edit.keep_alive) commands.emplace_back(SetKeepAlive{id,value});
    return controller.transact(revision,commands);
}
AuthoredChange ParameterRegistry::execute(ProjectController& controller, std::uint64_t revision, const std::vector<Command>& commands) const {
    check_revision(controller,revision);
    for (const auto& command : commands) require(command_actions_.contains(command.index()),RegistryErrorCode::missing_address,"Command has no registered action");
    return controller.transact(revision,commands);
}
AuthoredChange ParameterRegistry::undo(ProjectController& controller, std::uint64_t revision) const {check_revision(controller,revision); return controller.undo(revision);}
AuthoredChange ParameterRegistry::redo(ProjectController& controller, std::uint64_t revision) const {check_revision(controller,revision); return controller.redo(revision);}
} // namespace mapgyver::control
