# Active execution plan - MapGyver Windows beta

## Authorized scope and qualification boundaries

- Implement M00-M13; product MapGyver; public Golden-Geek/MapGyver2. Create tested
  local Conventional Commits and push directly to main. No release, force push,
  history rewrite or remote policy changes. Only laptop hardware is available.
- Follow docs/backlog.json IDs/dependencies and project build/test wrappers.
  Milestone completion requires its actual evidence, not documentation alone.
- M00 complete at 93be949919b6aedeccda365087f7b6b1781ce27f; CI 38771335741 green;
  milestone/m00 points there. M01/M02/M03 are in progress; M04-M19 remain planned.
- M01 is unqualified: missing physical two-output/scanout/hotplug, device/driver
  loss recovery and sustained reference performance evidence. Renderer remains
  provisional; do not tag M01 or complete dependent milestones from API tests.
- NDI legal/distribution gate ADR-011 and reference workload budgets remain open.
  Unsupported features retain configuration/reasons. No proprietary runtime or
  license exception is authorized. Continue independent implementation.

## Implemented baseline and evidence

- M01.C01/C02/C03: real native bgfx/D3D11 ownership and sampling, cross-version
  Spout, strict D3D11VA MPEG-2/NV12 GPU conversion, owned worker failures,
  coherent frame sets and hidden multi-window lifecycle. Actual source/SDK/GPU
  identities, Debug/Release and hosted results are in docs/progress/M01.md and
  referenced JSON reports. Hidden/API evidence does not qualify physical output.
- M02.C01/C02: portable definitions, scoped instance ownership/sharing/transport,
  arbitrary stage/surface/layout/output/patch models, stable geometry IDs and
  calibration separation. C03/C04 add atomic transactions/undo, one typed registry,
  bounded exact JSON/schema export and validated logical restoration. Full commit
  and evidence trail is in docs/progress/M02.md. No native readiness/durable save.
- M03.C01: authoritative reliable session/revision/ownership core, terminal reply
  cache/sequence watermark, reconnect/full-snapshot resync and bounded journal.
  Windows engine --serve uses current-user ACL protected named-pipe IO with
  framing, deadlines and completion-owned cancellation. Code 551c322 and evidence
  a3a8ca9 have green CI 38824975794. Independent native clients exercise 12 phases.
- M03.C02 portable adapter: bounded OSC codec, immutable complete registry-backed
  OSCQuery tree, exact LISTEN/IGNORE and validated read-only-by-default ingress.
  Code 0680d44dbb62e6e03e5f2416d1afe445d9b14ff4, evidence e07a8a4. CI 38829637884
  fully green: Windows 15 contracts; Linux/macOS 14. ADR-019 records replacement
  rationale and primary spec/source review.
- Current socket code: 5ca43b36600ac60dadc74b13d2824a71103f377d. Optional --oscquery
  binds real loopback HTTP/UDP/WebSocket endpoints with Boost.Beast/Asio 1.92.0.
  One control owner polls bounded async completions; Host/Origin policy and
  explicit pipe ownership transfer gate the shared external OSC channel. This
  grant covers all local OSC peers, not individually authenticated clients.
- Socket slice clean Debug/Release wrappers each pass 17/17 at that exact SHA,
  dirty=false. Seven portable-fixture socket phases, eight production-engine
  socket phases, twelve real pipe regressions. Actual kernel stalls/deadlines,
  connection-reset/EOF classes, peer/message limits, atomic validation, exact
  subscriptions/reconnect, structural updates and capacity-isolated notifications
  are retained in docs/progress/M03-C02-network-local.json. Valid model text above
  OSC string capacity closes only its subscriber; HTTP/model/healthy IO continue.
- Boost/build-helper 56-package source/archive/notice closure is retained in
  docs/licenses/network-dependencies.json. This is not a release binary SBOM.

## Current slice status and next work

1. M03.C02 portable codec/projection: implemented/tested/hosted green.
2. M03.C02 actual native sockets: implemented/tested clean locally; code and
   evidence prepared for push. Collect its own hosted Windows/Linux/macOS results.
3. M03.C03 next: generated TypeScript contracts, authenticated reliable browser
   WebSocket channel sharing ControlService, packaged Svelte/TypeScript/Vite shell
   showing actual state/ownership/reconnect, static serving and preview contract.
   Keep development Vite separate and production assets static. Preview must report
   unavailable until an actual native renderer/encoder supplies it; no fake frame.
4. M03.C04: extend actual remote parity/retry/reconnect/traffic tests through the
   browser channel. Durable save needs M12; rendered applied-frame outcomes need
   runtime integration. Unsupported operations must reject with a reason.
5. Discovery/authenticated remote access, UI, encoded preview/rendered application,
   durable saves and prerequisite qualification remain open. No M01/M02/M03 tag.

## Validation and continuation

- python tools/ci/check.py --preset windows-local --configuration Debug --skip-bootstrap
- python tools/ci/check.py --preset windows-local --configuration Release --skip-bootstrap
- python tools/validate_plan.py
- Use focused docs/STATUS.md, docs/DECISIONS.md, requested milestone/contracts/tests
  and python tools/show_milestone.py Mxx. Do not reload the whole planning pack.
- Exact environments/commands/source SHAs/JUnit and known gaps are in milestone
  progress JSON/Markdown. Keep clean-source evidence tied to its code SHA; never
  write an invented/self-referential hash. Preserve running source CI when routine
  docs can wait for the next push. No routine approval checkpoint is required.
