// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mapgyver/control/wire.hpp>
#include <chrono>

namespace mapgyver::protocol {
using mapgyver::control::wire::Json;
using mapgyver::domain::Id;
struct ServiceLimits {
    std::size_t sessions = 16, cache_entries = 128, cache_bytes = 8388608;
    std::size_t request_bytes = 1048576, response_bytes = 4194304;
    std::size_t journal_events = 256, event_batch = 16, operations_per_request = 1024;
    std::chrono::milliseconds resume_window{60000};
};
class ServiceError : public std::runtime_error {
public:
    ServiceError(std::string code,const char* message) : std::runtime_error(message), code_(std::move(code)) {}
    [[nodiscard]] const std::string& code() const noexcept {return code_;}
private:
    std::string code_;
};
struct Connection {Id session; Json hello;};
struct RenderedState {
    std::uint64_t generation, revision, frame;
    mapgyver::domain::MediaTime show_time;
};
// One control-thread owner. No OS types, native rendering or synchronous IO.
// Transport authenticates/binds a connection to its returned session ID.
class ControlService {
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;
    explicit ControlService(mapgyver::control::ProjectController controller, ServiceLimits limits = {},
        Clock clock = std::chrono::steady_clock::now, mapgyver::domain::InstancePool::IdGenerator ids = Id::random);
    ~ControlService();
    ControlService(const ControlService&) = delete;
    ControlService& operator=(const ControlService&) = delete;
    [[nodiscard]] Connection connect(std::string_view hello);
    void disconnect(const Id& session);
    [[nodiscard]] Json handle(const Id& connection_session,std::string_view request);
    // Transport asks only for a subscribed session. It advances its send cursor
    // after a complete write; the client retains its actually received cursor
    // for reconnect. Kernel write completion is not proof of client receipt.
    [[nodiscard]] std::optional<Json> notifications(const Id& session,std::uint64_t after_event) const;
    [[nodiscard]] std::shared_ptr<const mapgyver::control::ProjectState> snapshot() const;
    [[nodiscard]] std::uint64_t revision() const;
    [[nodiscard]] mapgyver::control::ParameterRegistry registry() const;
    // Control owner consumes native worker observations. Never called from a
    // render deadline callback; no network operation can write these statuses.
    void begin_renderer(std::uint64_t generation);
    void report_rendered(RenderedState frame,std::string reason = {});
    void renderer_failed(std::uint64_t generation,std::string reason);
private:
    struct State;
    ServiceLimits limits_;
    Clock clock_;
    std::shared_ptr<mapgyver::domain::InstancePool::IdGenerator> ids_;
    std::unique_ptr<State> state_;
    void expire();
};
} // namespace mapgyver::protocol
