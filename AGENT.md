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

Restore NRFusion's normal in-game interface in Resident Evil Requiem while keeping only the minimum compatibility fix needed to avoid the black-screen/crash path.

## Session checklist

- [ ] Separate neural discovery from capture/presentation hooks.
- [ ] Restore the normal in-frame overlay path without re-enabling DLSS-G mutation/Streamline overrides.
- [ ] Validate the normal UI with NRFusion disabled.
- [ ] Validate NR enabled past the previous failure point.
- [ ] Run focused regression tests.
- [ ] Commit, create one external source ZIP, and record handoff.
