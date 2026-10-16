# Active execution plan - MapGyver Windows beta

## Authorized scope and qualification boundaries

- Implement M00-M13; product MapGyver; public Golden-Geek/MapGyver2. Create tested
  local Conventional Commits and push directly to main. No release, force push,
  history rewrite or remote policy changes. Only laptop hardware is available.
- Follow docs/backlog.json IDs/dependencies and project build/test wrappers.
  Milestone completion requires its actual evidence, not documentation alone.
- M00 complete at 93be949919b6aedeccda365087f7b6b1781ce27f; CI 38771335741 green;
  milestone/m00 points there. M01/M02/M03/M04 are in progress; M05-M19 remain planned.
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

M03.C03 code 6daac46016d3d11f01794a26e388e84bce867a4d and evidence
6e3ddc951f353bf278dd1c2281650c63176e538c are published. Clean local Debug/Release
21/21, native pipe/socket phases and three Chromium scenarios each passed.
Installed Release static assets serve with Node absent from the child PATH.
CI 38838422770 is fully green, attempt 2, on Windows/Linux/macOS. Its first
macOS job failed a pinned Boost download before compilation/tests; one terminal
failed-job retry passed. The bounded macOS socket saturation fix is now verified.
Encoded preview and durable saves remain open. The native applied-frame bridge
is recorded below.

M04.C01 current working slice:

1. Implemented: portable immutable active-video graph, dependency-first iterative
   compilation, scoped instances/shared output deduplication, inactive/unavailable
   culling with reasons, compatible lifetime target pool and explicit byte limits.
   Contracts cover 37 shared surfaces, independent scope, reordered old-plan
   immutability, known sRGB blends and 2,048-deep graph/two-target reuse.
2. Implemented: provisional Windows bgfx/D3D11 offscreen compositor with actual
   hardware identity/capacity checks and prepared authored shader binaries.
   Source uploads cache by resource/generation. Dense pass views support hundreds
   of independent leaves; empty branches share a transparent constant.
3. Code `c8a986054fca05f5328979d26a001e0e7817bf89`: clean windows-render Debug/Release wrappers
   each pass 22/22, dirty=false. Actual GPU runs each pass eight cases/160 frames/
   sixteen captures, maximum error 1/255 at fixed tolerance 2/255, normal exit.
   M04-C01-local.json retains source/binary/shader/compiler/environment/JUnit and
   GPU evidence. Release cleanup regression was fixed by dropping extra capture
   COM references before bgfx unloads D3D11; bgfx retains actual resource ownership.
4. Six additional offline bgfx tool dependencies/notices inspected and inventoried.
   No application exception/proprietary runtime. Source inventory is not M13 SBOM.
5. C01 code/evidence pushed at 0aa62c5547de512565a83f0aaa20c2c1f0da26bd.
   CI 38843868195 is terminal: Windows Debug/Linux/macOS/plan passed; Release
   expired the 30m cold dependency-configure guard before app compilation/tests.
   Retained configure log showed FFmpeg 16m + SPIR-V 11m then SPIRV-Cross active.
   Scoped windows-ci configure guard is now 40m; outer job/app/test guards unchanged.
6. C02 controls/window code 0c98675065db977864b25eb991bba9f644f1192c and inventory
   code 4d4bcc3325c28aa348c113f797878eb719304151 have clean Debug/Release 23/23
   and actual GPU/window evidence in M04-C02-local.json / M04-C02-inventory-local.json.
   They were published with evidence/CI fix at 256e2cb4426389ec444ebf6d3af72378e75a04fe.
   CI 38847721626 is fully green on Windows Debug/Release, Linux/macOS, plan and
   quality-gate. Hosted compilation is separate from GPU/physical qualification.
