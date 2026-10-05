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

Rebuild the NRFusion installer, install it into D:\Games\Resident Evil Requiem, reproduce the crash, fix it, rebuild/reinstall, and validate in-game that the filter is active with observable evidence.

## Session checklist

- [ ] Baseline build installer from current HEAD.
- [ ] Install baseline into Resident Evil Requiem and reproduce the crash.
- [ ] Capture crash/log evidence and identify the failing path.
- [ ] Implement the smallest fix plus a regression test.
- [ ] Build/test the fix and regenerate the installer.
- [ ] Reinstall and launch the real game past the former crash point.
- [ ] Capture in-game evidence that NRFusion/filter is active.
- [ ] Commit final state, create external source ZIP, record handoff.
