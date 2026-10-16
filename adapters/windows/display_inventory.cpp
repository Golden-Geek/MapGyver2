// SPDX-License-Identifier: GPL-3.0-or-later
#include "display_inventory.hpp"
#include <Windows.h>
#include <algorithm>
#include <map>
#include <tuple>

namespace mapgyver::windows {
namespace {
void require(LONG status,const char* operation) {
    if (status != ERROR_SUCCESS) throw runtime::OutputError(std::string{operation}+" failed: Win32 "+std::to_string(status));
}
std::string utf8(std::span<const wchar_t> input) {
    const auto end = std::find(input.begin(),input.end(),L'\0');
    if (end == input.end()) throw runtime::OutputError("Native display identity is not terminated within its bounded field");
    const auto count = static_cast<int>(end-input.begin());
    if (!count) return {};
    const auto size = WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,input.data(),count,nullptr,0,nullptr,nullptr);
    if (!size) throw runtime::OutputError("Display identity contains invalid UTF-16");
    std::string result(static_cast<std::size_t>(size),'\0');
    if (WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,input.data(),count,result.data(),size,nullptr,nullptr) != size)
        throw runtime::OutputError("Display identity UTF-8 conversion failed");
    return result;
}
}
std::vector<runtime::Display> detected_displays(std::size_t capacity) {
    if (!capacity || capacity > 256) throw runtime::OutputError("Native display inventory capacity must be 1..256");
    std::vector<DISPLAYCONFIG_PATH_INFO> paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> modes;
    bool observed = false;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        UINT32 path_count = 0, mode_count = 0;
        require(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&path_count,&mode_count),"GetDisplayConfigBufferSizes");
        if (path_count > capacity || mode_count > capacity*3) throw runtime::OutputError("Native display topology exceeds bounded inventory capacity");
        if (!path_count) return {};
        paths.resize(path_count);modes.resize(mode_count);
        const auto status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&path_count,paths.data(),&mode_count,modes.data(),nullptr);
        if (status == ERROR_INSUFFICIENT_BUFFER) continue;
        require(status,"QueryDisplayConfig");paths.resize(path_count);modes.resize(mode_count);observed = true;break;
    }
    if (!observed) throw runtime::OutputError("Native display topology changed during all three bounded inventory attempts");
    using Source = std::tuple<LONG,DWORD,UINT32>;
    std::map<Source,std::size_t> source_users;
    for (const auto& path : paths) ++source_users[{path.sourceInfo.adapterId.HighPart,path.sourceInfo.adapterId.LowPart,path.sourceInfo.id}];
    std::vector<runtime::Display> result;result.reserve(paths.size());
    for (const auto& path : paths) {
        const auto index = path.sourceInfo.modeInfoIdx;
        if (index >= modes.size() || modes[index].infoType != DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE ||
            modes[index].id != path.sourceInfo.id || modes[index].adapterId.HighPart != path.sourceInfo.adapterId.HighPart ||
            modes[index].adapterId.LowPart != path.sourceInfo.adapterId.LowPart) throw runtime::OutputError("Native display source mode is unavailable/inconsistent");
        const auto& source = modes[index].sourceMode;
        DISPLAYCONFIG_TARGET_DEVICE_NAME name{};
        name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;name.header.size = sizeof(name);
        name.header.adapterId = path.targetInfo.adapterId;name.header.id = path.targetInfo.id;
        require(DisplayConfigGetDeviceInfo(&name.header),"DisplayConfigGetDeviceInfo");
        auto endpoint = utf8(name.monitorDevicePath);
        if (endpoint.empty()) throw runtime::OutputError("Native display target has no explicit device path");
        runtime::Display display{std::move(endpoint),utf8(name.monitorFriendlyDeviceName),
            {source.width,source.height,{path.targetInfo.refreshRate.Numerator,path.targetInfo.refreshRate.Denominator}},source.position.x,source.position.y};
        if (source_users.at({path.sourceInfo.adapterId.HighPart,path.sourceInfo.adapterId.LowPart,path.sourceInfo.id}) != 1) display.reason = "output_display_source_cloned";
        if (!display.mode.width || !display.mode.height || display.mode.refresh_hz.numerator <= 0 || display.mode.refresh_hz.denominator <= 0) display.reason = "output_display_mode_unavailable";
        result.push_back(std::move(display));
    }
    return result;
}
}
