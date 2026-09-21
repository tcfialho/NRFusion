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

- Hard cap: every first-party handwritten code file must be <=300 physical lines after formatting.
- Blank lines and comments count. The rule is intentionally mechanical and unambiguous.
- Soft limit: at ~250 lines, stop adding responsibility and split before reaching 300.
- Applies to production code, headers, CUDA, shaders, tests, harnesses, tools, scripts, CMake/build logic and installer code.
- Excludes documentation, generated files, vendored/third-party code and fixtures copied verbatim from upstream.
- Do not evade the cap with minification, multiple statements per line, giant embedded code strings, generated-style .inc dumps, or by moving implementation into headers.
- Split by responsibility/lifetime/ownership, never arbitrary Part1/Part2 chunks.
- Existing >300-line first-party files may remain untouched during transition, but must not grow. If standalone work needs to modify one substantially, split it mechanically first or retire it in that phase.
- Final standalone cutover requires zero first-party handwritten code files above 300 lines.

## Comments

- Prefer clearer names or extracted functions over comments.
- Comment only why something non-obvious is necessary, never restate what the code does.
- No narrative architecture headers, decorative arrows, or comments longer than 120 characters.

## Current session

Start: 2026-09-20 21:45 BRT
Base plan head: 6d3209c9b6abec46ca0e6c3107ec41f81531d313
Target: refine the standalone plan around the hard <=300-line handwritten source rule.
Checklist:
- define exact counting/scope and anti-evasion rules
- classify current oversized code as split/retire/final cleanup
- refine phases that own large existing files
- make zero >300 first-party files a qualification/cutover gate
- update branch once after review
