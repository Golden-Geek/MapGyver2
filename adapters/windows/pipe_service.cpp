// SPDX-License-Identifier: GPL-3.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#include "pipe_service.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace mapgyver::windows {
namespace {
using protocol::Json;
using Clock = std::chrono::steady_clock;
constexpr std::size_t peer_count = 16, max_request = 1048576, max_response = 4194304;
constexpr auto frame_timeout = std::chrono::seconds{5}, write_timeout = std::chrono::seconds{2};
std::atomic_bool stopping{false};
BOOL WINAPI console_stop(DWORD reason) {
    if (reason == CTRL_C_EVENT || reason == CTRL_BREAK_EVENT || reason == CTRL_CLOSE_EVENT || reason == CTRL_SHUTDOWN_EVENT) {
        stopping.store(true); return TRUE;
    }
    return FALSE;
}
[[noreturn]] void fail(const char* operation,DWORD error = GetLastError()) {
    throw std::runtime_error(std::string{operation}+" failed, Win32="+std::to_string(error));
}
class Handle {
public:
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) : value_(value) {}
    ~Handle() {if (value_ != INVALID_HANDLE_VALUE && value_ != nullptr) CloseHandle(value_);}
    Handle(const Handle&) = delete; Handle& operator=(const Handle&) = delete;
    void reset(HANDLE value) {if (value_ != INVALID_HANDLE_VALUE && value_ != nullptr) CloseHandle(value_); value_ = value;}
    HANDLE get() const {return value_;}
private:
    HANDLE value_;
};
class Security {
public:
    Security() {
        HANDLE raw{};
        if (!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&raw)) fail("OpenProcessToken");
        Handle token{raw}; DWORD bytes = 0;
        if (GetTokenInformation(token.get(),TokenUser,nullptr,0,&bytes) || GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes > 65536)
            fail("GetTokenInformation size");
        std::vector<std::byte> storage(bytes);
        if (!GetTokenInformation(token.get(),TokenUser,storage.data(),bytes,&bytes)) fail("GetTokenInformation");
        EXPLICIT_ACCESSW access{}; access.grfAccessPermissions = GENERIC_ALL; access.grfAccessMode = SET_ACCESS;
        access.grfInheritance = NO_INHERITANCE; access.Trustee.TrusteeForm = TRUSTEE_IS_SID; access.Trustee.TrusteeType = TRUSTEE_IS_USER;
        access.Trustee.ptstrName = static_cast<LPWSTR>(reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid);
        const auto result = SetEntriesInAclW(1,&access,nullptr,&acl_);
        if (result != ERROR_SUCCESS) fail("SetEntriesInAcl",result);
        if (!InitializeSecurityDescriptor(&descriptor_,SECURITY_DESCRIPTOR_REVISION) || !SetSecurityDescriptorDacl(&descriptor_,TRUE,acl_,FALSE) ||
            !SetSecurityDescriptorControl(&descriptor_,SE_DACL_PROTECTED,SE_DACL_PROTECTED)) {
            const auto error = GetLastError(); LocalFree(acl_); acl_ = nullptr; fail("SetSecurityDescriptorDacl",error);
        }
        attributes_ = {sizeof(SECURITY_ATTRIBUTES),&descriptor_,FALSE};
    }
    ~Security() {if (acl_) LocalFree(acl_);}
    Security(const Security&) = delete; Security& operator=(const Security&) = delete;
    SECURITY_ATTRIBUTES* get() {return &attributes_;}
