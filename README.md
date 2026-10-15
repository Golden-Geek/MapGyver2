# MapGyver

MapGyver is a GPL projection-mapping application under development at
[Golden-Geek/MapGyver2](https://github.com/Golden-Geek/MapGyver2). Windows x64 is
the first delivery target, with a native C++20 engine and static web editor.
The current implementation is the M00 build foundation; playback and editing
are not implemented yet. See [project status](docs/STATUS.md) for actual evidence.

**Start with [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md), then give Codex [CODEX_START.md](CODEX_START.md).**

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
| [docs/STATUS.md](docs/STATUS.md) | Honest initial project state |
| [ci/activation.json](ci/activation.json) | When each CI template becomes active |
| [tools/validate_plan.py](tools/validate_plan.py) | Backlog, dependencies, coverage and file-link validation |

This repository is a new implementation. The original
[MapGyver](https://github.com/Golden-Geek/MapGyver) is a design/code reference;
there is no existing JUCE application being replaced here.

The `.github/workflows/plan.yml` workflow validates this planning pack after it is installed in a repository. Files under `ci/templates` end in `.yml.in` and deliberately do not run until their prerequisite targets and scripts exist. No application build can truthfully pass before that implementation exists.

Local validation: `python tools/validate_plan.py`. Focused task context: `python tools/show_milestone.py M00`.

Original handoff documentation and helper tools are provided under GPL-3.0-or-later; external references retain their own licenses. The canonical GPLv3 text is included in LICENSE. The application dependency notices and approved optional-runtime path are completed during implementation.
