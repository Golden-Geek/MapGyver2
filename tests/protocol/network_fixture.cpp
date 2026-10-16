// SPDX-License-Identifier: GPL-3.0-or-later
// Portable socket fixture, not a production authentication or ownership path.
#include "adapters/osc/network.hpp"
#include <mapgyver/build_info.hpp>
#include <atomic>
#include <iostream>
#include <thread>

int main() {
    using namespace mapgyver;
    using protocol::Json;
    try {
        protocol::ControlService service{control::ProjectController{domain::make_default_project()}};
        network::OscQueryServer server{service};
        const auto owner = service.connect(Json{{"protocolVersion",1},{"clientId","test-fixture"},{"resumeSessionId",nullptr},{"requestEdit",true}}.dump());
        const auto reply = service.handle(owner.session,Json{{"protocolVersion",1},{"serviceId",owner.hello.at("serviceId")},
            {"clientId","test-fixture"},{"sessionId",owner.session.str()},{"requestId","grant"},{"sequence","0"},
            {"expectedRevision",nullptr},{"applyAt",nullptr},{"operation","transferEdit"},
            {"payload",{{"expectedOwnershipGeneration",owner.hello.at("snapshot").at("ownershipGeneration")},
                {"targetSession",server.endpoints().at("oscSessionId")}}}}.dump());
        if (reply.at("status") != "accepted") throw std::runtime_error(reply.dump());
        const auto build = build_info();
        auto ready = server.endpoints(); ready["fixtureGrantsOsc"] = true;
        ready["project"] = control::wire::encode_project(*service.snapshot());
        ready["registry"] = control::wire::describe(service.registry());
        ready["sourceRevision"] = build.revision; ready["sourceDirty"] = build.dirty ? Json(*build.dirty) : Json(nullptr);
        std::cout << ready.dump() << '\n' << std::flush;
        std::atomic_bool stopped{false};
        // Parent closes stdin; only the main control thread touches the service.
        std::thread stop([&]{std::string line; while (std::getline(std::cin,line)) {} stopped.store(true);});
        while (!stopped.load()) {server.poll(); std::this_thread::sleep_for(std::chrono::milliseconds{2});}
        stop.join(); service.disconnect(owner.session);
        return 0;
    } catch (const std::exception& error) {std::cerr << error.what() << '\n'; return 1;}
}
