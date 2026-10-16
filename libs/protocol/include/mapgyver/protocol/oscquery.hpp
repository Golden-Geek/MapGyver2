// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mapgyver/protocol/osc.hpp>
#include <mapgyver/control/wire.hpp>
#include <mapgyver/protocol/service.hpp>
#include <set>

namespace mapgyver::protocol::oscquery {
using control::wire::Json;
struct Host {
    std::string name = "MapGyver", address = "127.0.0.1";
    std::uint16_t osc_port = 0, websocket_port = 0;
    bool websockets = false; // Set only by a bound/implemented transport.
};
struct HttpResult {unsigned status; Json body;};
struct ActionInvocation {std::optional<control::Command> command; bool undo = false; std::uint64_t revision = 0;};
struct Changes {bool structure = false; std::vector<osc::Message> values;};
// Frozen projection of the authoritative registry. It owns no writable mirror
// model or socket. All writes become typed operations for the same validator.
class Tree {
public:
    explicit Tree(const control::ParameterRegistry& registry,Host host = {});
    [[nodiscard]] HttpResult query(std::string_view target) const;
    [[nodiscard]] Json host_info() const;
    [[nodiscard]] control::ParameterWrite write(const osc::Message& message) const;
    [[nodiscard]] ActionInvocation invoke(const osc::Message& message) const;
    [[nodiscard]] osc::Message value(const std::string& address) const;
    [[nodiscard]] Changes changes_from(const Tree& previous) const;
    [[nodiscard]] const std::set<std::string>& methods() const noexcept {return methods_;}
    [[nodiscard]] bool contains(const std::string& address) const {return nodes_.contains(address);}
private:
    struct Node {Json attributes; std::map<std::string,std::string> children;};
    [[nodiscard]] Json subtree(const std::string& address) const;
    void insert(const std::string& address,Json attributes);
    Host host_;
    std::map<std::string,Node> nodes_;
    std::map<std::string,control::ParameterDescriptor> parameters_;
    std::map<std::string,control::ActionDescriptor> actions_;
    std::set<std::string> methods_;
};
struct Notifications {std::optional<Json> structure; std::vector<osc::Message> values;};
// One WebSocket client's bounded exact-path filter. Connections construct a new
// instance on reconnect; a container subscription never implies descendants.
class Subscription {
public:
    explicit Subscription(std::size_t capacity = 1024) : capacity_(capacity) {}
    void command(const Json& message,const Tree& tree);
    [[nodiscard]] Notifications select(const Changes& changes,const Tree& tree);
    [[nodiscard]] std::size_t size() const noexcept {return paths_.size();}
private:
    std::size_t capacity_;
    std::set<std::string> paths_;
};
// One operator-granted external OSC channel. Starts read-only, never claims
// another editor's ownership. The transport calls close before destroying it.
class Ingress {
public:
    explicit Ingress(ControlService& service);
    Ingress(const Ingress&) = delete;
    Ingress& operator=(const Ingress&) = delete;
    [[nodiscard]] const Id& session() const noexcept {return connection_.session;}
    [[nodiscard]] Json receive(std::span<const std::uint8_t> packet);
    void close();
private:
    ControlService& service_;
    Connection connection_;
    std::uint64_t sequence_ = 0;
    bool closed_ = false;
};
} // namespace mapgyver::protocol::oscquery
