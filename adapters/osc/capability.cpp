// SPDX-License-Identifier: GPL-3.0-or-later
#include "capability.hpp"
#include <array>
#include <cstdint>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#elif defined(__linux__)
#include <sys/random.h>
#include <cerrno>
#elif defined(__APPLE__)
#include <cstdlib>
#endif
namespace mapgyver::network {
std::string make_capability() {
    std::array<std::uint8_t,32> bytes{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw std::runtime_error("System capability generation failed");
#elif defined(__linux__)
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto size = getrandom(bytes.data()+offset,bytes.size()-offset,0);
        if (size < 0 && errno == EINTR) continue;
        if (size <= 0) throw std::runtime_error("System capability generation failed");
        offset += static_cast<std::size_t>(size);
    }
#elif defined(__APPLE__)
    arc4random_buf(bytes.data(),bytes.size());
#else
    throw std::runtime_error("System capability generation unavailable on this platform");
#endif
    constexpr char hex[] = "0123456789abcdef";
    std::string result; result.reserve(64);
    for (const auto byte : bytes) {result.push_back(hex[byte>>4]); result.push_back(hex[byte&15]);}
    return result;
}
bool capability_matches(std::string_view expected,std::string_view supplied) noexcept {
    if (supplied.size() != 64 || expected.size() != 64) return false;
    unsigned difference = 0;
    for (std::size_t index = 0; index < 64; ++index)
        difference |= static_cast<unsigned char>(expected[index])^static_cast<unsigned char>(supplied[index]);
    return difference == 0;
}
}
