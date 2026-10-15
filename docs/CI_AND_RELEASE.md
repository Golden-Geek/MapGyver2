# CI and release implementation

Windows x64 is the first qualified delivery. Hosted CI prevents build/domain/protocol/UI regressions; a controlled hardware lab establishes GPU/media/output/recovery/performance evidence. Those are distinct gates.

## Staged activation

| Phase | Checks that must exist | Activation |
| --- | --- | --- |
| Planning pack | Backlog/dependency/coverage/link validator | Included `plan.yml` after this pack is installed in a repository |
| M00 | Windows x64 Debug/Release; Linux/macOS portable core; stable quality-gate | Adapt `ci.yml.in` once their build contracts exist |
| M01 | Native GPU/Spout/presentation experiments and environment reports | Restricted manual hardware workflow plus recorded lab runs |
| M03 | UI types/lint/build, protocol tests and remote reconnect smoke | Adapt `web.yml.in`; add its check to required repository policy |
| M04-M11 | Render references, media corpus, cue/app/integration/mapping tests | Extend real targets; hosted tests exclude separately labeled hardware cases |
| M12 | Recovery scenarios, long-run resource checks and 72-hour qualification | Dedicated lab execution with exact source/profile/media identity |
| M13 | Clean-machine package smoke, source/license materials and qualified candidate | Adapt `release.yml.in`; publishing is a separate deliberate action |

Template activation is recorded in `ci/activation.json`. The templates are inactive by filename and are not working application CI in this handoff. M00 must create the referenced CMake presets and bootstrap scripts; M03 must create the UI package/lock/version file/scripts; lab/release scripts arrive with their milestones. Never make absent commands pass with an empty placeholder.

## Initial job topology

`Plan / validate` checks planning consistency. `CI / windows-native` builds MSVC x64 in Debug and Release and runs non-hardware CTest with `--no-tests=error`. `CI / quality-gate` always evaluates its plan, native and portable prerequisite results and fails unless every required result succeeded. `CI / portable-core` builds/runs the shared core on Ubuntu 24.04 x64 and macOS 15 ARM64, with platform and media adapters disabled explicitly. M03 adds `Web UI / ui` with exact lockfile install, type checking, lint, tests, build and selected Playwright journeys.

Configure repository rules to require stable job names after the first real successful runs; matrix checks have explicit configurations. Keep at least one always-running aggregate gate. Avoid whole-workflow path filters that leave required checks pending. Conditional jobs must feed the aggregate honestly: skipped/cancelled/failed is not success unless the gate explicitly documents why the check is inapplicable.

Do not set blanket `continue-on-error` or silently skip all tests. Test quarantine needs an issue/reason, bounded scope and a replacement qualification requirement. Core build checks must compile the real domain/control/persistence code. A missing dependency cannot silently switch a feature off in a job intended to test it.

## Reproducible build contract

M00 implements `python tools/ci/bootstrap.py --triplet <triplet> [--core-only]`, a pinned dependency root and CMake presets `windows-ci` and `portable-ci`. Windows uses the Visual Studio 2022 generator on `windows-2025`; the actual image currently supplies VS 2022 17.14. Record exact compiler/SDK/CMake versions per run. Portable core uses Ninja and an explicitly selected current compiler supporting the project's C++20 subset.

The project owns `PM_BUILD_PLATFORM`, `PM_BUILD_MEDIA`, `PM_BUILD_UI`, `PM_BUILD_HARDWARE_TESTS` and `BUILD_TESTING` options. Unknown/missing mandatory feature selections fail configuration rather than appearing as unused CMake variables. Presets use documented output paths. Dependencies resolve through a committed vcpkg baseline/feature manifest or a comparably reproducible alternative documented in M00. Do not pin a made-up vcpkg commit in this planning pack; choose and verify the actual dependency set during bootstrap.

M03 adds `ui/.node-version` with a selected exact supported Node version, `package-lock.json` and scripts `check`, `lint`, `test:ci`, `build`, `test:e2e:ci`. Production assets are static; Node is not required on the show computer. Lock any Playwright/browser revisions used in reports.

Action revisions in templates were resolved against official GitHub repositories on 2026-10-15:

| Action | Version | Full commit |
| --- | --- | --- |
| actions/checkout | v7.0.1 | `3d3c42e5aac5ba805825da76410c181273ba90b1` |
| actions/setup-node | v7.1.0 | `949feb2413d6458794dcd2491c4babbbce0c15c1` |
| actions/upload-artifact | v7.0.2 | `cf430e030ddbb5b0abf93d22962f4752f3646cd9` |

