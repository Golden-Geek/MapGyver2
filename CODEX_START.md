# Start the Windows implementation

For ongoing work, read docs/STATUS.md and docs/execution/ACTIVE.md first. The
bootstrap prompt below is retained as the original M00 task; do not restart a
completed milestone. Resume the next unfinished authorized slice.

Use this prompt from the root of the actual target repository after installing the handoff pack:

> Implement milestone M00 of this projection-mapping project. Read AGENTS.md, docs/STATUS.md, docs/DECISIONS.md and the M00 section of IMPLEMENTATION_PLAN.md. Treat docs/backlog.json as the milestone and commit contract. Start by inspecting the repository and existing build instructions, preserve unrelated work, and create docs/execution/ACTIVE.md for this task. Establish a pinned C++20 Windows x64 build, a portable core target, actual diagnostic CLI/schema smoke checks and the GitHub Actions gates described in docs/CI_AND_RELEASE.md. Adapt the inactive CI templates only when their real target/script contracts exist. Use the four planned M00 commit slices, recording the actual hashes and validation evidence. If working in MapGyver rather than a new repository, first document the existing JUCE build and a bounded migration/integration strategy. Do not implement later application milestones in this task. Complete everything in M00 that the available environment supports; clearly retain any unexecuted Windows or remote CI gate as pending. Do not claim a hosted Windows build or real GPU test ran unless its evidence exists. Commit completed slices locally. Report the resulting commits, checks, remaining gate and the next action for M01.

After M00 is qualified, use this continuation prompt:

> Continue with the next ready milestone in docs/STATUS.md. Read its dependency evidence, AGENTS.md and relevant contracts. Implement its planned slices, keep the execution plan current, run the applicable tests, and create local commits for completed work. Checkpoint only when its acceptance gates truly pass. If a hardware or licensing gate is unavailable, retain it explicitly and continue independent work within this task scope. Report real source revisions and evidence; do not infer completion from compilation alone.

For M01, provide access to a Windows x64 workstation with the target GPU/driver, at least two presentation outputs and independent Spout sender/receiver applications. The first reference managed-app client will later exercise one and three named outputs. A hosted Actions runner can compile experiments but cannot certify projector timing, GPU sharing performance or hotplug behavior.

An implementation task may authorize several milestones explicitly. In that case Codex should continue through ready milestones without asking for routine permission between commits. Milestone dependencies and the current task scope still govern the work.
