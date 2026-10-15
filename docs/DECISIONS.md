# Implementation decisions

| ID | State | Decision and consequence |
| --- | --- | --- |
| ADR-001 | Accepted default | New native engine with a web editor named MapGyver in Golden-Geek/MapGyver2; Windows x64 is first. Golden-Geek/MapGyver is a reference. This repository started with a planning pack and no application, so no JUCE migration is taking place. An existing-repository migration requires its own bounded integration ADR. |
| ADR-002 | Accepted | Portable C++20 domain/control/persistence. Platform presentation, codecs, process APIs and sharing live in adapters. |
| ADR-003 | Provisional until M01 | SDL3 for platform/window integration and bgfx as the first render candidate. Native external textures, synchronization, multiple windows and hardware decoding are acceptance gates. Native D3D11 behind the same narrow interface is the bounded fallback. |
| ADR-004 | Accepted | Assets, source instances, layers/clips, named-output compositions, cues, surfaces, groups, layouts, projection patches and output targets are distinct responsibilities. |
| ADR-005 | Accepted | One typed application registry and command validator power UI, persistence metadata, OSCQuery and remote control; canonical identities survive rename and organization changes. |
| ADR-006 | Accepted | Native output runs independently from editor and preview. One render worker/GPU initially, with explicit ownership; selective source/SDK worker isolation is justified by measured fault/copy tradeoffs. |
| ADR-007 | Accepted | Svelte/TypeScript/Vite static web editor; thin desktop packaging is optional after browser workflows are qualified. No Electron/Tauri dependency is needed for the first native engine. |
| ADR-008 | Accepted | FFmpeg plus explicit optimized decoder/HAP paths; never equate format import with accelerated playback or fast seeking. |
| ADR-009 | Accepted | SQLite local authored state, versioned schemas, stable IDs, portable media references and a consistent snapshot/export mechanism. Telemetry and frame ticks are not a persistent event stream. |
| ADR-010 | Accepted default | GPL-3.0-or-later for newly authored application code. Versioned dependency/license provenance is required; copied reference code needs a provenance review. |
| ADR-011 | Open blocking gate for NDI distribution | Establish the actual current SDK terms and whether owner-approved permission or a narrow linking exception is needed. Dynamic loading or IPC alone is not a compatibility decision. Core implementation can continue; NDI completion remains open. |
| ADR-012 | Accepted | Spout/Windows, Syphon/macOS, PipeWire/Linux and network transports share a transport-independent frame/port contract. Native ARM adapter availability is individually tested. |
| ADR-013 | Accepted | One surface is the default case of the arbitrary-surface model. Surface groups are operational collections; optional adjacency/UV seam metadata describes continuity. |
| ADR-014 | Accepted | Windows/Linux/macOS portable core builds begin at M00; native platform ports follow the Windows beta. Linux ARM64 is the first board target; armhf means separate 32-bit support. |
| ADR-015 | Open until M01 | Name exact reference hardware/driver/OS/media workloads and ratify provisional performance thresholds. Record budgets before later optimization claims. |
| ADR-016 | Accepted | Single editing owner plus read-only observers initially. Remote concurrency uses revisions and explicit ownership rather than an early CRDT implementation. |
| ADR-017 | Accepted | Advanced automatic calibration and distributed execution are post-beta extensions, with stable data/interface boundaries established earlier. |
| ADR-018 | Accepted | windows-2022 qualifies VS2022/v143 after actual windows-2025 supplied VS2026 without VS2022. Local VS2026/v145 is separate evidence. See docs/adr/ADR-018-ci-toolchain.md. |

An accepted default is actionable within the plan; it is not a claim that hardware qualification exists. Change a decision with a short ADR containing trigger, options considered, measured evidence, compatibility and acceptance impact. Do not change the stack simply because another framework is familiar.

Do not implement a complex plugin framework in M00. Native C ABI extensions or out-of-process plugins become useful once at least two real integrations reveal the necessary contract. Platform SDK ABIs are not the application's project format.
