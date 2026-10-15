# Active execution plan — MapGyver Windows beta

## Scope and baseline

- Authorized: M00–M13; MapGyver; public Golden-Geek/MapGyver2; tested local
  commits pushed directly to main. No public release or policy changes.
- Planning baseline: `1d686c1`; no previous application or Git history.
  Planning validation passed before the baseline commit and public push.
- Follow backlog dependencies and preserve slice IDs. Current: M01.C01 bgfx
  owned/shared texture sampling, lifetime retirement and producer-timeout tests.

## Steps

1. M00.C01 complete locally: pinned C++20 build/core/manifests/SPDX provenance;
   Debug and Release build and executable smoke passed. Hash recorded next slice.
2. M00.C02 complete locally: diagnostic schema/CLI and four meaningful CTest
   contracts pass in Debug and Release; source identity, invalid commands,
   capability reasons and negative feature configuration are covered.
3. M00.C03 implemented and locally tested: Windows Debug/Release and Linux/macOS portable CI,
   fail-closed quality gate, logs and exact environment/source identity.
   Replacement run 38770417178 passed every required hosted job.
4. M00.C04 complete: fresh-checkout Debug/Release, bootstrap guide and durable
   evidence. Final `93be949` CI 38771335741 passed every job; annotated
   milestone/m00 tag created and pushed at that green commit.
5. M01.C01 in progress: native D3D11 baseline then actual bgfx imports. Renderer provisional
   until critical tests pass; no speculative backends/plugin framework.
   Baseline partial C01 committed `e1bbfff7911e6abf7bd63284443995ae4a09382e`.
   CI 38773362603 passed, including native experiment compilation. Clean-source
   RTX 5080 Release run passed six raw-pixel cases; retained report is
   docs/progress/M01-d3d11-baseline.json. Next: actual bgfx sampling/import path;
   C01 and M01 remain incomplete. Pinned bgfx archive/ownership findings are in
   docs/progress/M01.md; do not substitute latest docs for pinned API behavior.
   Working-tree bgfx Release probe now passes 12 exact pixel/alpha cases and a
   real keyed-mutex producer stall returns WAIT_TIMEOUT with no success payload.
   Debug and Release hardware contracts pass after removing an invalid renderFrame
   call in the single-threaded bgfx build. Both non-hardware wrappers pass; shader
   provenance and 11 resolved dependency notices are recorded. Next: commit/push,
   clean-source GPU reruns and hosted compilation. M01.C02 Spout follows.
6. M02–M13 pending: follow contracts and continue independent work around
   unavailable hardware/legal evidence without qualifying dependents.

## Validation

- `python tools/validate_plan.py`: passed at baseline (20 milestones / 80 slices /
  34 scenarios). This validates the planning contract, not application behavior.
- M00.C01: `90faf2a`, pushed; local Debug/Release build and banner smoke passed.
- M00.C02: `b47656a`, pushed; all four CTest contracts passed in both configurations.
- M00.C03 `d4d3367`: run 38769687062 passed Linux/macOS but failed Windows:
  windows-2025 had VS2026, no VS2022. Quality gate correctly failed. ADR-018
  switches to windows-2022; replacement run must pass before M00 completion.
- C03 correction `4fcc2012f06b6b079ea4fd7075a07f686bc75d69`: CI 38770417178
  passed all jobs, 5/5 contracts in each of four builds. Durable summary in
  docs/progress/M00-ci-evidence.json; exact commands/results in docs/progress/M00.md.
- Fresh public checkout at d4d3367 passed Release without binary-cache reuse
  and Debug, both source/diagnostics dirty=false. No native GPU tests claimed.
- Local M00: `python tools/ci/bootstrap.py --triplet x64-windows`,
  `cmake --preset windows-local`, build Debug/Release, CTest for each with
  `--output-on-failure --no-tests=error`.
- Hosted: windows-ci on windows-2022 / VS2022; portable-ci on native Linux x64
  and macOS ARM64. Record actual source hashes and run URLs in milestone evidence.

## Decisions and open gates

- Local VS2026 uses v145; CI VS2022 uses v143. Initial local v143 configuration
  failed MSB8020: the default BuildTools instance has no v143 integration, even
  though a compiler exists in the separate Community instance. Use native v145
  instead of installing another toolchain or silently changing the CI contract.
- vcpkg: 20ceb8adbf0504d4b0d5e4058a32f9d4fc896391; nlohmann-json 3.12.0#2.
- Refresh source identity every build. Archives without Git report unknown
  identity; no invented clean revision.
- Laptop only: Windows 11 x64, Intel graphics and RTX 5080 Laptop GPU.
  Two physical outputs/projectors and hotplug qualification remain open.
- NDI distribution path remains unresolved (ADR-011); mandatory gaps block M13.
