# Agent workflow

- Protect work with small Git commits.
- Before changes: reproduce, identify the cause, then patch the smallest observable defect.
- For bugs: reproduce -> cause -> fix -> regression test -> full-flow validation.
- Prioritize delivery blockers over refactors.
- Preserve state before risky changes.
- End with: result | tests | commit | blocker | exact next action.

## Build discipline

- Do not update the remote branch after every commit when pushes trigger expensive CI.
- Accumulate small commits first without moving the branch ref.
- Do not start repeated full builds while code is still changing.
- Inspect logs from an existing failed/in-progress run before deciding whether another build is necessary.
- Update the branch ref only when the current change set is stable enough for validation.
- Prefer one final full build for the completed change set.
- If another code change is required after that build, batch all known fixes before triggering the next run.
- Add CI concurrency/cancellation where supported so obsolete runs from the same branch do not consume runners.

## Current session

Start: 2026-09-20 15:49 BRT
Target: improve NRFusion performance, portability, and validation without redundant CI runs.
