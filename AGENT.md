# Agent workflow

- Protect work continuously with Git and small commits.
- At session start, identify current state, last relevant commit, what works, and the exact next action.
- Before implementation, keep a checklist of independent, verifiable items small enough for one <=25 minute interaction.
- Measure real wall-clock session time. At 20 minutes stabilize; at 23 minutes freeze, checkpoint, and hand off.
- Prioritize delivery/integration blockers over refinements.
- For bugs: reproduce -> cause -> fix -> regression test -> full-flow validation.
- Preserve state before destructive or risky changes.
- Use nohup + PID + persistent log for medium/long jobs when shell execution is available.
- Before restarting interrupted work, inspect existing process/log state and do not duplicate heavy jobs.
- Keep useful logs/checkpoints progressively. Do not depend on a final save only.
- Generate one source checkpoint ZIP per session outside the worktree when source code changed materially.
- End with: result | tests | commit | ZIP | blocker | exact next action.

## Build discipline

- Do not update a remote feature branch after every commit when PR synchronization triggers expensive CI.
- Accumulate small detached/local commits while code is changing.
- Do not repeatedly run full builds while code is changing.
- Inspect existing CI state/logs before deciding whether another build is necessary.
- Move the branch ref only when the current change set is stable enough for validation.
- Prefer one final full build for the completed change set.
- If another fix is required after that build, batch all known fixes before the next remote update.
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

Start: 2026-09-20 23:57 BRT
Branch: standalone/phase-01-frame-contract-20260920
Base phase-00: c76d04e1999b6306eda66d29370776cc7aed1c2f
Phase 01: complete.
Completed:
- extracted portable FrameContract from Types.hpp
- added explicit resource provenance/reliability/ownership/lifetime and frame binding
- preserved legacy aggregate/source compatibility without touching the 3900-line patcher
- made PipelinePolicy consume the new reliability accessors
- added fake-provider and 100k-frame contract validation
- compiled contract tests and PipelinePolicy boundary with warnings-as-errors
Exact next action:
- start Phase 02 from docs/standalone/02-runtime-shell.md
- audit GameProbe/bootstrap/config ownership before creating standalone lifecycle objects
