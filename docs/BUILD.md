# Building MapGyver

M00 builds the portable C++20 foundation and diagnostic executable. Rendering,
media playback and the editor arrive in later milestones. Use an x64 Windows
host for Windows builds; Linux x64 and macOS ARM64 qualify only the portable
core at this stage.

## Prerequisites

- Git, Python 3.11 or newer, and CMake 3.31 or newer.
- Windows CI: VS2022 17.14 / v143 C++ tools and a Windows SDK (tested
  10.0.26100.0). Local VS2026: v145 and CMake 4.2 or newer.
- Linux/macOS: a C++20 compiler and Ninja. The CI environment report identifies
  the actual compiler, OS, runner image and tools; native GPU ports are separate.
- Network access for first bootstrap. vcpkg verifies downloaded source archives
  and tools using its pinned metadata. No system codec pack is used.

## Windows

From the repository root:

```powershell
python tools/ci/check.py --preset windows-local --configuration Debug
python tools/ci/check.py --preset windows-local --configuration Release
out/build/windows-local/Release/mapgyver-engine.exe --version
out/build/windows-local/Release/mapgyver-engine.exe --diagnose --json --no-gpu
```

Use `windows-ci` instead of `windows-local` with VS2022. The local preset uses
VS2026's default matching installation, not an absolute machine-specific path.
It uses v145 because the initially selected BuildTools instance did not contain
v143 integration. If changing generators/toolsets, use a separate preset/build
directory or `cmake --fresh --preset <preset>`.

The wrapper performs the following real operations and stops at any failure:

```powershell
python tools/ci/bootstrap.py --triplet x64-windows
cmake --preset windows-local
cmake --build out/build/windows-local --config Release --parallel 2
ctest --test-dir out/build/windows-local -C Release --output-on-failure --no-tests=error
```

Bootstrap does not modify global tool installations. It checks the registry
origin and exact commit and refuses modified tracked dependency sources. CMake
then resolves the manifest at that baseline. `--skip-bootstrap` on the wrapper
reuses that root but still verifies its revision.

## Portable core

```sh
# Linux x64
python3 tools/ci/bootstrap.py --triplet x64-linux --core-only
# macOS ARM64: use --triplet arm64-osx instead
python3 tools/ci/check.py --preset portable-ci --configuration Release --skip-bootstrap
```

All M00 presets explicitly disable platform/media/UI/hardware/NDI features.
Requests to enable unimplemented features or unknown `PM_BUILD_*` options fail
configuration. No empty adapter or software-rendering result qualifies native
GPU behavior.

## Evidence and reproduction

The wrapper retains environment JSON, command logs, JUnit and diagnostic JSON
under `out/reports/<preset>/<configuration>`. CMake files and CTest logs remain
under `out/build/<preset>`. Reports include source SHA/dirty status, runner image,
compiler, SDK where applicable, registry baseline and resolved package versions.
CI uploads these on success and failure with 14-day retention. Milestone progress
documents retain source identities, run URLs, outcomes and explicit gaps.

To verify a fresh checkout, clone the public repository to a new directory and
run the wrapper there. Set `VCPKG_BINARY_SOURCES=clear` for that process to disable
vcpkg binary cache reuse. This does not erase caches. Source identity must match
the checkout and dirty=false; source archives report unknown provenance instead.
Generated/downloaded output stays under ignored `out/`.

M01 adds the optional `windows-graphics` preset and standalone native D3D11
baseline executable. Windows CI compiles it; real GPU execution is manual and
separately recorded. See [graphics experiments](../experiments/graphics/README.md).
The `windows-spout` profile also builds the bgfx/Spout participant and a separate
Release-only peer against pinned official SDK inputs. Windows CI compiles both
SDK versions; it does not execute the manual native GPU contract. The wrapper
downloads only checksum-verified fixture inputs under ignored out/qualification.
The `windows-decode` profile adds pinned FFmpeg avcodec/core and a strict native
D3D11VA decoder/import probe. Windows CI compiles it and checks its version CLI;
the real decode/GPU pixel and fallback-refusal contract remains manual.

Run `python tools/validate_plan.py` for document/backlog validation and
`python tools/show_milestone.py Mxx` for focused milestone contracts. These
commands do not qualify playback, GPU sharing, physical output or performance.
