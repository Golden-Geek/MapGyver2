// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <nlohmann/json.hpp>
namespace mapgyver::experiments {
// This is an experimental diagnostic boundary, not the application's render API.
[[nodiscard]] nlohmann::json d3d11_shared_texture_baseline();
[[nodiscard]] nlohmann::json bgfx_texture_interop(bool hold_producer_key = false);
[[nodiscard]] nlohmann::json bgfx_window_submission();
}
