// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mapgyver/protocol/service.hpp>
#include <memory>

namespace mapgyver::network {
// Poll on the ControlService owner. All sockets/library types remain private.
// This explicit opt-in endpoint is loopback-only; it supplies no remote auth.
class OscQueryServer {
public:
    explicit OscQueryServer(protocol::ControlService& service);
    ~OscQueryServer();
    OscQueryServer(const OscQueryServer&) = delete;
    OscQueryServer& operator=(const OscQueryServer&) = delete;
    void poll();
    [[nodiscard]] protocol::Json endpoints() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
