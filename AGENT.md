# Agent workflow

- Protect work continuously with Git and small logical commits.
- At session start, identify current state, last relevant commit, what already works, and the exact next action.
- Before implementation, keep a checklist of independent, verifiable items small enough for one <=20 minute interaction.
- Prioritize delivery/integration blockers over refinements.
- For bugs: reproduce -> cause -> fix -> regression test -> full-flow validation.
- Preserve committed state before destructive or risky changes.
- Use nohup + PID + persistent log for medium/long jobs when shell execution is available.
- Before restarting interrupted work, inspect existing process/log state and do not duplicate heavy jobs.
- Keep useful logs/checkpoints progressively. Do not depend on a final save only.
- Generate one source checkpoint ZIP per session outside the worktree when source code changed materially.
- End with: result | tests | commit | ZIP | blocker | exact next action.

## Operational speed and validation discipline

### Session budget

- Hard session limit: 20 minutes measured by real wall-clock time, not by perceived amount of work.
- At 15 minutes, stop opening new work and stabilize the current change.
- At 18 minutes, freeze code and switch only to validation, checkpoint and handoff.
- Never spend the end of a session waiting for a long CI/build.
- If a remote job is still running at freeze, record its run ID and exact expected next action.
- Inspect unfinished remote jobs at the start of the next session instead of duplicating them.
- Prefer a smaller completed subgate over starting another responsibility that cannot finish inside the session.

### Branch and PR policy

- Use one long-lived standalone integration branch until final cutover.
- Do not create one branch per phase, subphase, review or fix.
- Do not create validation PRs for intermediate phases.
- Create a PR only for final cutover unless the user explicitly requests another PR.
- Preserve progress with normal Git commits, not with extra branches or PRs.
- While developing, create small local/detached commits as needed and advance the integration branch only when a coherent subgate is stable.
- Never force-push or rewrite shared history unless explicitly required and verified safe.
- If concurrent work moved the branch, inspect ancestry and tree differences first.
- Rebase or cherry-pick only the missing work after that inspection; never blindly overwrite the branch.

### Git checkpoint discipline

- Commit before risky refactoring or destructive changes.
- Prefer several small logical commits over one large commit.
- Do not push every small commit when a push triggers CI.
- Accumulate a stable logical batch, then advance the integration branch once.
- Documentation-only closure commits must not trigger expensive validation.
- Git is the primary recovery mechanism; the ZIP is an external checkpoint, not a replacement for versioned state.
- At session end, record the integration branch, validated code commit, current branch head, outstanding WIP commits and exact next action.

### Tool-call economy

- Batch independent repository reads into one orchestration call whenever possible.
- Prefer one call that reads several related files over repeated single-file calls.
- Prefer one search containing several related queries over sequential searches.
- After the first audit, fetch only changed or directly relevant files; do not repeatedly reread unchanged files.
- Reuse commit SHAs, tree SHAs, blob SHAs, file contents, refs and workflow IDs already obtained during the session.
- Do not ask the same repository-state question twice unless the underlying ref changed.
- Use compare/diff once per stable batch instead of repeatedly comparing after every commit.
- When inspecting a large file, request only the needed ranges after the first structural scan.
- Use one orchestration call for related independent GitHub reads in parallel whenever possible.
- Prefer one read batch, one write batch and one final verification batch for a normal subgate.
- Avoid low-value status calls whose result cannot change the next action.
- A normal coding session should need only a few connector round-trips; exceed that only for an actual blocker.

### Build and validation discipline

- Never use full Windows CI as the inner development loop.
- First use structural checks, focused unit tests and targeted compilation.
- Keep focused validation targets isolated from large libraries when they can compile only the production units they actually exercise.
- Do not use Windows validation as an intermediate development gate.
- Do not ask the user to run Windows tests or builds during implementation.
- Develop and review with the available environment: static analysis, portable compilation, fakes and focused non-Windows tests.
- Windows build/integration/installer validation is deferred to the final cutover unless the user explicitly requests it earlier.
- Portable CI may run on master code pushes; docs-only master pushes are ignored.
- Do not repeatedly rebuild an unchanged dependency graph merely to validate a small boundary.
- If an expensive workflow exceeds the session cap, record its run ID and stop instead of waiting.
- Keep CI concurrency/cancellation enabled so obsolete runs do not consume runners.

