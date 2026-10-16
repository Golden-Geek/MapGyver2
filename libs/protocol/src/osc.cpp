// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/protocol/osc.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <type_traits>

namespace mapgyver::protocol::osc {
namespace {
void require(bool valid,const char* reason) {if (!valid) throw Error(reason);}
void utf8(const std::string& value) {
    require(value.find('\0') == std::string::npos,"OSC strings cannot contain embedded NUL");
    try {static_cast<void>(nlohmann::json(value).dump());}
    catch (const nlohmann::json::exception&) {throw Error("OSC string is invalid UTF-8");}
}
char tag(const Argument& value) {
    return std::visit([](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T,std::int32_t>) return 'i';
        else if constexpr (std::is_same_v<T,float>) return 'f';
        else if constexpr (std::is_same_v<T,std::string>) return 's';
        else if constexpr (std::is_same_v<T,Blob>) return 'b';
        else if constexpr (std::is_same_v<T,std::int64_t>) return 'h';
        else if constexpr (std::is_same_v<T,double>) return 'd';
        else if constexpr (std::is_same_v<T,Timetag>) return 't';
        else if constexpr (std::is_same_v<T,bool>) return item ? 'T' : 'F';
        else if constexpr (std::is_same_v<T,Nil>) return 'N';
        else return 'I';
    },value);
}
struct Writer {
    Limits limits; std::vector<std::uint8_t> data;
    void reserve(std::size_t bytes) {require(data.size() <= limits.bytes && bytes <= limits.bytes-data.size(),"OSC packet capacity exceeded");}
    void number(std::uint64_t value,unsigned width) {
        reserve(width); for (unsigned index = width; index > 0; --index) data.push_back(static_cast<std::uint8_t>(value>>((index-1)*8)));
    }
    void padded(std::span<const std::uint8_t> value,bool nul) {
        const auto extra = nul ? std::size_t{1} : std::size_t{0};
        require(value.size() <= limits.bytes && extra <= limits.bytes-value.size(),"OSC field capacity exceeded");
        const auto unpadded = value.size()+extra;
        require(unpadded <= std::numeric_limits<std::size_t>::max()-3,"OSC size overflow");
        const auto size = (unpadded+3)&~std::size_t{3}; reserve(size);
        data.insert(data.end(),value.begin(),value.end()); data.insert(data.end(),size-value.size(),0);
    }
    void string(const std::string& value) {
        require(value.size() <= limits.string_bytes,"OSC string capacity exceeded"); utf8(value);
        padded({reinterpret_cast<const std::uint8_t*>(value.data()),value.size()},true);
    }
};
struct Reader {
    std::span<const std::uint8_t> data; Limits limits; std::size_t position = 0;
    std::span<const std::uint8_t> take(std::size_t count) {
        require(position <= data.size() && count <= data.size()-position,"Truncated OSC field");
        const auto value = data.subspan(position,count); position += count; return value;
    }
    std::uint64_t number(unsigned width) {std::uint64_t result = 0; for (auto byte : take(width)) result = (result<<8)|byte; return result;}
    void padding(std::size_t field_size) {
        const auto count = (4-field_size%4)%4;
        for (auto byte : take(count)) require(byte == 0,"Nonzero OSC padding");
    }
    std::string string() {
        const auto start = position;
        while (position < data.size() && data[position] != 0) {
            require(position-start < limits.string_bytes,"OSC string capacity exceeded"); ++position;
        }
        require(position < data.size(),"Unterminated OSC string");
        std::string value(reinterpret_cast<const char*>(data.data()+start),position-start);
        ++position; padding(position-start); utf8(value); return value;
    }
};
Message message(std::span<const std::uint8_t> bytes,Limits limits) {
    Reader reader{bytes,limits}; Message value{reader.string(),{}};
    require(!value.address.empty() && value.address.front() == '/',"OSC message address must begin with slash");
    const auto tags = reader.string(); require(!tags.empty() && tags.front() == ',',"OSC type tags must begin with comma");
    require(tags.size()-1 <= limits.arguments,"OSC argument capacity exceeded");
    value.arguments.reserve(tags.size()-1);
    for (const auto type : std::string_view{tags}.substr(1)) {
        switch (type) {
        case 'i': value.arguments.emplace_back(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(reader.number(4)))); break;
        case 'f': {
            const auto number = std::bit_cast<float>(static_cast<std::uint32_t>(reader.number(4)));
            require(std::isfinite(number),"OSC numeric arguments must be finite"); value.arguments.emplace_back(number); break;
        }
        case 's': value.arguments.emplace_back(reader.string()); break;
        case 'b': {
            const auto size = reader.number(4); require(size <= limits.bytes,"OSC blob capacity exceeded");
            const auto data = reader.take(static_cast<std::size_t>(size));
            value.arguments.emplace_back(Blob{{data.begin(),data.end()}}); reader.padding(static_cast<std::size_t>(size)); break;
        }
        case 'h': value.arguments.emplace_back(std::bit_cast<std::int64_t>(reader.number(8))); break;
        case 'd': {
            const auto number = std::bit_cast<double>(reader.number(8)); require(std::isfinite(number),"OSC numeric arguments must be finite");
            value.arguments.emplace_back(number); break;
        }
        case 't': value.arguments.emplace_back(Timetag{reader.number(8)}); break;
        case 'T': value.arguments.emplace_back(true); break;
        case 'F': value.arguments.emplace_back(false); break;
        case 'N': value.arguments.emplace_back(Nil{}); break;
        case 'I': value.arguments.emplace_back(Impulse{}); break;
        default: throw Error("Unsupported OSC type tag");
        }
    }
    require(reader.position == bytes.size(),"Trailing OSC message bytes"); return value;
}
}
bool Packet::immediate() const noexcept {return std::all_of(bundle_times.begin(),bundle_times.end(),[](Timetag tag){return tag.value == 1;});}
std::string types(const Message& message) {std::string result; for (const auto& argument : message.arguments) result += tag(argument); return result;}
std::vector<std::uint8_t> encode(const Message& message,Limits limits) {
    require(!message.address.empty() && message.address.front() == '/',"OSC message address must begin with slash");
    require(message.arguments.size() <= limits.arguments,"OSC argument capacity exceeded");
    Writer writer{limits,{}}; writer.string(message.address); writer.string(","+types(message));
    for (const auto& argument : message.arguments) std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T,std::int32_t>) writer.number(std::bit_cast<std::uint32_t>(value),4);
        else if constexpr (std::is_same_v<T,float>) {require(std::isfinite(value),"OSC numeric arguments must be finite"); writer.number(std::bit_cast<std::uint32_t>(value),4);}
        else if constexpr (std::is_same_v<T,std::string>) writer.string(value);
        else if constexpr (std::is_same_v<T,Blob>) {
            require(value.bytes.size() <= std::numeric_limits<std::int32_t>::max(),"OSC blob length exceeds int32");
            writer.number(value.bytes.size(),4); writer.padded(value.bytes,false);
        }
        else if constexpr (std::is_same_v<T,std::int64_t>) writer.number(std::bit_cast<std::uint64_t>(value),8);
        else if constexpr (std::is_same_v<T,double>) {require(std::isfinite(value),"OSC numeric arguments must be finite"); writer.number(std::bit_cast<std::uint64_t>(value),8);}
        else if constexpr (std::is_same_v<T,Timetag>) writer.number(value.value,8);
    },argument);
    return std::move(writer.data);
}
std::vector<std::uint8_t> bundle(const std::vector<std::vector<std::uint8_t>>& elements,Timetag time,Limits limits) {
    require(elements.size() <= limits.messages,"OSC bundle element capacity exceeded");
    Writer writer{limits,{}}; writer.string("#bundle"); writer.number(time.value,8);
    for (const auto& element : elements) {
        require(!element.empty() && element.size()%4 == 0 && element.size() <= std::numeric_limits<std::int32_t>::max(),"Invalid OSC bundle element length");
        writer.number(element.size(),4); writer.padded(element,false);
    }
    return std::move(writer.data);
}
Packet decode(std::span<const std::uint8_t> bytes,Limits limits) {
    require(!bytes.empty() && bytes.size() <= limits.bytes && bytes.size()%4 == 0,"Invalid OSC packet size");
    struct Work {std::span<const std::uint8_t> bytes; std::size_t depth;};
    std::vector<Work> pending{{bytes,0}}; Packet result; std::size_t elements = 0;
    while (!pending.empty()) {
        const auto work = pending.back(); pending.pop_back();
        require(work.depth <= limits.depth && ++elements <= limits.elements,"OSC nesting/element capacity exceeded");
        const auto data = work.bytes;
        if (data.size() >= 8 && std::equal(data.begin(),data.begin()+8,std::array<std::uint8_t,8>{'#','b','u','n','d','l','e',0}.begin())) {
            Reader reader{data,limits}; static_cast<void>(reader.take(8)); result.bundle_times.push_back({reader.number(8)});
            std::vector<Work> children;
            while (reader.position < data.size()) {
                const auto count = reader.number(4);
                require(count > 0 && count <= limits.bytes && count%4 == 0,"Invalid OSC bundle element size");
                children.push_back({reader.take(static_cast<std::size_t>(count)),work.depth+1});
                require(children.size() <= limits.messages,"OSC bundle element capacity exceeded");
            }
            for (auto it = children.rbegin(); it != children.rend(); ++it) pending.push_back(*it);
        } else {
            require(result.messages.size() < limits.messages,"OSC message capacity exceeded"); result.messages.push_back(message(data,limits));
        }
    }
    return result;
}
} // namespace mapgyver::protocol::osc