These action versions use Node 24 internally, independently of the UI's selected Node version. Re-check runner compatibility before activation, particularly for self-hosted runners. Full action SHAs stabilize action code; hosted image contents still change and must be recorded. Use reviewed dependency-update changes to advance pins.

## Hosted correctness versus real hardware

Hosted Windows can build native code and may run software/WARP render references. It cannot certify real projector hotplug, external Spout interoperability, physical synchronization, hardware decode throughput or GPU memory behavior. Label every report: hosted compile, portable test, software reference, native GPU lab, physical output, emulated architecture or long-run qualification.

Hardware evidence includes tested source SHA, configuration/feature flags, device and driver IDs, OS/build, adapter versions, display/EDID topology, media hashes, warmup, clock settings, metric definitions, resource budgets and logs. A synthetic failure injection does not replace the corresponding physical event when physical behavior is the acceptance target.

For a public GPL repository, never execute arbitrary fork/PR code on a persistent workstation connected to the installation network. The manual hardware template runs only the trusted default branch with a protected hardware environment, no broad secrets and an isolated disposable/recoverable lab account/runner. Do not use `pull_request_target` to fetch and execute untrusted changes. Trusted-main hardware execution still needs normal maintenance and isolation.

The sample hardware workflow runs bounded interop/recovery/capacity suites. A 72-hour soak uses a dedicated lab procedure/scheduler respecting the actual CI job-duration limits; publish its exact manifest/results into the qualification evidence store. Do not hide a long run behind a hosted-job timeout or reuse a report from another SHA.

## Caches artifacts and failure diagnosis

Key dependency caches by OS/architecture/compiler/runtime linkage, baseline and feature manifest. Key UI caches by exact Node and lockfile. Treat caches as optional accelerators; a cold build must work. Do not restore ABI-incompatible binaries across architectures or unreviewed fork caches into trusted release jobs.

Upload JUnit/CTest output, configure/build diagnostics and small environment reports on failure as well as success. Keep performance traces and full dumps opt-in with size limits; avoid accidentally collecting private media or credentials. Version benchmark reports and retain enough artifacts to compare regressions. Actions artifacts are working evidence, not durable public releases.

Static analysis/sanitizers are added to targets where they work: clang-tidy on owned code, ASan on suitable Windows/Linux tests, UBSan/TSan on supported isolated portable components and protocol/media fuzzing with bounded corpora. Do not force unsupported sanitizer/SDK combinations or call a sanitizer-free compile a sanitizer pass.

## Commit and checkpoint policy

Use short-lived milestone branches when implementing in a shared repository. Each backlog slice is a coherent commit with relevant checks. If a slice grows, split it and map both commits to the same slice ID in evidence. Avoid unrelated refactors and giant end-of-milestone dumps.

A milestone report at `docs/progress/Mxx.md` records scope, source SHAs under test, commands/results, CI URLs, acceptance IDs, machine/media profiles, actual metrics and explicit open gaps. The final completion documentation commit is tagged only after its required checks are green. Do not invent tag names as existing artifacts or attempt to embed a commit's own future hash in that commit.

Protect main from direct unreviewed pushes and require the agreed checks once configured. Keep remote policy changes, pushes, merges and publication within the current implementation task's authorization. Local tested milestone commits are the normal implementation workflow.

## Windows candidate packaging and promotion

Build the candidate from an exact qualified revision. Include native binaries, static UI, documented approved dependencies, version/build info, dependency notices/SPDX inventory, corresponding-source/build materials, sample project and checksums. Document optional-runtime detection and installation. Do not bundle unapproved NDI components or unrelated installed codec packs.

Run a clean non-developer Windows installation, offline startup/playback, project open/save/upgrade, missing-media/runtime behavior, Unicode/long paths, app management, remote pairing and uninstall/update retention tests. A candidate with a different binary/dependency set needs matching qualification evidence.

The release template only builds/uploads candidate artifacts; it does not create a public release or push a tag. Signing/attestation/notarization and any credentialed publication get their own configured trusted process. Publish Windows support/performance statements only for the tested profiles and disclose remaining requirement gaps.

Later native ports add hosted native-architecture builds where supported and real hardware qualification. Current public runner labels include `windows-11-arm` and `ubuntu-24.04-arm`; verify the selected ARM Windows image's compiler generation before choosing a CMake generator, since it can differ from the x64 image.