private:
    PACL acl_ = nullptr; SECURITY_DESCRIPTOR descriptor_{}; SECURITY_ATTRIBUTES attributes_{};
};
struct Operation {
    Handle event{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    OVERLAPPED overlapped{};
    bool pending = false;
    Operation() {if (!event.get()) fail("CreateEvent"); overlapped.hEvent = event.get();}
    void reset() {
        if (pending) throw std::logic_error("Reusing pending pipe IO");
        if (!ResetEvent(event.get())) fail("ResetEvent");
        overlapped = {}; overlapped.hEvent = event.get();
    }
};
bool disconnected(DWORD error) {
    return error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA || error == ERROR_PIPE_NOT_CONNECTED || error == ERROR_OPERATION_ABORTED;
}
struct Peer {
    Handle pipe; Operation reader,writer;
    bool connecting = true, closing = false, close_after_write = false, have_request = false;
    bool service_disconnected = false;
    std::optional<domain::Id> session;
    std::uint64_t cursor = 0;
    std::array<char,4> header{};
    std::size_t header_used = 0, body_used = 0, output_used = 0;
    std::string body, output;
    std::optional<std::uint64_t> output_cursor;
    std::optional<Clock::time_point> read_deadline,write_deadline,cancel_deadline;
    Peer(const std::wstring& endpoint,Security& security,bool first) {
        const auto value = CreateNamedPipeW(endpoint.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|(first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
            PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,static_cast<DWORD>(peer_count),65536,65536,5000,security.get());
        if (value == INVALID_HANDLE_VALUE) fail("CreateNamedPipe"); pipe.reset(value);
        reader.reset();
        if (ConnectNamedPipe(pipe.get(),&reader.overlapped)) connected();
        else {
            const auto error = GetLastError();
            if (error == ERROR_IO_PENDING) reader.pending = true;
            else if (error == ERROR_PIPE_CONNECTED) connected();
            else fail("ConnectNamedPipe",error);
        }
    }
    ~Peer() {
        // CancelIoEx only requests cancellation. Keep OVERLAPPED and every buffer
        // alive until completion; a failed OS cancellation is fatal to this
        // engine process rather than freeing kernel-owned memory underneath it.
        cancel();
        const auto deadline = Clock::now()+std::chrono::seconds{5};
        while (reader.pending || writer.pending) {
            drain(reader); drain(writer);
            if (Clock::now() >= deadline) {std::fputs("Fatal: named-pipe IO cancellation did not complete\n",stderr); std::_Exit(1);}
            Sleep(1);
        }
    }
    void connected() {connecting = false; read_deadline = Clock::now()+frame_timeout;}
    void cancel() noexcept {
        if (pipe.get() != INVALID_HANDLE_VALUE && (reader.pending || writer.pending)) {
            if (!CancelIoEx(pipe.get(),nullptr) && GetLastError() != ERROR_NOT_FOUND) {
                std::fputs("Fatal: CancelIoEx failed for owned named-pipe IO\n",stderr); std::_Exit(1);
            }
        }
    }
    void drain(Operation& operation) noexcept {
        if (!operation.pending) return;
        const auto signaled = WaitForSingleObject(operation.event.get(),0);
        if (signaled == WAIT_TIMEOUT) return;
        if (signaled != WAIT_OBJECT_0) {std::fputs("Fatal: owned pipe completion event failed\n",stderr); std::_Exit(1);}
        DWORD bytes = 0;
        if (GetOverlappedResult(pipe.get(),&operation.overlapped,&bytes,FALSE)) operation.pending = false;
        else {
            const auto error = GetLastError();
            if (disconnected(error)) operation.pending = false;
            else if (error != ERROR_IO_INCOMPLETE) {std::fputs("Fatal: unexpected owned pipe cancellation result\n",stderr); std::_Exit(1);}
        }
    }
    void close(protocol::ControlService& service,const char* reason) {
        if (closing) return;
        closing = true; cancel_deadline = Clock::now()+std::chrono::seconds{5}; cancel();
        if (session && !service_disconnected) {service.disconnect(*session); service_disconnected = true;}
        std::fprintf(stderr,"%s\n",Json{{"event","control_peer_closed"},{"reason",reason}}.dump().c_str());
    }
    std::optional<DWORD> complete(Operation& operation) {
        DWORD bytes = 0;
        if (GetOverlappedResult(pipe.get(),&operation.overlapped,&bytes,FALSE)) {operation.pending = false; return bytes;}
        const auto error = GetLastError();
        if (error == ERROR_IO_INCOMPLETE) return std::nullopt;
        if (disconnected(error)) {operation.pending = false; return DWORD{0};}
        fail("GetOverlappedResult",error);
    }
    std::optional<DWORD> read(char* target,std::size_t size) {
        if (reader.pending) return complete(reader);
        reader.reset(); DWORD bytes = 0;
        if (ReadFile(pipe.get(),target,static_cast<DWORD>(size),&bytes,&reader.overlapped)) return bytes;
        const auto error = GetLastError();
        if (error == ERROR_IO_PENDING) {reader.pending = true; return std::nullopt;}
        if (disconnected(error)) return DWORD{0}; fail("ReadFile",error);
    }
    void enqueue(const Json& value,std::optional<std::uint64_t> delivered_cursor = std::nullopt) {
        if (!output.empty()) throw std::logic_error("Overwriting an unsent pipe response");
        const auto json = value.dump();
        if (json.size() > max_response) throw std::runtime_error("Pipe response capacity exceeded");
        output.resize(json.size()+4);
        const auto length = static_cast<std::uint32_t>(json.size());
        for (unsigned index = 0; index < 4; ++index) output[index] = static_cast<char>((length>>(index*8))&255U);
        std::copy(json.begin(),json.end(),output.begin()+4); output_used = 0; output_cursor = delivered_cursor;
        write_deadline = Clock::now()+write_timeout;
    }
    void send(protocol::ControlService& service) {
        if (output.empty()) return;
        if (Clock::now() >= *write_deadline) {close(service,"write_timeout"); return;}
        std::optional<DWORD> bytes;
        if (writer.pending) bytes = complete(writer);
        else {
            writer.reset(); DWORD count = 0;
            const auto size = static_cast<DWORD>(std::min<std::size_t>(65536,output.size()-output_used));
            if (WriteFile(pipe.get(),output.data()+output_used,size,&count,&writer.overlapped)) bytes = count;
            else {
                const auto error = GetLastError();
                if (error == ERROR_IO_PENDING) writer.pending = true;
                else if (disconnected(error)) bytes = DWORD{0};
                else fail("WriteFile",error);
            }
        }
        if (!bytes) return;
        if (*bytes == 0) {close(service,"write_disconnected"); return;}
        output_used += *bytes;
        if (output_used == output.size()) {
            output.clear(); output_used = 0; write_deadline.reset();
            if (output_cursor) {cursor = *output_cursor; output_cursor.reset();}
            if (close_after_write) close(service,"hello_rejected");
        }
    }
    void receive(protocol::ControlService& service) {
        if (have_request || (!output.empty() && !reader.pending)) return;
        for (int step = 0; step < 16 && !have_request; ++step) {
            const bool reading_header = header_used < header.size();
            const auto bytes = reading_header ? read(header.data()+header_used,header.size()-header_used) : read(body.data()+body_used,body.size()-body_used);
            if (!bytes) return;
            if (*bytes == 0) {close(service,"read_disconnected"); return;}
            if (!read_deadline) read_deadline = Clock::now()+frame_timeout;
            if (reading_header) {
                header_used += *bytes;
                if (header_used == header.size()) {
                    std::uint32_t length = 0;
                    for (unsigned index = 0; index < 4; ++index) length |= static_cast<std::uint32_t>(static_cast<unsigned char>(header[index]))<<(index*8);
                    if (length == 0 || length > max_request) {close(service,"invalid_frame_length"); return;}
                    body.resize(length); body_used = 0;
                }
            } else {
                body_used += *bytes;
                if (body_used == body.size()) {have_request = true; read_deadline.reset();}
            }
        }
    }
    void process(protocol::ControlService& service) {
        if (!have_request || !output.empty()) return;
        if (!session) {
            std::optional<protocol::Connection> connection;
            const auto reject_hello = [&](const std::exception& error) {
                enqueue({{"protocolVersion",1},{"type","hello_error"},{"error",{{"code","hello_rejected"},{"message",std::string{error.what()}.substr(0,512)}}}});
                close_after_write = true;
            };
            try {
                connection = service.connect(body);
            }
            catch (const protocol::ServiceError& error) {reject_hello(error);}
            catch (const control::wire::Error& error) {reject_hello(error);}
            catch (const domain::ModelError& error) {reject_hello(error);}
            if (connection) {
                session = std::move(connection->session);
                cursor = control::wire::read_unsigned(connection->hello.at("snapshot").at("eventSequence")); enqueue(connection->hello);
            }
        } else {
            const auto reply = service.handle(*session,body);
            std::optional<std::uint64_t> next_cursor;
            if (reply.at("status") == "accepted" && reply.at("payload").is_object() && reply.at("payload").value("type","") == "events")
                next_cursor = control::wire::read_unsigned(reply.at("payload").at("cursor"));
            enqueue(reply,next_cursor);
        }
        have_request = false; header_used = 0; body_used = 0; body.clear();
    }
    bool tick(protocol::ControlService& service) {
        if (closing) {
            drain(reader); drain(writer);
            if (reader.pending || writer.pending) {
                if (Clock::now() >= *cancel_deadline) {std::fputs("Fatal: named-pipe cancellation deadline exceeded\n",stderr); std::_Exit(1);}
                return false;
            }
            return true;
        }
        if (connecting) {
            const auto result = complete(reader);
            if (!result) return false;
            // A completed connect transfers no data; it is not a zero-byte read.
            connected();
        }
        if (read_deadline && Clock::now() >= *read_deadline) {close(service,"frame_timeout"); return false;}
        send(service); if (closing) return false;
        receive(service); if (closing) return false;
        process(service);
        if (session && output.empty() && !have_request && header_used == 0) {
            if (auto notification = service.notifications(*session,cursor))
                enqueue({{"protocolVersion",1},{"type","notification"},{"payload",*notification}},control::wire::read_unsigned(notification->at("cursor")));
        }
        return false;
    }
};
}
void serve_named_pipe(protocol::ControlService& service,std::string_view name,const std::function<void(const std::string&)>& ready,const std::function<void()>& poll) {
    static_cast<void>(domain::Id{std::string{name}});
    const std::string endpoint = "\\\\.\\pipe\\MapGyver."+std::string{name};
    const std::wstring wide(endpoint.begin(),endpoint.end()); Security security;
    std::array<std::unique_ptr<Peer>,peer_count> peers;
    for (std::size_t index = 0; index < peers.size(); ++index) peers[index] = std::make_unique<Peer>(wide,security,index == 0);
    stopping.store(false);
    const bool console = GetConsoleWindow() != nullptr;
    if (console && !SetConsoleCtrlHandler(console_stop,TRUE)) fail("SetConsoleCtrlHandler");
    struct Handler {bool enabled; ~Handler() {if (enabled) SetConsoleCtrlHandler(console_stop,FALSE);}} handler{console};
    ready(endpoint);
    while (!stopping.load()) {
        if (poll) poll();
        for (auto& peer : peers) if (peer->tick(service)) {
            peer.reset(); peer = std::make_unique<Peer>(wide,security,false);
        }
        Sleep(5);
    }
    for (auto& peer : peers) peer->close(service,"server_stopping");
}
protocol::Json pipe_capabilities() {
    return {{"connections",peer_count},{"requestBytes",max_request},{"responseBytes",max_response},
        {"frameTimeoutMs",std::chrono::duration_cast<std::chrono::milliseconds>(frame_timeout).count()},
        {"writeTimeoutMs",std::chrono::duration_cast<std::chrono::milliseconds>(write_timeout).count()},
        {"remoteClients","rejected_by_pipe_flag"},{"access","current_user_acl"}};
}
} // namespace mapgyver::windows
