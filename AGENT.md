# Session rules

- Use Git from the start; make small, frequent commits.
- Start by recording the real wall-clock timestamp, current Git state, last relevant commit, what works, and the next objective action.
- Keep a checklist of small, independently verifiable tasks.
- Bug workflow: reproduce -> identify cause -> fix -> regression-test -> validate the full flow beyond the former failure point.
- Commit relevant state before risky/destructive operations.
- Preserve progress with Git, useful logs, and checkpoints.
- For medium/long jobs, prefer persistent logs and verify existing processes before relaunching.
- At session end: stabilize, test, commit, create one external source ZIP, and record handoff as result | tests | commit | ZIP | blocker | exact next action.
- Do not include generated files, dependencies, secrets, or temporary artifacts in the source ZIP.

## Current objective

Fix Resident Evil Requiem's crash immediately after NRFusion reports `NR applied` by preserving and restoring the game's D3D12 command-list binding state around the real NGX neural hook, following the proven OptiScaler RE Engine pattern.

## Session checklist

- [ ] Add command-list state tracking for Reset, pipeline state, descriptor heaps, compute/graphics root signatures and root arguments.
- [ ] Suppress tracking while NRFusion performs its own neural dispatches.
- [ ] Snapshot/restore the tracked game state around ExecuteGameNeuralFrame in HookEvaluate.
- [ ] Add focused regression tests for binding-state restoration.
- [ ] Build proxy and tests.
- [ ] Install with NR disabled and verify menu stability.
- [ ] Enable NR and validate beyond the previous `NR applied` crash point.
- [ ] Capture evidence, commit, ZIP, handoff.
