// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mapgyver/protocol/service.hpp>
#include <functional>
#include <string>
#include <string_view>

namespace mapgyver::windows {
// Control/IO owner only. SDK handles, ACLs and overlapped buffers stay private.
void serve_named_pipe(protocol::ControlService& service,std::string_view name,
                      const std::function<void(const std::string&)>& ready,
                      const std::function<void()>& poll = {});
[[nodiscard]] protocol::Json pipe_capabilities();
void request_pipe_stop() noexcept;
}
