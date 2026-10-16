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

The optional decoder experiment adds FFmpeg 9.0.2#1 with avcodec/core only;
actual configured/runtime license is LGPL-2.1-or-later. GPL/nonfree and external
codec features are disabled. [Decoder inventory](decoder-dependencies.json)
records the six additional resolved packages and archive/notice hashes. pkgconf
has mixed per-file terms (including BSD-4-Clause and an Autoconf macro exception)
and remains a host build tool, never an app runtime. Meson's acquired tool uses
Apache-2.0; the installed vcpkg helper uses MIT. The test MPEG-2 media is generated
by newly authored code and its exact encoded bytes are hashed per run.

The M03 OSCQuery transport adds Boost.Beast/Asio 1.92.0 from the same pinned
baseline. [Network inventory](network-dependencies.json) retains the resolved
56-package Boost/build-helper closure, source archive hashes and installed notice
hashes (BSL-1.0 and MIT). Application transport uses asynchronous callbacks;
default transitive Asio features also install context/date-time. Resolved package
presence is not a binary linkage/distribution claim. M13 owns the actual binary
SBOM, notices and matching source/build materials. No proprietary runtime or
license exception is introduced.

The M03 static editor pins Svelte 5.57.2, Vite 8.3.4, TypeScript 6.0.2,
vite-plugin-svelte 7.3.1, svelte-check 4.7.6 and Playwright 1.64.1. The
[npm source inventory](editor-dependencies.json) retains lockfile integrity,
package license metadata, installed notice hashes and optional platform packages.
Uninstalled platform packages are not qualified by metadata. Runtime Svelte,
esm-env and clsx have inspected MIT notices copied beside the static editor;
the application GPL license is also copied. TypeScript/Playwright use Apache-2.0;
Lightning CSS and its native bindings use MPL-2.0 and are development build tools,
not application runtime dependencies. Other resolved licenses are MIT/ISC/BSD-3-Clause.
Node/npm, Chromium and Playwright's FFmpeg download remain development/test inputs,
never installed with the native application. This source inventory is not the M13
distributed binary/bundle SBOM or its matching source package.

M04 offline shader compilation enables the pinned bgfx host `tools` feature.
[Additional render-tool inventory](render-tools-dependencies.json) records six
resolved packages and source archive/installed notice hashes. Existing graphics
and network inventories remain applicable. glslang has mixed per-file terms,
including historical Bison skeleton and permissive NVIDIA preprocessor notices;
SPIR-V headers distinguish source/generator and CC-BY-4.0 documentation terms.
Full notices were inspected and remain required with any redistributed tools.
Only shaderc runs during the build; these tools are not installed app runtimes.
Authored application shaders use the pinned BSD bgfx shader include, with hashes
and actual compiler commands retained per build. No application license exception
or proprietary runtime was introduced. M13 still owns binary provenance/packaging.
