// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <mapgyver/control/project.hpp>
namespace mapgyver::assets {
struct ImageImport {domain::Id asset;std::vector<control::Command> commands;};
// Pure authored operation, with no file IO/probe. The image adapter prepares
// actual bytes asynchronously. Ambiguous root inputs require explicit routing.
[[nodiscard]] ImageImport import_image(const control::ProjectState& state,std::string locator,std::string name,
    bool default_fit = false,domain::InstancePool::IdGenerator ids = domain::Id::random);
}
