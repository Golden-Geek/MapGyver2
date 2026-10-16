# MapGyver

MapGyver is a GPL projection-mapping application under development at
[Golden-Geek/MapGyver2](https://github.com/Golden-Geek/MapGyver2). Windows x64 is
the first delivery target, with a native C++20 engine and static web editor.
The implementation includes a validated portable model, local browser/OSC control,
native static composition/mapping and background PNG/JPEG import. Video/audio,
complete editor workflows and hardware qualification remain in progress. See
[project status](docs/STATUS.md) for actual evidence.

Build and run diagnostics using [the build guide](docs/BUILD.md). Track ongoing
work in [the active execution plan](docs/execution/ACTIVE.md).

```powershell
python tools/ci/check.py --preset windows-local --configuration Release
out/build/windows-local/Release/mapgyver-engine.exe --diagnose --json --no-gpu
```

Use `windows-ci` with VS2022 or `windows-local` with VS2026. See the build guide
for portable builds, prerequisites, evidence and diagnostic capability limits.

The retained planning contract describes 20 milestones and 80 commit slices.
The authorized delivery is M00–M13, Windows beta. Later native ports and advanced
calibration/distributed rendering remain planned. Inactive CI templates are
activated only when their real build/test contracts exist.

| File | Use |
| --- | --- |
| [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) | Product contract, architecture, sequencing and every planned commit |
| [AGENTS.md](AGENTS.md) | Short persistent implementation instructions |
| [PLANS.md](PLANS.md) | Durable execution plans and continuation across sessions |
| [CODEX_START.md](CODEX_START.md) | Copyable first task and continuation prompt |
| [docs/backlog.json](docs/backlog.json) | Authoritative milestone, requirement and acceptance IDs |
| [docs/CI_AND_RELEASE.md](docs/CI_AND_RELEASE.md) | CI jobs, gates, hardware evidence, packaging and release policy |
| [docs/ACCEPTANCE.md](docs/ACCEPTANCE.md) | Scenario catalogue and qualification profiles |
| [docs/DECISIONS.md](docs/DECISIONS.md) | Accepted defaults, provisional decisions and blocking gates |
| [docs/PROTOCOL_CONTRACTS.md](docs/PROTOCOL_CONTRACTS.md) | Commands, parameters, frame sets and managed applications |
| [docs/SOURCES.md](docs/SOURCES.md) | Primary-source references and verified action revisions |
| [docs/STATUS.md](docs/STATUS.md) | Current implementation and qualification state |
| [ci/activation.json](ci/activation.json) | When each CI template becomes active |
| [tools/validate_plan.py](tools/validate_plan.py) | Backlog, dependencies, coverage and file-link validation |

This repository is a new implementation. The original
[MapGyver](https://github.com/Golden-Geek/MapGyver) is a design/code reference;
there is no existing JUCE application being replaced here.

The Plan workflow validates the contract. CI builds Windows Debug/Release and
Linux/macOS portable core, runs real tests, and retains source/environment/log
evidence. Its stable quality gate fails unless every prerequisite succeeds.
Later workflow templates stay inactive until their real contracts exist.

Local validation: `python tools/validate_plan.py`. Focused task context: `python tools/show_milestone.py M00`.

Application code, handoff documents and helpers are GPL-3.0-or-later; external
dependencies retain their own licenses. See LICENSE and
[dependency provenance](docs/licenses/README.md). NDI remains optional and blocked
pending the owner-approved distribution path. The
[original pack reports](docs/planning-baseline/README.md) are historical evidence.
