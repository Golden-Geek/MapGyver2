// SPDX-License-Identifier: GPL-3.0-or-later
#include "network.hpp"
#include "capability.hpp"
#include <mapgyver/protocol/oscquery.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <array>
#include <deque>
#include <set>
#include <filesystem>
#include <fstream>
#include <charconv>
#include <cstdio>
#include <cstdlib>

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
struct Frame {bool binary; std::string bytes; std::optional<std::uint64_t> cursor = {};};
struct Asset {std::string bytes,mime;};
bool development_origin(const std::string& origin) {
    constexpr std::string_view prefix = "http://127.0.0.1:";
    if (!origin.starts_with(prefix)) return false;
    unsigned port = 0; const auto text = std::string_view{origin}.substr(prefix.size());
    const auto parsed = std::from_chars(text.data(),text.data()+text.size(),port);
    return parsed.ec == std::errc{} && parsed.ptr == text.data()+text.size() && port > 0 && port <= 65535 && std::to_string(port) == text;
}
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
    std::optional<int> socket_send_buffer;
    std::array<std::uint8_t,65536> udp_bytes{};
    udp::endpoint sender;
    std::set<std::shared_ptr<Peer>> peers;
    bool stopped = false;
    Options options;
    std::string capability = make_capability();
    std::map<std::string,Asset> assets;
    explicit Impl(protocol::ControlService& value,Options configuration);
    ~Impl();
    void accept();
    void receive();
    void refresh();
    void dispatch(std::span<const std::uint8_t> bytes);
    protocol::Json endpoints() const;
    protocol::Json editor_bootstrap() const;
};
struct OscQueryServer::Impl::Peer : std::enable_shared_from_this<Peer> {
    Impl& owner;
    websocket::stream<beast::tcp_stream> stream;
    beast::flat_buffer input{message_limit};
    http::request_parser<http::empty_body> parser;
    std::optional<http::response<http::string_body>> response;
    asio::steady_timer write_timer;
    asio::steady_timer hello_timer;
    protocol::oscquery::Subscription subscription;
    std::deque<std::shared_ptr<const Frame>> output;
    std::size_t output_bytes = 0;
    bool upgraded = false, closed = false, writing = false;
    bool control_channel = false;
    bool close_after_write = false;
    std::optional<domain::Id> session;
    std::uint64_t cursor = 0;
    Peer(Impl& server,tcp::socket socket) : owner(server),stream(std::move(socket)),write_timer(server.io),hello_timer(server.io) {
        parser.header_limit(16384); parser.body_limit(0);
        beast::get_lowest_layer(stream).socket().set_option(asio::socket_base::send_buffer_size{16384});
        asio::socket_base::send_buffer_size observed;
        beast::get_lowest_layer(stream).socket().get_option(observed); owner.socket_send_buffer = observed.value();
    }
    void close() {
        if (closed) return;
        closed = true; ++owner.peer_closes;
        if (session) {owner.service.disconnect(*session); session.reset();}
        Error ignored;
        write_timer.cancel();
        hello_timer.cancel();
        stream.set_option(websocket::stream_base::timeout{websocket::stream_base::none(),websocket::stream_base::none(),false});
        auto& lowest = beast::get_lowest_layer(stream);
        lowest.socket().shutdown(tcp::socket::shutdown_both,ignored);
        lowest.close(); // Also cancel Beast's internal TCP deadline timer.
        // Pending handlers retain this peer and their exact buffers until completion.
    }
    void start() {
        beast::get_lowest_layer(stream).expires_after(5s);
        http::async_read(stream.next_layer(),input,parser,[self=shared_from_this()](Error error,std::size_t) {
            if (self->closed) return;
            if (error == beast::error::timeout) ++self->owner.http_timeouts;
            if (error) {self->close(); return;}
            self->request();
        });
    }
    void respond_bytes(unsigned status,std::string body,const std::string& mime) {
        response.emplace(static_cast<http::status>(status),11);
        response->set(http::field::server,"MapGyver");
        response->set(http::field::content_type,mime);
        response->set(http::field::cache_control,"no-store");
        response->set(http::field::connection,"close");
        response->set(http::field::referrer_policy,"no-referrer");
        response->set("X-Content-Type-Options","nosniff");
        response->set("Content-Security-Policy","default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' blob:; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'");
        if (status != 204) response->body() = std::move(body);
        if (response->body().size() > queue_limit) {
            response->result(http::status::insufficient_storage);
            response->body() = R"({"error":"response_capacity"})";
        }
        response->prepare_payload();
        beast::get_lowest_layer(stream).expires_after(2s);
        http::async_write(stream.next_layer(),*response,[self=shared_from_this()](Error,std::size_t){self->close();});
    }
    void respond(unsigned status,const protocol::Json& body) {respond_bytes(status,status == 204 ? std::string{} : body.dump(),"application/json; charset=utf-8");}
    void request() {
        const auto& request = parser.get();
        const auto host_header = request[http::field::host];
        const auto port = std::to_string(owner.host.websocket_port);
        if (host_header != "127.0.0.1:"+port && host_header != "localhost:"+port) {
            respond(403,{{"error","invalid_host"}}); return;
        }
        if (request.method() != http::verb::get) {respond(405,{{"error","get_required"}}); return;}
        const auto origin = request[http::field::origin];
        const auto own_origin = "http://"+std::string{host_header};
        const auto target = std::string{request.target()};
        control_channel = websocket::is_upgrade(request) && target == "/control";
        if (control_channel) {
            const auto protocols = std::string{request[http::field::sec_websocket_protocol]};
            constexpr std::string_view prefix = "mapgyver.control.v1, token.";
            if (!protocols.starts_with(prefix) || !capability_matches(owner.capability,std::string_view{protocols}.substr(prefix.size()))) {
                respond(403,{{"error","capability_required"}}); return;
            }
            if (!origin.empty() && origin != own_origin && (owner.options.development_origin.empty() || origin != owner.options.development_origin)) {
                respond(403,{{"error","origin_rejected"}}); return;
            }
        } else if (!origin.empty() && (websocket::is_upgrade(request) || origin != own_origin)) {
            respond(403,{{"error","browser_origin_unavailable"}}); return;
        }
        if (websocket::is_upgrade(request)) {
            if (request.target() != "/" && !control_channel) {respond(404,{{"error","missing_websocket_path"}}); return;}
            if (input.size() != 0) {respond(400,{{"error","wait_for_upgrade_before_frames"}}); return;}
            beast::get_lowest_layer(stream).expires_never();
            stream.set_option(websocket::stream_base::timeout{5s,30s,true});
            stream.read_message_max(message_limit);
            if (control_channel) stream.set_option(websocket::stream_base::decorator([](websocket::response_type& reply){reply.set(http::field::sec_websocket_protocol,"mapgyver.control.v1");}));
            stream.async_accept(request,[self=shared_from_this()](Error error) {
                if (error || self->closed) {self->close(); return;}
                self->upgraded = true;
                if (self->control_channel) {
                    self->hello_timer.expires_after(5s);
                    self->hello_timer.async_wait([self](Error failure){if (!failure) self->close();});
                }
                self->read();
            });
            return;
        }
        if (target == "/preview") {respond(503,{{"available",false},{"reason","render_worker_not_connected"},{"appliedRevision",nullptr},{"frame",nullptr}}); return;}
        if (target.starts_with("/editor")) {
            const auto route = target == "/editor" || target == "/editor/" ? "/editor/index.html" : target;
            const auto found = owner.assets.find(route);
            if (found == owner.assets.end()) {respond(404,{{"error","editor_asset_unavailable"}}); return;}
            respond_bytes(200,found->second.bytes,found->second.mime); return;
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
                if (self->control_channel) self->control(bytes);
                else if (self->stream.got_text()) self->subscription.command(control::wire::parse(bytes),*self->owner.tree);
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
              catch (const protocol::ServiceError&) {self->close(); return;}
            self->input.consume(self->input.size());
            if (!self->closed && !self->close_after_write) self->read();
        });
    }
    void control(const std::string& bytes) {
        if (!stream.got_text()) {close(); return;}
        if (!session) {
            std::optional<protocol::Connection> connected;
            try {connected = owner.service.connect(bytes);}
            catch (const protocol::ServiceError& error) {
                close_after_write = true; hello_timer.cancel();
                enqueue({false,protocol::Json{{"protocolVersion",1},{"type","hello_error"},{"error",{{"code",error.code()},{"message",std::string{error.what()}.substr(0,512)}}}}.dump()});
                return;
            }
            const auto& connection = *connected; hello_timer.cancel();
            session = connection.session;
            cursor = control::wire::read_unsigned(connection.hello.at("snapshot").at("eventSequence"));
            enqueue({false,connection.hello.dump()});
        } else {
            const auto reply = owner.service.handle(*session,bytes);
            std::optional<std::uint64_t> next;
            if (reply.at("status") == "accepted" && reply.at("payload").is_object() && reply.at("payload").value("type","") == "events")
                next = control::wire::read_unsigned(reply.at("payload").at("cursor"));
            enqueue({false,reply.dump(),next});
        }
        owner.refresh();
    }
    void events() {
        if (!closed && control_channel && session && output.empty()) {
            if (const auto notification = owner.service.notifications(*session,cursor))
                enqueue({false,protocol::Json{{"protocolVersion",1},{"type","notification"},{"payload",*notification}}.dump(),control::wire::read_unsigned(notification->at("cursor"))});
        }
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
            if (frame->cursor) self->cursor = *frame->cursor;
            self->output_bytes -= frame->bytes.size(); self->output.pop_front(); self->writing = false;
            if (self->close_after_write) {self->close(); return;}
            self->write();
        });
    }
    void notify(const protocol::oscquery::Changes& changes) {
        if (closed || !upgraded || control_channel) return;
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
OscQueryServer::Impl::Impl(protocol::ControlService& value,Options configuration) : service(value),ingress(service),options(std::move(configuration)) {
    if (!options.development_origin.empty() && !development_origin(options.development_origin)) throw std::runtime_error("Development origin must be an explicit loopback HTTP port");
    if (!options.editor_directory.empty()) {
        namespace fs = std::filesystem;
        const auto root = fs::canonical(fs::path{std::u8string{options.editor_directory.begin(),options.editor_directory.end()}});
        std::uintmax_t total = 0; std::size_t count = 0;
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (++count > 128 || fs::is_symlink(entry.symlink_status())) throw std::runtime_error("Editor asset directory exceeds capacity or contains a symlink");
            if (entry.is_directory()) continue;
            if (!entry.is_regular_file()) throw std::runtime_error("Editor asset is not a regular file");
            const auto relative = entry.path().lexically_relative(root).generic_string();
            if (relative.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/._-") != std::string::npos)
                throw std::runtime_error("Editor asset path contains unsupported characters");
            const auto size = entry.file_size();
            if (size > queue_limit || total+size > 16777216) throw std::runtime_error("Editor assets exceed byte capacity");
            total += size;
            const auto extension = entry.path().extension().string();
            const auto mime = extension == ".html" ? "text/html; charset=utf-8" : extension == ".js" ? "text/javascript; charset=utf-8" : extension == ".css" ? "text/css; charset=utf-8" : "application/octet-stream";
            std::ifstream file{entry.path(),std::ios::binary}; std::string bytes(static_cast<std::size_t>(size),'\0');
            if (!file.read(bytes.data(),static_cast<std::streamsize>(size)) || file.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Could not preload consistent editor asset");
            assets.emplace("/editor/"+relative,Asset{std::move(bytes),mime});
        }
        if (!assets.contains("/editor/index.html")) throw std::runtime_error("Editor index.html is missing");
    }
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
    ingress.close();
    // Cancellation is a request, not completion. Drain while member buffers and
    // all peers are still alive, including the outstanding UDP/accept operations.
    io.restart(); io.run_for(5s);
    if (!io.stopped()) {
        std::fputs("Fatal: network cancellation completion deadline exceeded\n",stderr);
        std::_Exit(1); // Keep buffers alive until the OS cleans up this owned process.
    }
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
        {"socketSendBufferRequestedBytes",16384},{"socketSendBufferObservedBytes",socket_send_buffer ? protocol::Json(*socket_send_buffer) : protocol::Json(nullptr)},
        {"discovery","unavailable"},{"remoteAuthorization","unavailable"}};
}
protocol::Json OscQueryServer::Impl::editor_bootstrap() const {
    const auto origin = "http://127.0.0.1:"+std::to_string(host.websocket_port);
    return {{"url",origin+"/editor/#token="+capability},{"websocket", "ws://127.0.0.1:"+std::to_string(host.websocket_port)+"/control"},
        {"capability",capability},{"staticAssets",!assets.empty()},{"developmentOrigin",options.development_origin.empty() ? protocol::Json(nullptr) : protocol::Json(options.development_origin)}};
}
OscQueryServer::OscQueryServer(protocol::ControlService& service,Options options) : impl_(std::make_unique<Impl>(service,std::move(options))) {}
OscQueryServer::~OscQueryServer() = default;
void OscQueryServer::poll() {
    impl_->refresh(); impl_->io.restart();
    for (unsigned count = 0; count < 64 && impl_->io.poll_one(); ++count) {}
    impl_->refresh();
    for (const auto& peer : impl_->peers) peer->events();
    std::erase_if(impl_->peers,[](const auto& peer){return peer->closed;});
}
protocol::Json OscQueryServer::endpoints() const {return impl_->endpoints();}
protocol::Json OscQueryServer::editor_bootstrap() const {return impl_->editor_bootstrap();}
}
