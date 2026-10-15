# ADR-018 — Qualify VS2022 on windows-2022

Accepted 2026-10-15 for M00.C03.

The pack specified windows-2025 with VS2022/v143. Actual run 38769687062 at
source d4d3367847548a60a2656a95f6b4059f725cc10c supplied VS2026 Enterprise
(MSVC 14.51) and no VS2022. Both Windows configurations failed at CMake generator
selection; Linux/macOS passed. The gate failed closed and artifacts were retained.
The image README still listed VS2022; runtime evidence takes precedence.

Use windows-2022 for the unchanged windows-ci VS2022/v143 preset. Alternatives
were switching the compatibility check to VS2026 or installing VS2022 on every
runner. The supported older image retains the intended compiler generation
without extra installer dependencies. Local VS2026/v145 checks continue.
This changes hosted infrastructure, not the Windows x64 product or renderer.

Acceptance requires actual Debug/Release build/CTest success on the replacement
runner, with compiler/SDK/image versions. No failed run qualifies M00.

Failed run: https://github.com/Golden-Geek/MapGyver2/actions/runs/38769687062.
