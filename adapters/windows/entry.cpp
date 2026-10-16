// SPDX-License-Identifier: GPL-3.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
#include <iostream>
#include <array>
#ifndef MAPGYVER_ENTRYPOINT
#define MAPGYVER_ENTRYPOINT engine_main
#endif
int MAPGYVER_ENTRYPOINT(int argc,char** argv);
int wmain(int argc,wchar_t** argv) {
    std::array<wchar_t,32768> module{};
    const auto module_length = GetModuleFileNameW(nullptr,module.data(),static_cast<DWORD>(module.size()));
    if (module_length == 0 || module_length >= module.size()) {std::cerr << "Could not locate the executable\n"; return 2;}
    std::vector<std::string> utf8; utf8.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        const auto* source = index == 0 ? module.data() : argv[index];
        const auto length = WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,source,-1,nullptr,0,nullptr,nullptr);
        if (length <= 0) {std::cerr << "Invalid command-line encoding\n"; return 2;}
        std::string value(static_cast<std::size_t>(length),'\0');
        if (!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,source,-1,value.data(),length,nullptr,nullptr)) return 2;
        value.pop_back(); utf8.push_back(std::move(value));
    }
    std::vector<char*> arguments;
    for (auto& value : utf8) arguments.push_back(value.data());
    arguments.push_back(nullptr);
    return MAPGYVER_ENTRYPOINT(argc,arguments.data());
}
