// SPDX-License-Identifier: GPL-3.0-or-later
#include "network.hpp"
#include <mapgyver/protocol/oscquery.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <array>
#include <deque>
#include <set>

namespace mapgyver::network {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using udp = asio::ip::udp;
using Error = boost::system::error_code;
using namespace std::chrono_literals;
namespace {
constexpr std::size_t peer_limit = 16, message_limit = 1048576;
constexpr std::size_t queue_limit = 4194304, queue_messages = 256;
struct Frame {bool binary; std::string bytes;};
}
struct OscQueryServer::Impl {
    struct Peer;
    asio::io_context io{1};
    tcp::acceptor acceptor{io};
    udp::socket datagram{io};
    protocol::ControlService& service;
    protocol::oscquery::Ingress ingress;
    protocol::oscquery::Host host;
    std::unique_ptr<protocol::oscquery::Tree> tree;
    std::uint64_t revision = 0, udp_accepted = 0, udp_rejected = 0, peer_closes = 0;
    std::uint64_t write_timeouts = 0, http_timeouts = 0;
    std::uint64_t notification_rejections = 0;
    std::array<std::uint8_t,65536> udp_bytes{};
    udp::endpoint sender;
    std::set<std::shared_ptr<Peer>> peers;
    bool stopped = false;
    explicit Impl(protocol::ControlService& value);
    ~Impl();
    void accept();
    void receive();
    void refresh();
    void dispatch(std::span<const std::uint8_t> bytes);
    protocol::Json endpoints() const;
};
struct OscQueryServer::Impl::Peer : std::enable_shared_from_this<Peer> {
    Impl& owner;
    websocket::stream<beast::tcp_stream> stream;
    beast::flat_buffer input{message_limit};
    http::request_parser<http::empty_body> parser;
    std::optional<http::response<http::string_body>> response;
    asio::steady_timer write_timer;
    protocol::oscquery::Subscription subscription;
    std::deque<std::shared_ptr<const Frame>> output;
    std::size_t output_bytes = 0;
    bool upgraded = false, closed = false, writing = false;
    Peer(Impl& server,tcp::socket socket) : owner(server),stream(std::move(socket)),write_timer(server.io) {
        parser.header_limit(16384); parser.body_limit(0);
        beast::get_lowest_layer(stream).socket().set_option(asio::socket_base::send_buffer_size{16384});
    }
    void close() {
        if (closed) return;
        closed = true; ++owner.peer_closes;
        Error ignored;
        write_timer.cancel();
        auto& socket = beast::get_lowest_layer(stream).socket();
        socket.cancel(ignored); socket.shutdown(tcp::socket::shutdown_both,ignored); socket.close(ignored);
        // Pending handlers retain this peer and their exact buffers until completion.
    }
    void start() {
        beast::get_lowest_layer(stream).expires_after(5s);
        http::async_read(stream.next_layer(),input,parser,[self=shared_from_this()](Error error,std::size_t) {
            if (error == beast::error::timeout) ++self->owner.http_timeouts;
            if (error) {self->close(); return;}
            self->request();
        });
    }
    void respond(unsigned status,protocol::Json body) {
        response.emplace(static_cast<http::status>(status),11);
        response->set(http::field::server,"MapGyver");
        response->set(http::field::content_type,"application/json; charset=utf-8");
        response->set(http::field::cache_control,"no-store");
        response->set(http::field::connection,"close");
        if (status != 204) response->body() = body.dump();
        if (response->body().size() > queue_limit) {
            response->result(http::status::insufficient_storage);
            response->body() = R"({"error":"response_capacity"})";
        }
        response->prepare_payload();
        beast::get_lowest_layer(stream).expires_after(2s);
        http::async_write(stream.next_layer(),*response,[self=shared_from_this()](Error,std::size_t){self->close();});
    }
    void request() {
        const auto& request = parser.get();
        const auto host_header = request[http::field::host];
        const auto port = std::to_string(owner.host.websocket_port);
        if (host_header != "127.0.0.1:"+port && host_header != "localhost:"+port) {
            respond(403,{{"error","invalid_host"}}); return;
        }
        // Browser write access is introduced with the authenticated editor channel.
        if (request.find(http::field::origin) != request.end()) {
            respond(403,{{"error","browser_origin_unavailable"}}); return;
        }
        if (request.method() != http::verb::get) {respond(405,{{"error","get_required"}}); return;}
        if (websocket::is_upgrade(request)) {
            if (request.target() != "/") {respond(404,{{"error","missing_websocket_path"}}); return;}
            if (input.size() != 0) {respond(400,{{"error","wait_for_upgrade_before_frames"}}); return;}
            beast::get_lowest_layer(stream).expires_never();
            stream.set_option(websocket::stream_base::timeout{5s,30s,true});
            stream.read_message_max(message_limit);
            stream.async_accept(request,[self=shared_from_this()](Error error) {
                if (error) {self->close(); return;}
                self->upgraded = true; self->read();
            });
            return;
        }
        const auto result = owner.tree->query(std::string_view{request.target().data(),request.target().size()});
        auto body = result.body;
        if (body.is_object() && body.contains("EXTENSIONS")) body["X_MAPGYVER"]["ENDPOINT"] = owner.endpoints();
        respond(result.status,std::move(body));
    }
    void read() {
        stream.async_read(input,[self=shared_from_this()](Error error,std::size_t) {
            if (error || self->closed) {self->close(); return;}
            const auto bytes = beast::buffers_to_string(self->input.data());
            try {
                if (self->stream.got_text()) self->subscription.command(control::wire::parse(bytes),*self->owner.tree);
                else {
                    const auto reply = self->owner.ingress.receive(std::span{reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()});
                    // Bare OSC is not a reliable envelope; expose rejection, never fake success.
                    if (reply.at("status") == "rejected") throw protocol::osc::Error("OSC command rejected");
                    self->owner.refresh();
                }
            } catch (const protocol::osc::Error&) {self->close(); return;}
              catch (const control::wire::Error&) {self->close(); return;}
              catch (const control::RegistryError&) {self->close(); return;}
              catch (const domain::ModelError&) {self->close(); return;}
            self->input.consume(self->input.size());
            self->read();
        });
    }
    void enqueue(Frame frame) {
        if (closed || !upgraded) return;
        if (output.size() >= queue_messages || frame.bytes.size() > queue_limit-output_bytes) {close(); return;}
        output_bytes += frame.bytes.size(); output.push_back(std::make_shared<const Frame>(std::move(frame)));
        if (!writing) write();
    }
    void write() {
        if (closed || output.empty()) return;
        writing = true;
        const auto frame = output.front(); stream.binary(frame->binary);
        write_timer.expires_after(2s);
        write_timer.async_wait([self=shared_from_this()](Error error){
            if (!error && !self->closed) {++self->owner.write_timeouts; self->close();}
        });
        stream.async_write(asio::buffer(frame->bytes),[self=shared_from_this(),frame](Error error,std::size_t) {
            self->write_timer.cancel();
            if (error || self->closed) {self->close(); return;}
            self->output_bytes -= frame->bytes.size(); self->output.pop_front(); self->writing = false;
            self->write();
        });
    }
    void notify(const protocol::oscquery::Changes& changes) {
        if (closed || !upgraded) return;
        const auto notification = subscription.select(changes,*owner.tree);
        if (notification.structure) enqueue({false,notification.structure->dump()});
        for (const auto& value : notification.values) {
            if (closed) break;
            try {
                const auto bytes = protocol::osc::encode(value);
                enqueue({true,std::string{bytes.begin(),bytes.end()}});
            } catch (const protocol::osc::Error&) {
                // A valid model value can exceed this transport's OSC encoding
                // capacity. Close its subscriber; never roll back/kill the model.
                ++owner.notification_rejections; close();
            }
        }
    }
};
OscQueryServer::Impl::Impl(protocol::ControlService& value) : service(value),ingress(service) {
    const auto address = asio::ip::make_address_v4("127.0.0.1");
    acceptor.open(tcp::v4()); acceptor.bind({address,0}); acceptor.listen(16);
    datagram.open(udp::v4()); datagram.bind({address,0});
    host.osc_port = datagram.local_endpoint().port(); host.websocket_port = acceptor.local_endpoint().port(); host.websockets = true;
    tree = std::make_unique<protocol::oscquery::Tree>(service.registry(),host); revision = service.revision();
    accept(); receive();
}
OscQueryServer::Impl::~Impl() {
    stopped = true;
    Error ignored; acceptor.close(ignored); datagram.close(ignored);
    for (const auto& peer : peers) peer->close();
    ingress.close(); io.stop();
    // io_context destroys all pending handlers while their buffers are still owned.
}
void OscQueryServer::Impl::accept() {
    acceptor.async_accept([this](Error error,tcp::socket socket) {
        if (stopped) return;
        std::erase_if(peers,[](const auto& peer){return peer->closed;});
        if (!error && peers.size() < peer_limit) {
            auto peer = std::make_shared<Peer>(*this,std::move(socket)); peers.insert(peer); peer->start();
        }
        if (error && error != asio::error::operation_aborted) throw boost::system::system_error(error);
        accept();
    });
}
void OscQueryServer::Impl::receive() {
    datagram.async_receive_from(asio::buffer(udp_bytes),sender,[this](Error error,std::size_t size) {
        if (stopped) return;
        if (!error && sender.address().is_loopback()) dispatch(std::span{udp_bytes.data(),size});
        else if (error != asio::error::operation_aborted) ++udp_rejected;
        receive();
    });
}
void OscQueryServer::Impl::dispatch(std::span<const std::uint8_t> bytes) {
    try {
        const auto reply = ingress.receive(bytes);
        if (reply.at("status") == "accepted") ++udp_accepted; else ++udp_rejected;
        refresh();
    } catch (const protocol::osc::Error&) {++udp_rejected;}
      catch (const control::wire::Error&) {++udp_rejected;}
      catch (const control::RegistryError&) {++udp_rejected;}
      catch (const domain::ModelError&) {++udp_rejected;}
}
void OscQueryServer::Impl::refresh() {
    if (revision == service.revision()) return;
    auto next = std::make_unique<protocol::oscquery::Tree>(service.registry(),host);
    const auto changes = next->changes_from(*tree);
    tree = std::move(next); revision = service.revision();
    for (const auto& peer : peers) peer->notify(changes);
}
protocol::Json OscQueryServer::Impl::endpoints() const {
    return {{"address",host.address},{"httpPort",host.websocket_port},{"websocketPort",host.websocket_port},{"oscPort",host.osc_port},
        {"oscSessionId",ingress.session().str()},{"access","loopback_operator_granted_osc_channel"},
        {"peerLimit",peer_limit},{"messageBytes",message_limit},{"queueBytes",queue_limit},{"queueMessages",queue_messages},
        {"httpTimeoutMs",5000},{"writeTimeoutMs",2000},{"websocketIdleTimeoutMs",30000},
        {"authoredRevision",std::to_string(service.revision())},
        {"udpAccepted",udp_accepted},{"udpRejected",udp_rejected},{"peerCloses",peer_closes},
        {"websocketWriteTimeouts",write_timeouts},{"httpReadTimeouts",http_timeouts},
        {"notificationRejections",notification_rejections},{"oscStringBytes",protocol::osc::Limits{}.string_bytes},
        {"discovery","unavailable"},{"remoteAuthorization","unavailable"}};
}
OscQueryServer::OscQueryServer(protocol::ControlService& service) : impl_(std::make_unique<Impl>(service)) {}
OscQueryServer::~OscQueryServer() = default;
void OscQueryServer::poll() {
    impl_->refresh(); impl_->io.restart();
    for (unsigned count = 0; count < 64 && impl_->io.poll_one(); ++count) {}
    impl_->refresh();
    std::erase_if(impl_->peers,[](const auto& peer){return peer->closed;});
}
protocol::Json OscQueryServer::endpoints() const {return impl_->endpoints();}
}
