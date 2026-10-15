# Execution plan protocol

Use a durable execution plan for work that spans several commits, changes multiple subsystems, or depends on hardware/SDK evidence. Keep it in `docs/execution/ACTIVE.md`; archive completed plans under `docs/execution/archive/` when useful. The plan is a continuation aid, not a second project specification.

Each plan records:

1. Selected milestone/commit IDs, scope and prerequisite evidence.
2. Current code/revision baseline and relevant working-tree changes.
3. Observable outcomes and exact validation commands.
4. Small implementation steps with pending/in-progress/completed status.
5. Decisions, experiments and why the next implementation is justified.
6. Actual test/CI/hardware evidence and explicit unverified gates.
7. Commit references and the next ready action.

Update it at material discoveries and before handing work to another session. After context loss, read the current code and this plan, verify the baseline, and resume the next unfinished step. Never reset completed work because a new session began. Keep implementation evidence separate from proposed outcomes.

A missing physical-output test blocks its dependent qualification, not useful independent work. Record the exact hardware/SDK requirement and the experiment to run; do not silently replace it with a mock or weaken the acceptance criteria.

For small reversible changes, use ordinary focused edits and relevant checks rather than generating ceremonial execution documents.
