// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/control/wire.hpp>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <type_traits>
#include <utility>

namespace mapgyver::control::wire {
namespace {
void require(bool valid, const char* message) {if (!valid) throw Error(ErrorCode::malformed,message);}
void keys(const Json& value, const auto& names) {
    require(value.is_object() && value.size() == names.size(),"Object has missing or unknown fields");
    for (const auto* name : names) require(value.contains(name),"Required object field is missing");
}
struct SchemaContext {
    Json definitions = Json::object();
    template<class Body> Json define(const std::string& name, Body body) {
        if (!definitions.contains(name)) {definitions[name] = nullptr; definitions[name] = body();}
        return {{"$ref","#/$defs/"+name}};
    }
};
template<class T, class Enable = void> struct Codec;
template<class T> auto fields();
template<class T, auto... Members> struct Object {
    const char* name;
    std::array<const char*,sizeof...(Members)> names;
    std::array<bool,sizeof...(Members)> default_if_missing{};
    template<auto Member> using MemberType = std::remove_cvref_t<decltype(std::declval<T>().*Member)>;
    Json encode(const T& value) const {
        Json result = Json::object(); std::size_t index = 0;
        ((result[names[index++]] = Codec<MemberType<Members>>::encode(value.*Members)),...);
        return result;
    }
    template<auto Member> MemberType<Member> read(const Json& value,std::size_t index) const {
        const auto found = value.find(names[index]);if (found != value.end()) return Codec<MemberType<Member>>::decode(*found);
        require(default_if_missing[index],"Required object field is missing");
        if constexpr (std::is_default_constructible_v<MemberType<Member>>) return {};
        else throw Error(ErrorCode::malformed,"Required object field is missing");
    }
    T decode(const Json& value) const {
        require(value.is_object(),"Expected an object");
        for (auto it = value.begin(); it != value.end(); ++it) {
            bool known = false;for (const auto* field : names) if (it.key() == field) {known = true;break;}
            require(known,"Object has unknown fields");
        }
        std::size_t index = 0;return T{read<Members>(value,index++)...};
    }
    Json shape(SchemaContext& context) const {
        Json properties = Json::object(); std::size_t index = 0;
        ((properties[names[index++]] = Codec<MemberType<Members>>::shape(context)),...);
        Json required = Json::array();for (std::size_t i = 0; i < names.size(); ++i) if (!default_if_missing[i]) required.push_back(names[i]);
        return {{"type","object"},{"additionalProperties",false},{"required",std::move(required)},{"properties",std::move(properties)}};
    }
};
template<class T, auto... Members> Object<T,Members...> object(const char* name, std::array<const char*,sizeof...(Members)> names,
    std::array<bool,sizeof...(Members)> defaults = {}) {return {name,names,defaults};}
template<class T, class Enable> struct Codec {
    static const char* name() {return fields<T>().name;}
    static Json encode(const T& value) {return fields<T>().encode(value);}
    static T decode(const Json& value) {return fields<T>().decode(value);}
    static Json shape(SchemaContext& context) {return context.define(name(),[&]{return fields<T>().shape(context);});}
};
template<class T> struct Codec<T,std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T,bool>>> {
    static const char* name() {return std::is_signed_v<T> ? "signedInteger" : "unsignedInteger";}
    static constexpr bool number = std::numeric_limits<T>::digits <= 53;
    static Json encode(T value) {if constexpr (number) return value; else return std::to_string(value);}
    static T decode(const Json& value) {
        if constexpr (number) {
            require(value.is_number_integer(),"Expected an exact JSON integer");
            if (value.is_number_unsigned()) {
                const auto parsed = value.get<std::uint64_t>();
                require(parsed <= static_cast<std::uint64_t>(std::numeric_limits<T>::max()),"Integer exceeds its type range");
                return static_cast<T>(parsed);
            }
            const auto parsed = value.get<std::int64_t>();
            require(parsed >= static_cast<std::int64_t>(std::numeric_limits<T>::min()) &&
                    parsed <= static_cast<std::int64_t>(std::numeric_limits<T>::max()),"Integer exceeds its type range");
            return static_cast<T>(parsed);
        } else {
            require(value.is_string(),"64-bit integers use canonical decimal strings");
            const auto& text = value.get_ref<const std::string&>();
            T result{}; const auto parsed = std::from_chars(text.data(),text.data()+text.size(),result);
            require(parsed.ec == std::errc{} && parsed.ptr == text.data()+text.size() && std::to_string(result) == text,
                    "Integer string is noncanonical or outside its type range");
            return result;
        }
    }
    static Json shape(SchemaContext&) {
        if constexpr (number) return {{"type","integer"},{"minimum",std::numeric_limits<T>::min()},{"maximum",std::numeric_limits<T>::max()}};
        else return {{"type","string"},{"pattern",std::is_signed_v<T> ? "^(0|-?[1-9][0-9]*)$" : "^(0|[1-9][0-9]*)$"},
            {"maxLength",20},{"x-mapgyver-minimum",std::to_string(std::numeric_limits<T>::min())},
            {"x-mapgyver-maximum",std::to_string(std::numeric_limits<T>::max())}};
    }
};
template<> struct Codec<bool> {
    static const char* name() {return "boolean";}
    static Json encode(bool value) {return value;}
    static bool decode(const Json& value) {require(value.is_boolean(),"Expected a boolean"); return value.get<bool>();}
    static Json shape(SchemaContext&) {return {{"type","boolean"}};}
};
template<> struct Codec<double> {
    static const char* name() {return "real";}
    static Json encode(double value) {require(std::isfinite(value),"Cannot encode a nonfinite number"); return value;}
    static double decode(const Json& value) {require(value.is_number(),"Expected a finite number"); const auto result = value.get<double>(); require(std::isfinite(result),"Expected a finite number"); return result;}
    static Json shape(SchemaContext&) {return {{"type","number"}};}
};
template<> struct Codec<std::string> {
    static const char* name() {return "text";}
    static Json encode(const std::string& value) {return value;}
    static std::string decode(const Json& value) {require(value.is_string(),"Expected a string"); return value.get<std::string>();}
    static Json shape(SchemaContext&) {return {{"type","string"}};}
};
template<> struct Codec<Id> {
    static const char* name() {return "reference";}
    static Json encode(const Id& value) {return value.str();}
    static Id decode(const Json& value) {return Id{Codec<std::string>::decode(value)};}
    static Json shape(SchemaContext&) {return {{"type","string"},{"pattern","^[a-zA-Z0-9_-]{1,64}$"}};}
};
template<class T> struct Codec<std::optional<T>> {
    static const char* name() {return std::is_same_v<T,Id> ? "optionalReference" : "optional";}
    static Json encode(const std::optional<T>& value) {return value ? Codec<T>::encode(*value) : Json(nullptr);}
    static std::optional<T> decode(const Json& value) {if (value.is_null()) return std::nullopt; return Codec<T>::decode(value);}
    static Json shape(SchemaContext& context) {return {{"anyOf",Json::array({Codec<T>::shape(context),{{"type","null"}}})}};}
};
template<class T> struct Codec<std::vector<T>> {
    static const char* name() {
        if constexpr (std::is_same_v<T,Id>) return "idList";
        else if constexpr (std::is_same_v<T,Triangle>) return "triangles";
        else if constexpr (std::is_same_v<T,Seam>) return "seams";
        else return "list";
    }
    static Json encode(const std::vector<T>& values) {Json result = Json::array(); for (const auto& value : values) result.push_back(Codec<T>::encode(value)); return result;}
    static std::vector<T> decode(const Json& values) {
        require(values.is_array(),"Expected an array"); std::vector<T> result; result.reserve(values.size());
        for (const auto& value : values) result.push_back(Codec<T>::decode(value));
        return result;
    }
    static Json shape(SchemaContext& context) {return {{"type","array"},{"items",Codec<T>::shape(context)}};}
};
template<class T,std::size_t Size> struct Codec<std::array<T,Size>> {
    static Json encode(const std::array<T,Size>& values) {Json result = Json::array(); for (const auto& value : values) result.push_back(Codec<T>::encode(value)); return result;}
    template<std::size_t... Index> static std::array<T,Size> read(const Json& value,std::index_sequence<Index...>) {return {Codec<T>::decode(value.at(Index))...};}
    static std::array<T,Size> decode(const Json& value) {require(value.is_array() && value.size() == Size,"Fixed array has the wrong length"); return read(value,std::make_index_sequence<Size>{});}
    static Json shape(SchemaContext& context) {return {{"type","array"},{"minItems",Size},{"maxItems",Size},{"items",Codec<T>::shape(context)}};}
};
template<class T> struct Codec<std::map<Id,T>> {
    static Json encode(const std::map<Id,T>& values) {Json result = Json::object(); for (const auto& [id,value] : values) result[id.str()] = Codec<T>::encode(value); return result;}
    static std::map<Id,T> decode(const Json& values) {
        require(values.is_object(),"Expected a stable-ID map"); std::map<Id,T> result;
        for (const auto& [key,value] : values.items()) result.emplace(Id{key},Codec<T>::decode(value));
        return result;
    }
    static Json shape(SchemaContext& context) {return {{"type","object"},{"propertyNames",Codec<Id>::shape(context)},{"additionalProperties",Codec<T>::shape(context)}};}
};
template<class T> auto enum_values();
template<class T> struct Codec<T,std::enable_if_t<std::is_enum_v<T>>> {
    static const char* name() {
        if constexpr (std::is_same_v<T,StreamKind>) return "streamKind";
        else if constexpr (std::is_same_v<T,LayoutMode>) return "layoutMode";
        else if constexpr (std::is_same_v<T,WarpMode>) return "warpMode";
        else if constexpr (std::is_same_v<T,ContentFit>) return "contentFit";
        else if constexpr (std::is_same_v<T,BlendMode>) return "blendMode";
        else if constexpr (std::is_same_v<T,MaskShape>) return "maskShape";
        else return "enum";
    }
    static Json encode(T value) {for (const auto& [item,text] : enum_values<T>()) if (item == value) return text; throw Error(ErrorCode::malformed,"Unknown enum value");}
    static T decode(const Json& value) {const auto text = Codec<std::string>::decode(value); for (const auto& [item,name] : enum_values<T>()) if (text == name) return item; throw Error(ErrorCode::malformed,"Unknown enum value");}
    static Json shape(SchemaContext&) {Json values = Json::array(); for (const auto& [item,text] : enum_values<T>()) {static_cast<void>(item); values.push_back(text);} return {{"type","string"},{"enum",values}};}
};
template<> auto enum_values<StreamKind>() {return std::array{std::pair{StreamKind::video,"video"},std::pair{StreamKind::audio,"audio"},std::pair{StreamKind::number,"number"},std::pair{StreamKind::boolean,"boolean"},std::pair{StreamKind::text,"text"},std::pair{StreamKind::trigger,"trigger"}};}
template<> auto enum_values<LayoutMode>() {return std::array{std::pair{LayoutMode::independent,"independent"},std::pair{LayoutMode::continuous,"continuous"}};}
template<> auto enum_values<WarpMode>() {return std::array{std::pair{WarpMode::homography,"homography"},std::pair{WarpMode::mesh,"mesh"}};}
template<> auto enum_values<ContentFit>() {return std::array{std::pair{ContentFit::stretch,"stretch"},std::pair{ContentFit::fit,"fit"},std::pair{ContentFit::fill,"fill"}};}
template<> auto enum_values<BlendMode>() {return std::array{std::pair{BlendMode::over,"over"},std::pair{BlendMode::add,"add"},std::pair{BlendMode::screen,"screen"}};}
template<> auto enum_values<MaskShape>() {return std::array{std::pair{MaskShape::none,"none"},std::pair{MaskShape::rectangle,"rectangle"},std::pair{MaskShape::ellipse,"ellipse"}};}
template<> auto enum_values<Access>() {return std::array{std::pair{Access::read_only,"readOnly"},std::pair{Access::read_write,"readWrite"},std::pair{Access::trigger,"trigger"}};}
template<> auto enum_values<Persistence>() {return std::array{std::pair{Persistence::authored,"authored"},std::pair{Persistence::resume_checkpoint,"resumeCheckpoint"},std::pair{Persistence::transient,"transient"}};}
template<class... T> struct Codec<std::variant<T...>> {
    static const char* name() {
        if constexpr (std::is_same_v<std::variant<T...>,LayerSource>) return "layerSource";
        else if constexpr (std::is_same_v<std::variant<T...>,ParameterValue>) return "parameterValue";
        else if constexpr (std::is_same_v<std::variant<T...>,Command>) return "command";
        else return "sourceBinding";
    }
    static Json encode(const std::variant<T...>& value) {return std::visit([](const auto& item)->Json {using U = std::decay_t<decltype(item)>; return {{"type",Codec<U>::name()},{"value",Codec<U>::encode(item)}};},value);}
    static std::variant<T...> decode(const Json& value) {
        keys(value,std::array{"type","value"}); const auto tag = Codec<std::string>::decode(value.at("type"));
        std::optional<std::variant<T...>> result;
        ([&]{if (tag == Codec<T>::name()) result.emplace(std::in_place_type<T>,Codec<T>::decode(value.at("value")));}(),...);
        require(result.has_value(),"Unknown tagged value type"); return std::move(*result);
    }
    static Json shape(SchemaContext& context) {
        Json cases = Json::array();
        (cases.push_back({{"type","object"},{"additionalProperties",false},{"required",{"type","value"}},
            {"properties",{{"type",{{"const",Codec<T>::name()}}},{"value",Codec<T>::shape(context)}}}}),...);
        return {{"oneOf",cases}};
    }
};
template<> auto enum_values<ParameterType>() {
    return std::array{std::pair{ParameterType::boolean,Codec<bool>::name()},std::pair{ParameterType::unsigned_integer,Codec<std::uint64_t>::name()},
        std::pair{ParameterType::real,Codec<double>::name()},std::pair{ParameterType::text,Codec<std::string>::name()},std::pair{ParameterType::reference,Codec<Id>::name()},
        std::pair{ParameterType::optional_reference,Codec<std::optional<Id>>::name()},std::pair{ParameterType::rational,"Rational"},std::pair{ParameterType::media_time,"MediaTime"},
        std::pair{ParameterType::point2,"Vec2"},std::pair{ParameterType::stream_kind,Codec<StreamKind>::name()},std::pair{ParameterType::layout_mode,Codec<LayoutMode>::name()},
        std::pair{ParameterType::warp_mode,Codec<WarpMode>::name()},std::pair{ParameterType::layer_source,Codec<LayerSource>::name()},std::pair{ParameterType::instance_port,"InstancePort"},
        std::pair{ParameterType::id_list,Codec<std::vector<Id>>::name()},std::pair{ParameterType::triangles,Codec<std::vector<Triangle>>::name()},std::pair{ParameterType::seams,Codec<std::vector<Seam>>::name()},
        std::pair{ParameterType::content_fit,Codec<ContentFit>::name()},std::pair{ParameterType::blend_mode,Codec<BlendMode>::name()},std::pair{ParameterType::mask_shape,Codec<MaskShape>::name()}};
}
// Every field list supplies encoding, strict decoding and schema generation.
template<> auto fields<Port>() {return object<Port,&Port::id,&Port::name,&Port::kind>("Port",{"id","name","kind"});}
template<> auto fields<ExternalSource>() {return object<ExternalSource,&ExternalSource::adapter,&ExternalSource::locator,&ExternalSource::outputs>("ExternalSource",{"adapter","locator","outputs"});}
template<> auto fields<CompositionSource>() {return object<CompositionSource,&CompositionSource::composition>("CompositionSource",{"composition"});}
template<> auto fields<Asset>() {return object<Asset,&Asset::id,&Asset::name,&Asset::source>("Asset",{"id","name","source"});}
template<> auto fields<AssetPort>() {return object<AssetPort,&AssetPort::asset,&AssetPort::port>("AssetPort",{"asset","port"});}
template<> auto fields<InputPort>() {return object<InputPort,&InputPort::port>("InputPort",{"port"});}
template<> auto fields<LayerVisual>() {return object<LayerVisual,&LayerVisual::opacity,&LayerVisual::blend,&LayerVisual::fit>("LayerVisual",{"opacity","blend","fit"},{false,false,true});}
template<> auto fields<SurfaceContent>() {return object<SurfaceContent,&SurfaceContent::crop_minimum,&SurfaceContent::crop_maximum,&SurfaceContent::fit,&SurfaceContent::opacity>("SurfaceContent",{"cropMinimum","cropMaximum","fit","opacity"});}
template<> auto fields<Mask>() {return object<Mask,&Mask::shape,&Mask::minimum,&Mask::maximum,&Mask::feather,&Mask::inverted>("Mask",{"shape","minimum","maximum","feather","inverted"});}
template<> auto fields<PatchVisual>() {return object<PatchVisual,&PatchVisual::opacity,&PatchVisual::blend,&PatchVisual::mask>("PatchVisual",{"opacity","blend","mask"});}
template<> auto fields<Layer>() {return object<Layer,&Layer::id,&Layer::name,&Layer::kind,&Layer::source,&Layer::shared_group,&Layer::visual>("Layer",{"id","name","kind","source","sharedGroup","visual"},{false,false,false,false,false,true});}
template<> auto fields<Canvas>() {return object<Canvas,&Canvas::width,&Canvas::height,&Canvas::pixel_format,&Canvas::colour_space,&Canvas::alpha>("Canvas",{"width","height","pixelFormat","colourSpace","alpha"});}
template<> auto fields<Composition>() {return object<Composition,&Composition::id,&Composition::name,&Composition::inputs,&Composition::outputs,&Composition::layers,&Composition::canvases>("Composition",{"id","name","inputs","outputs","layers","canvases"});}
template<> auto fields<Definitions>() {return object<Definitions,&Definitions::assets,&Definitions::compositions>("Definitions",{"assets","compositions"});}
template<> auto fields<Rational>() {return object<Rational,&Rational::numerator,&Rational::denominator>("Rational",{"numerator","denominator"});}
template<> auto fields<MediaTime>() {return object<MediaTime,&MediaTime::ticks,&MediaTime::time_base>("MediaTime",{"ticks","timeBase"});}
template<> auto fields<TransportIntent>() {return object<TransportIntent,&TransportIntent::position,&TransportIntent::rate,&TransportIntent::playing,&TransportIntent::loop>("TransportIntent",{"position","rate","playing","loop"});}
template<> auto fields<InstancePort>() {return object<InstancePort,&InstancePort::instance,&InstancePort::port>("InstancePort",{"instance","port"});}
template<> auto fields<SourceInstance>() {return object<SourceInstance,&SourceInstance::id,&SourceInstance::asset,&SourceInstance::transport,&SourceInstance::keep_alive,&SourceInstance::owner_references,&SourceInstance::layers,&SourceInstance::inputs>("SourceInstance",{"id","asset","transport","keepAlive","ownerReferences","layers","inputs"});}
template<> auto fields<Placement>() {return object<Placement,&Placement::id,&Placement::source>("Placement",{"id","source"});}
template<> auto fields<Vec2>() {return object<Vec2,&Vec2::x,&Vec2::y>("Vec2",{"x","y"});}
template<> auto fields<Triangle>() {return object<Triangle,&Triangle::vertices>("Triangle",{"vertices"});}
template<> auto fields<SurfaceVertex>() {return object<SurfaceVertex,&SurfaceVertex::id,&SurfaceVertex::local,&SurfaceVertex::uv>("SurfaceVertex",{"id","local","uv"});}
template<> auto fields<SurfaceGeometry>() {return object<SurfaceGeometry,&SurfaceGeometry::vertices,&SurfaceGeometry::triangles>("SurfaceGeometry",{"vertices","triangles"});}
template<> auto fields<Surface>() {return object<Surface,&Surface::id,&Surface::name,&Surface::content_placement,&Surface::geometry,&Surface::content>("Surface",{"id","name","contentPlacement","geometry","content"},{false,false,false,false,true});}
template<> auto fields<SurfaceEdge>() {return object<SurfaceEdge,&SurfaceEdge::surface,&SurfaceEdge::first,&SurfaceEdge::second>("SurfaceEdge",{"surface","first","second"});}
template<> auto fields<Seam>() {return object<Seam,&Seam::first,&Seam::second,&Seam::reversed>("Seam",{"first","second","reversed"});}
template<> auto fields<SurfaceGroup>() {return object<SurfaceGroup,&SurfaceGroup::id,&SurfaceGroup::name,&SurfaceGroup::surfaces,&SurfaceGroup::seams>("SurfaceGroup",{"id","name","surfaces","seams"});}
template<> auto fields<LayoutVertex>() {return object<LayoutVertex,&LayoutVertex::id,&LayoutVertex::surface_uv,&LayoutVertex::composition_uv>("LayoutVertex",{"id","surfaceUv","compositionUv"});}
template<> auto fields<SurfaceChart>() {return object<SurfaceChart,&SurfaceChart::surface,&SurfaceChart::vertices,&SurfaceChart::triangles>("SurfaceChart",{"surface","vertices","triangles"});}
template<> auto fields<Layout>() {return object<Layout,&Layout::id,&Layout::name,&Layout::group,&Layout::mode,&Layout::charts>("Layout",{"id","name","group","mode","charts"});}
template<> auto fields<DesiredOutputMode>() {return object<DesiredOutputMode,&DesiredOutputMode::width,&DesiredOutputMode::height,&DesiredOutputMode::refresh_hz>("DesiredOutputMode",{"width","height","refreshHz"});}
template<> auto fields<OutputTarget>() {return object<OutputTarget,&OutputTarget::id,&OutputTarget::name,&OutputTarget::adapter,&OutputTarget::endpoint,&OutputTarget::desired,&OutputTarget::enabled,&OutputTarget::blackout,&OutputTarget::freeze>("OutputTarget",{"id","name","adapter","endpoint","desired","enabled","blackout","freeze"});}
template<> auto fields<WarpVertex>() {return object<WarpVertex,&WarpVertex::id,&WarpVertex::surface_uv,&WarpVertex::projector_pixels>("WarpVertex",{"id","surfaceUv","projectorPixels"});}
template<> auto fields<Calibration>() {return object<Calibration,&Calibration::mode,&Calibration::reference_width,&Calibration::reference_height,&Calibration::vertices,&Calibration::triangles,&Calibration::boundary>("Calibration",{"mode","referenceWidth","referenceHeight","vertices","triangles","boundary"});}
template<> auto fields<ProjectionPatch>() {return object<ProjectionPatch,&ProjectionPatch::id,&ProjectionPatch::name,&ProjectionPatch::surface,&ProjectionPatch::output,&ProjectionPatch::calibration,&ProjectionPatch::visual>("ProjectionPatch",{"id","name","surface","output","calibration","visual"},{false,false,false,false,false,true});}
template<> auto fields<Stage>() {return object<Stage,&Stage::surfaces,&Stage::groups,&Stage::layouts,&Stage::outputs,&Stage::patches>("Stage",{"surfaces","groups","layouts","outputs","patches"});}
template<> auto fields<ReplaceDefinitions>() {return object<ReplaceDefinitions,&ReplaceDefinitions::value>("ReplaceDefinitions",{"value"});}
template<> auto fields<ReplaceStage>() {return object<ReplaceStage,&ReplaceStage::value>("ReplaceStage",{"value"});}
template<> auto fields<PlaceSource>() {return object<PlaceSource,&PlaceSource::source,&PlaceSource::keep_alive>("PlaceSource",{"source","keepAlive"});}
template<> auto fields<ShareSource>() {return object<ShareSource,&ShareSource::source>("ShareSource",{"source"});}
template<> auto fields<RemovePlacement>() {return object<RemovePlacement,&RemovePlacement::placement>("RemovePlacement",{"placement"});}
template<> auto fields<BindInput>() {return object<BindInput,&BindInput::owner,&BindInput::input,&BindInput::source,&BindInput::keep_alive>("BindInput",{"owner","input","source","keepAlive"});}
template<> auto fields<UnbindInput>() {return object<UnbindInput,&UnbindInput::owner,&UnbindInput::input>("UnbindInput",{"owner","input"});}
template<> auto fields<SetTransport>() {return object<SetTransport,&SetTransport::instance,&SetTransport::value>("SetTransport",{"instance","value"});}
template<> auto fields<SetKeepAlive>() {return object<SetKeepAlive,&SetKeepAlive::instance,&SetKeepAlive::value>("SetKeepAlive",{"instance","value"});}
template<> auto fields<AssignSurfaceContent>() {return object<AssignSurfaceContent,&AssignSurfaceContent::surface,&AssignSurfaceContent::source>("AssignSurfaceContent",{"surface","source"});}
template<> auto fields<NumericRange>() {return object<NumericRange,&NumericRange::minimum,&NumericRange::maximum>("NumericRange",{"minimum","maximum"});}
template<> auto fields<ParameterDescriptor>() {return object<ParameterDescriptor,&ParameterDescriptor::parameter,&ParameterDescriptor::owner,&ParameterDescriptor::address,&ParameterDescriptor::type,&ParameterDescriptor::value,&ParameterDescriptor::default_value,&ParameterDescriptor::range,&ParameterDescriptor::unit,&ParameterDescriptor::description,&ParameterDescriptor::access,&ParameterDescriptor::persistence,&ParameterDescriptor::available,&ParameterDescriptor::availability_reason>("ParameterDescriptor",{"parameter","owner","address","type","value","defaultValue","range","unit","description","access","persistence","available","availabilityReason"});}
template<> auto fields<ActionDescriptor>() {return object<ActionDescriptor,&ActionDescriptor::id,&ActionDescriptor::address,&ActionDescriptor::payload_type,&ActionDescriptor::description,&ActionDescriptor::access>("ActionDescriptor",{"id","address","payloadType","description","access"});}
template<> auto fields<ParameterWrite>() {return object<ParameterWrite,&ParameterWrite::address,&ParameterWrite::value>("ParameterWrite",{"address","value"});}
struct Version {};
template<> struct Codec<Version> {
    static Json encode(Version) {return 1;}
    static Version decode(const Json& value) {if (!value.is_number_integer() || value != 1) throw Error(ErrorCode::unsupported_version,"Unsupported wire schema version"); return {};}
    static Json shape(SchemaContext&) {return {{"type","integer"},{"const",1}};}
};
struct ProjectDocument {Version version; Definitions definitions; std::map<Id,SourceInstance> sources; std::map<Id,Placement> placements; Stage stage; Id root;};
struct CommandDocument {Version version; Command command;};
struct WriteDocument {Version version; ParameterWrite write;};
struct RegistryDocument {Version version; std::uint64_t revision; std::vector<ParameterDescriptor> parameters; std::vector<ActionDescriptor> actions;};
template<> auto fields<ProjectDocument>() {return object<ProjectDocument,&ProjectDocument::version,&ProjectDocument::definitions,&ProjectDocument::sources,&ProjectDocument::placements,&ProjectDocument::stage,&ProjectDocument::root>("ProjectDocument",{"schemaVersion","definitions","sources","placements","stage","rootPlacement"});}
template<> auto fields<CommandDocument>() {return object<CommandDocument,&CommandDocument::version,&CommandDocument::command>("CommandDocument",{"schemaVersion","command"});}
template<> auto fields<WriteDocument>() {return object<WriteDocument,&WriteDocument::version,&WriteDocument::write>("WriteDocument",{"schemaVersion","write"});}
template<> auto fields<RegistryDocument>() {return object<RegistryDocument,&RegistryDocument::version,&RegistryDocument::revision,&RegistryDocument::parameters,&RegistryDocument::actions>("RegistryDocument",{"schemaVersion","revision","parameters","actions"});}
}
Json parse(std::string_view bytes, Limits limits) {
    if (bytes.size() > limits.bytes) throw Error(ErrorCode::capacity_exceeded,"Wire document byte capacity exceeded");
    std::vector<std::set<std::string>> objects; std::size_t events = 0;
    try {
        return Json::parse(bytes.begin(),bytes.end(),[&](int depth,Json::parse_event_t event,Json& value) {
            if (depth < 0 || static_cast<std::size_t>(depth) > limits.depth || ++events > limits.events)
                throw Error(ErrorCode::capacity_exceeded,"Wire document depth/parser-event capacity exceeded");
            if (event == Json::parse_event_t::object_start) objects.emplace_back();
            else if (event == Json::parse_event_t::key) {
                require(!objects.empty() && objects.back().insert(value.get<std::string>()).second,"Duplicate object key");
            } else if (event == Json::parse_event_t::object_end) objects.pop_back();
            return true;
        });
    } catch (const Json::exception&) {throw Error(ErrorCode::malformed,"Invalid JSON encoding");}
}
Json encode_project(const ProjectState& project) {
    return Codec<ProjectDocument>::encode({{},project.instances.definitions().data(),project.instances.instances(),project.instances.placements(),project.stage.data(),project.root_placement});
}
ProjectState decode_project(const Json& document, ProjectLimits project_limits, InstanceLimits instance_limits, InstancePool::IdGenerator ids) {
    auto value = Codec<ProjectDocument>::decode(document);
    auto instances = InstancePool::restore(DefinitionSnapshot{std::move(value.definitions)},std::move(value.sources),std::move(value.placements),instance_limits,std::move(ids));
    ProjectState state{instances,StageSnapshot{std::move(value.stage),instances,project_limits.stage},std::move(value.root)};
    return *ProjectController{std::move(state),project_limits}.snapshot();
}
Json encode_command(const Command& command) {return Codec<CommandDocument>::encode({{},command});}
Command decode_command(const Json& document) {return Codec<CommandDocument>::decode(document).command;}
Json encode_write(const ParameterWrite& write) {return Codec<WriteDocument>::encode({{},write});}
ParameterWrite decode_write(const Json& document) {return Codec<WriteDocument>::decode(document).write;}
Json describe(const ParameterRegistry& registry) {return Codec<RegistryDocument>::encode({{},registry.revision(),registry.inventory(),registry.actions()});}
std::uint64_t read_unsigned(const Json& value) {return Codec<std::uint64_t>::decode(value);}
MediaTime read_media_time(const Json& value) {return Codec<MediaTime>::decode(value);}
Json schema() {
    SchemaContext context;
    Json roots = Json::array({Codec<ProjectDocument>::shape(context),Codec<CommandDocument>::shape(context),Codec<WriteDocument>::shape(context),Codec<RegistryDocument>::shape(context)});
    return {{"$schema","https://json-schema.org/draft/2020-12/schema"},{"$id","https://github.com/Golden-Geek/MapGyver2/schemas/control-model-v1.schema.json"},
        {"title","MapGyver portable control/model wire documents v1"},{"oneOf",std::move(roots)},{"$defs",std::move(context.definitions)}};
}
} // namespace mapgyver::control::wire
