// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/protocol/osc.hpp>
#include <array>
#include <functional>
#include <iostream>
#include <limits>

using namespace mapgyver::protocol::osc;
namespace {
void check(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
void rejects(const std::function<void()>& operation) {
    try {operation();} catch (const Error&) {return;}
    throw std::runtime_error("Malformed or oversized OSC unexpectedly accepted");
}
void independent_golden_and_types() {
    const Message message{"/x",{std::int32_t{-1},1.5F,std::string{"hi"}}};
    const std::vector<std::uint8_t> golden{0x2f,0x78,0,0,0x2c,0x69,0x66,0x73,0,0,0,0,
        0xff,0xff,0xff,0xff,0x3f,0xc0,0,0,0x68,0x69,0,0};
    check(encode(message) == golden,"OSC encoding disagrees with independently specified big-endian/padding bytes");
    check(decode(golden).messages == std::vector<Message>{message},"OSC golden decoding changed value types");
    const Message extended{"/all",{std::numeric_limits<std::int32_t>::min(),-0.25F,std::string{"média"},Blob{{0,1,255}},
        std::numeric_limits<std::int64_t>::min(),1.25,Timetag{0x1234567812345678ULL},true,false,Nil{},Impulse{}}};
    check(types(extended) == "ifsbhdtTFNI","OSC typetag inventory changed");
    check(decode(encode(extended)).messages.front() == extended,"OSC extended types/UTF-8/blob/timetag lost exact state");
    check(decode(encode({"/empty",{Blob{{}}}})).messages.front().arguments == std::vector<Argument>{Blob{{}}},"Zero-length blob alignment changed");
}
void bundles_and_capacities() {
    const auto first = encode({"/first",{std::int32_t{1}}}), second = encode({"/second",{std::string{"two"}}});
    const auto nested = bundle({first,bundle({second})}); const auto packet = decode(nested);
    check(packet.messages.size() == 2 && packet.messages[0].address == "/first" && packet.messages[1].address == "/second" && packet.immediate(),
          "Nested OSC bundle changed message order or immediate timetag");
    const auto scheduled = decode(bundle({nested},Timetag{0xf123456789abcdefULL}));
    check(!scheduled.immediate() && scheduled.bundle_times.front().value == 0xf123456789abcdefULL,"OSC scheduling bits were rounded or treated as immediate");
    check(decode(bundle({})).messages.empty(),"Empty OSC bundle was not retained as an empty operation");
    Limits limits; limits.bytes = first.size()-1; rejects([&]{static_cast<void>(decode(first,limits));});
    limits = {}; limits.arguments = 0; rejects([&]{static_cast<void>(decode(first,limits));});
    limits = {}; limits.messages = 1; rejects([&]{static_cast<void>(decode(nested,limits));});
    limits = {}; limits.depth = 0; rejects([&]{static_cast<void>(decode(nested,limits));});
    limits = {}; limits.elements = 1; rejects([&]{static_cast<void>(decode(nested,limits));});
    limits = {}; limits.string_bytes = 1; rejects([&]{static_cast<void>(encode({"/long",{}},limits));});
}
void corruption_is_rejected() {
    const auto encoded = encode({"/x",{std::int32_t{9},std::string{"hi"}}});
    for (std::size_t size = 0; size < encoded.size(); ++size) rejects([&]{static_cast<void>(decode(std::span{encoded}.first(size)));});
    auto bad = encoded; bad[3] = 1; rejects([&]{static_cast<void>(decode(bad));});
    bad = encoded; bad[5] = '?'; rejects([&]{static_cast<void>(decode(bad));});
    bad = encoded; bad.push_back(0); rejects([&]{static_cast<void>(decode(bad));});
    bad = encoded; bad.insert(bad.end(),4,0); rejects([&]{static_cast<void>(decode(bad));});
    auto blob = encode({"/blob",{Blob{{1,2,3}}}}); blob.back() = 1; rejects([&]{static_cast<void>(decode(blob));});
    auto utf8 = encode({"/text",{std::string{"ok"}}}); utf8[12] = 0xff; rejects([&]{static_cast<void>(decode(utf8));});
    rejects([]{static_cast<void>(encode({"/bad",{std::numeric_limits<double>::infinity()}}));});
    rejects([]{static_cast<void>(encode({"/bad",{std::string{"a\0b",3}}}));});
    auto nan = encode({"/n",{0.0F}}); nan[8] = 0x7f; nan[9] = 0xc0; rejects([&]{static_cast<void>(decode(nan));});
    auto bundle_bytes = bundle({encoded}); bundle_bytes[19] = 3; rejects([&]{static_cast<void>(decode(bundle_bytes));});
}
}
int main() {
    try {independent_golden_and_types(); bundles_and_capacities(); corruption_is_rejected(); std::cout << "Passed bounded OSC wire contracts\n"; return 0;}
    catch (const std::exception& error) {std::cerr << "OSC contract failed: " << error.what() << '\n'; return 1;}
}
