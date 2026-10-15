# Projection Mapping Codex Handoff

A repository-ready implementation plan for a GPL projection-mapping application. Windows x64 is the first delivery target. The architecture retains native Windows ARM64, macOS ARM64, Linux x64 and Linux ARM64 paths; 32-bit armhf is a separate optional extension.

**Start with [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md), then give Codex [CODEX_START.md](CODEX_START.md).**

This package contains planning documents, a machine-readable backlog, validation tooling and inactive CI templates. It does not contain the application implementation. All 20 milestones and 80 commits are planned; no application milestone, hardware qualification, GitHub workflow or repository commit has been executed by preparing this pack.

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

Copy the package contents into a new target repository. Preserve any existing repository instructions and adapt this pack rather than overwriting unrelated files. MapGyver is a design/code reference, not an implied destination for a rewrite. If the chosen repository is MapGyver, begin with an integration ADR covering its current JUCE build and migration boundary before replacing application architecture.

The `.github/workflows/plan.yml` workflow validates this planning pack after it is installed in a repository. Files under `ci/templates` end in `.yml.in` and deliberately do not run until their prerequisite targets and scripts exist. No application build can truthfully pass before that implementation exists.

Local validation: `python tools/validate_plan.py`. Focused task context: `python tools/show_milestone.py M00`.

Original handoff documentation and helper tools are provided under GPL-3.0-or-later; external references retain their own licenses. The canonical GPLv3 text is included in LICENSE. The application dependency notices and approved optional-runtime path are completed during implementation.
