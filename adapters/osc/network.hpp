// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mapgyver/protocol/service.hpp>
#include <memory>

namespace mapgyver::network {
struct Options {
    std::string editor_directory; // Preload bounded static assets at startup.
    std::string development_origin; // Explicit http://127.0.0.1:<port> only.
};
// Poll on the ControlService owner. All sockets/library types remain private.
// This explicit opt-in endpoint is loopback-only; it supplies no remote auth.
class OscQueryServer {
public:
    explicit OscQueryServer(protocol::ControlService& service,Options options = {});
    ~OscQueryServer();
    OscQueryServer(const OscQueryServer&) = delete;
    OscQueryServer& operator=(const OscQueryServer&) = delete;
    void poll();
    [[nodiscard]] protocol::Json endpoints() const;
    // Private local launch information. Never include in HOST_INFO/HTTP replies.
    [[nodiscard]] protocol::Json editor_bootstrap() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
