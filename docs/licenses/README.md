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

The optional M01 graphics experiment adds pinned bgfx/SDL3 and their resolved
source dependencies. [Graphics inventory](graphics-dependencies.json) records
versions, port revisions, license metadata and installed notice hashes.
Two ports omit SPDX metadata: lodepng's installed zlib notice was reviewed;
stb offers MIT/public-domain alternatives and the MIT option is selected.
No proprietary SDK is added. bgfx's pinned source archive also contains bx,
bimg and bundled component notices; retain the applicable full source/notices
when producing a release rather than treating its top-level license as an SBOM.
The copied example shader binaries have their own provenance and BSD notice
under experiments/graphics/shaders. They are experimental assets; their
historical compiler build is not claimed as reproduced.

The optional Spout experiment adds BSD-2-Clause SpoutDX 2.007.010#1 and an
independent official 2.007.018 SDK fixture. [Spout inventory](spout-dependencies.json)
records these inputs and the four additional resolved ports. Spout's port omits
SPDX metadata; its installed BSD-2-Clause notice was inspected. Khronos headers
use MIT notices and registry materials use Apache-2.0. The opengl port copies
Windows SDK headers/import libraries for local compilation; these are SDK
prerequisites, not an authorization to distribute SDK files. No proprietary
runtime is committed or bundled. The fixture manifest pins the official SDK
archive and source license hashes; retain that notice with any fixture package.
