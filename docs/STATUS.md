# Project status

MapGyver implementation started 2026-10-15 in Golden-Geek/MapGyver2.

- Primary deliverable: a qualified Windows x64 beta through M13.
- [M00, repository and CI foundation](progress/M00.md): complete, with local
  fresh-checkout and hosted Windows/Linux/macOS build/test evidence.
- Current milestone: [M01, graphics and sharing feasibility](progress/M01.md),
  in progress. [M02 portable domain work](progress/M02.md) proceeds independently
  while M01 qualification stays open; M03–M19 remain planned. M02 cannot be
  completed ahead of M01. Completing M00 does not qualify the beta.
- M01.C01 texture sampling and M01.C02 bidirectional Spout/bgfx interoperability
  have clean native Debug/Release GPU evidence and green hosted builds.
  M01.C03 has clean native decoder/import, owned process-fault and cooperating
  frame-set and hidden multi-window API evidence. Physical two-output/scanout,
  device-loss recovery and sustained performance gates remain open. Renderer
  provisional; no M01 completion tag.
- First architecture gate: M01, render/Spout/decode/presentation feasibility on Windows hardware.
- Open product decisions: current NDI licensing/distribution path and exact reference hardware/workload budgets.
- Later native targets: Windows ARM64, Linux x64/PipeWire, macOS ARM64/Syphon, Linux ARM64/Raspberry Pi.
- Separate optional target: 32-bit Linux armhf.

The planning baseline is committed and published. Application checks, CI source revisions
and remaining gates are recorded in the milestone evidence; planning validation alone
does not qualify application behavior. Only the laptop is currently available for GPU
experiments; two physical outputs/projectors remain an external qualification gate.

Track each milestone with `planned`, `in_progress`, `blocked` or `complete`, plus a link to its progress evidence. A blocked optional integration does not disappear from requirement coverage. Required Windows capability gaps prevent an unqualified claim that the full Windows beta meets the product contract.
