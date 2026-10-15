# Dependency provenance

New application code is GPL-3.0-or-later; see the root LICENSE. The SPDX inventory
records M00 source dependencies, not a release binary SBOM. No reference
application code or proprietary runtime has been copied.

nlohmann/json 3.12.0 is MIT licensed, resolved as vcpkg 3.12.0#2. Its exact
upstream archive SHA512 and port sources are in the pinned vcpkg registry.
Installed upstream notices are retained under
`out/build/<preset>/vcpkg_installed/<triplet>/share/<package>/copyright`.
The vcpkg-cmake and vcpkg-cmake-config MIT build helpers use the same pinned
registry. The inventory lists them separately as build dependencies.

Compiler/SDK, CMake, Python, Ninja and Git are prerequisites, not bundled app
runtimes. Record their actual versions per build. M13 adds a resolved binary
inventory and includes notices and matching source/build materials.

NDI integration/distribution remains blocked by ADR-011. This inventory does
not authorize a license exception or bundling proprietary runtimes.