7. C02 engine-owner code 847d25462c0a65c19c0709fa8715ba96991f7ac8: --render creates a native GPU/window
   owner; control/status mailboxes carry bounded values. Core validates native
   generation/revision/frame/clock and preserves original cached outcome metadata.
   Svelte output flags and edit release share pipe/OSC validation. Actual 900-frame
   tests cover UI checkbox, reload, owned browser process death, no clients for
   2s, native controls, pending resource shape/undo and granted OSC bundle. Final
   offline three-window captures use fixed tolerance 2/255. Initial Node stdin
   teardown bug fixed without widening deadlines; owned browser job cleanup passes.
   Clean Debug/Release wrappers pass 23/23, dirty=false, at that code SHA. Both
   900-frame engine/three-capture runs and compositor/window API regressions pass.
   M04-C02-native-program-local.json and M04-native-program-ui.png retain actual
   source/binary/environment/JUnit/GPU/browser evidence. Evidence commit 3700aa4.
   Install fix 00422799c7105204e15c5ea216b28b74c74cbd00 copies reviewed SDL3 shared runtime/notice
   and GPL license; committed-source installed Release static/60-frame GPU smoke
   passes with Node absent from child PATH. M04-native-install-local.json retained.
   Published at bdde9d5a4d6839a2d161b53adacfc222ee225f5f; CI 38857240163
   is fully green on Windows Debug/Release, Linux/macOS, plan and quality-gate.
8. Next: implement actual GPU resource preparation/handoff for new graphs/images,
   sizes, bindings and mapping while the old native scene keeps running. Current
   compatible flags/metadata apply; other edits remain explicitly unapplied.
   The pinned bgfx default creates a SINGLETHREADED D3D11 device. A follow-up
   now owns/injects a hardware device without that flag. Startup image imports
   borrow explicitly owned immutable textures retained through bgfx shutdown.
   Actual Debug/Release GPU probes each prepare 128 generations on a background
   thread while 120 native frames advance, retain eight/1 MiB, reject five invalid
   requests and pass final active-output/bitwise-float readbacks. The actual driver
   reports concurrent creation support. All immediate-context/bgfx calls still
   belong to one render owner. Local dirty-source wrappers pass 23/23, and the
   existing compositor/window/900-frame browser-loss checks pass in both configs.
   Code e83b8947a8848ebb2f13dbe5f6edd7664733ebd9 now has clean Debug/Release
   23/23 and all GPU/engine regressions, dirty=false, retained in
   M04-preparation-device-local.json. Next implement runtime generation adoption/
   retirement and graph/window handoff. A first fixed-layout adapter handoff is
   now implemented: background immutable sources copy into preallocated mutable
   sampled textures on the immediate-context owner before drawing. One existing
   query gives nonblocking completion; caller owns busy candidates and receives
   retired ownership for background disposal. Copies/bytes are explicit, old/
   foreign generations and layout/byte limits validate before GPU commands.
   Debug/Release hardware probes pass two live generations and six freeze/
   blackout goldens (maximum error 1 at tolerance 2/255). Both wrappers pass
   23/23 and existing GPU/window/900-frame engine regressions pass. Code
   073175d67978ed95e1cb42449a8489d62769e6d3 has clean Debug/Release evidence,
   dirty=false, in M04-image-handoff-local.json. Preparation foundation code
   e83b894 and evidence a6a0fd6 precede it. Publish the tested commits/evidence.
   Next connect bounded preparation/candidate/retirement mailboxes to engine
   commands while continuing old output; stop/join preparation before bgfx
   shutdown. Tag control batches with the desired scene generation: controls for
   an unapplied scene must not advance applied revision on the old image. Bound
   working/pending/ready/retired ownership and aggregate candidate bytes; cancel
   superseded work and dispose it off the render owner. Shape-changing graph/
   window edits and C03 mapping still remain.
   C03 homography/masks/color rules, encoded preview and C04 full scenarios remain.

The engine now has a static-pattern native program path and actual browser-loss
evidence. No animated-media/resource-handoff, encoded preview, physical scanout,
sustained budgets or recovery claim. M01/M03 prerequisites still prevent milestone
qualification. No M01/M02/M03/M04 tag.

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
