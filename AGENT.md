# Agent workflow

- Protect work continuously with Git and small commits.
- At session start, identify current state, last relevant commit, what works, and the exact next action.
- Before implementation, keep a checklist of independent, verifiable items small enough for one <=20 minute interaction.
- Session hard cap is 20 minutes. Stabilize by 15 minutes, freeze by 18, checkpoint/handoff by 20.
- Prioritize delivery/integration blockers over refinements.
- For bugs: reproduce -> cause -> fix -> regression test -> full-flow validation.
- Preserve state before destructive or risky changes.
- Use nohup + PID + persistent log for medium/long jobs when shell execution is available.
- Before restarting interrupted work, inspect existing process/log state and do not duplicate heavy jobs.
- Keep useful logs/checkpoints progressively. Do not depend on a final save only.
- Generate one source checkpoint ZIP per session outside the worktree when source code changed materially.
- End with: result | tests | commit | ZIP | blocker | exact next action.

## Tool discipline

- Batch independent connector operations into one orchestration call per stage.
- Reuse returned content, SHAs, trees and refs instead of fetching the same state again.
- Do not perform repeated exploratory searches after the owner/file/commit is known.
- Prefer one compare/read batch, one write batch and one final verification batch.
- Do not query time, branch state or workflow state repeatedly unless it changes the next action.
- A normal coding session should use only a few connector round-trips; exceed that only for an actual blocker.

## Branch / PR / build discipline

- Use one long-lived standalone integration branch until final cutover. Do not create a branch per phase.
- Do not open a PR per phase. Keep phase boundaries in commits/docs; use one final PR only when cutover is ready.
- Accumulate small detached/local commits while code is changing, then advance the integration branch at checkpoints.
- Never use full Windows CI as the inner development loop.
- First use structural checks, focused unit tests and targeted compilation.
- Windows workflow is manual-only. Use fast mode with explicit targets/test regex for focused validation.
- Full Windows distribution/NSIS validation is reserved for major integration milestones and final cutover.
- Portable CI may run on master code pushes; docs-only master pushes are ignored.
- If an expensive workflow exceeds the session cap, record its run id and stop; inspect it at the next session.
- Keep CI concurrency/cancellation enabled so obsolete runs do not consume runners.

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

Start: 2026-09-21 21:13 BRT
Branch: standalone/phase-05-d3d12-executor-20260921
Base: master 548349b740e6fdfec7b0a9f6dacc17ebcdf9b160
Phase 05: in progress; subgates 01-03a complete; 03b implementation staged.

Completed this session:
- reanchored the integration history as master + two scratch WIP commits
- proved old/new integration trees identical before moving the branch
- fixed invalid scratch kind incorrectly selecting HdrCopy
- added fail-closed regression
- isolated scratch validation from nrfusion_core
- added WARP lifecycle/state/resize/retirement regression
- confirmed master Portable run 35669420707 passed
- no full Windows build or new PR
- connector calls are now batched and reused instead of repeated fetch/search loops

Exact next action:
- run only nrfusion_nr_scratch_resources_tests on Windows fast validation
- if it passes, wire output/colorCopy/hdrCopy into the first real encode/copy seam
- keep HDR/exposure/residual/multipass out until this resource-state subgate is validated
