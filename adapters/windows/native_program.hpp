// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mapgyver/protocol/service.hpp>
#include <memory>
namespace mapgyver::windows {
struct ProgramOptions {
    bool hidden = false;
    std::uint64_t frame_limit = 0; // Explicit bounded qualification run; zero runs continuously.
    std::string report; // Offline captures after that bounded run, never in frame().
    std::string media_root{}; // Relative image locators resolve here; no SDK type.
};
// Control owner calls poll(); the native owner runs without UI/control callbacks.
class NativeProgram {
public:
    NativeProgram(protocol::ControlService& service,ProgramOptions options = {});
    ~NativeProgram();
    NativeProgram(const NativeProgram&) = delete;
    NativeProgram& operator=(const NativeProgram&) = delete;
    // True after a bounded qualification run finishes, permitting normal shutdown.
    [[nodiscard]] bool poll();
    void finish();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