### CI polling discipline

- Never continuously poll a workflow.
- After triggering CI, continue useful local/static work only if it does not modify the validated batch.
- Check CI at most once after enough time has passed for the relevant fast job to finish, once near session freeze, and immediately after a reported failure.
- Before relaunching a failed or interrupted heavy job, inspect its existing status/logs and fix or explain the cause first.
- Do not start another equivalent workflow while the previous relevant run is still active.

## Source size

- Immediate rule: every new or substantively modified first-party handwritten code file for standalone work must be <=300 physical lines after formatting.
- Blank lines and comments count. The rule is intentionally mechanical.
- Soft limit: at ~250 lines, stop adding responsibility and split before reaching 300.
- Applies to production code, headers, CUDA, shaders, tests, harnesses, tools, scripts, CMake/build logic and installer code.
- Transition-only grandfathering: existing >300-line first-party files may remain read-only/no-growth until their owning phase splits or retires them.
- Final cutover requires zero first-party handwritten code files above 300 lines.
- Generated-file exemption is valid only when the file is reproducibly produced from tracked inputs, lives under a generated directory, is marked generated, and is not manually edited. The checker verifies location/marker; review verifies reproducibility.
- Vendored/third-party code and upstream fixtures are exempt only while unmodified. Local handwritten modifications count as first-party code.
- Do not evade the cap with minification, multiple statements per line, giant embedded code strings, generated-style .inc dumps, or by moving implementation into headers.
- Split by responsibility/lifetime/ownership, never arbitrary Part1/Part2 chunks or one God class spread across partial files.
- The 300-line rule must not create runtime cost: do not add virtual dispatch, heap/PImpl, shared ownership, locks, atomics, or indirect calls solely to split files.
- Splitting hot code across translation units can change inlining/optimization. Measure before/after when the split touches a measured hot path; preserve small inline helpers only when justified.

## Comments

- Prefer clearer names or extracted functions over comments.
- Comment only why something non-obvious is necessary, never restate what the code does.
- No narrative architecture headers, decorative arrows, or comments longer than 120 characters.

## Current session

Start: 2026-09-22 08:32 BRT
Branch: standalone/integration
Base/default branch: master
Phase 06: CLOSED AFTER POST-CLOSE ADVERSARIAL REVIEW.

Validated code commit:
916190b95f50b9f54d1af320db30495ede87c317

Focused portable validation:
- original closure run: 35723235377 -> PASS
- post-review run: 35724258682 -> PASS
- configure/build/nrfusion_nr_session_tests/nrfusion_nr_session_stress_tests: PASS
- measured stress: 1,000,000 frames, exactly 0 heap allocations
- Windows: not used

Post-close defect found and fixed:
- MapTimedWork accepted the same submitted WorkTicket more than once
- duplicate entries could consume the 16-slot timing ring and displace valid work
- NrSessionWorkTracker now marks timing mapping one-shot with fixed per-entry state
- duplicate MapTimedWork fails before touching the ring
- regression added and validated
- removed one duplicate nrRecentSamples_.clear() and emaNrMs_=0 assignment in the downshift hot path

Current source-size state:
- src/PerformanceController.cpp: 282 lines
- src/PerformanceControllerLifecycle.cpp: 209 lines
- src/PerformanceControllerScale.cpp: 56 lines
- include/nrfusion/FusionRuntime.hpp: 296 lines
- include/nrfusion/NrSessionWorkState.hpp: 73 lines
- src/NrSessionWorkState.cpp: 130 lines
- src/NrSession.cpp: 138 lines
- tests/nr_session_tests.cpp: 204 lines

Outstanding Phase 06 work: none.

Exact next action:
- read docs/standalone/07-*.md and start Phase 07 on standalone/integration
- preserve 916190b95f50b9f54d1af320db30495ede87c317 as the Phase 06 validated code baseline
- do not open a new branch or PR
