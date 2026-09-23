# PRIORIDADE MÁXIMA — ESTABILIDADE DO AGENT MODE

A falha mostrada como **“Agent Mode Connection Failed”** significa perda/intermitência do canal remoto e deve ser tratada antes de atribuir erro ao código.

Regras obrigatórias desta prioridade:
- Minimizar chamadas remotas: preferir **1 chamada grande por etapa** e **1 leitura de resultado**.
- Dentro da chamada grande, executar localmente no Windows: inspeção, edição, build, testes, git status, logs e diagnóstico.
- Gravar logs persistentes fora do worktree em D:\Users\tcfialho\Documents\NRFusion-*.log.
- Não fazer dezenas de read_file, search, list_processes ou chamadas pequenas em sequência quando PowerShell/grep local resolve na mesma execução.
- Não usar chamadas remotas concorrentes.
- Antes de repetir trabalho após timeout/desconexão, verificar processo e log já existentes.
- Se a conexão cair, preservar o estado local e retomar pelo log/processo existente; não reinterpretar automaticamente a queda como falha do código.
- Em tarefas médias/longas, encapsular a etapa em script PowerShell local e executar em lote; retornar ao MCP só para coletar o resultado.
- Priorizar robustez do canal sobre granularidade de observação. Menos round-trips é melhor.
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

Date: 2026-09-23 BRT
Branch: standalone/integration
Validated code head: e845b14

Phase 06: CLOSED.
Phase 07: CLOSED, including real Windows compile/harness validation.
Phase 08: CLOSED.

Windows gate closure:
- MinGW x64/Ninja full build: PASS.
- MinGW x64 CTest with RTX hardware owner gates: 35/35 PASS.
- MSVC 19.44 x64 full build: PASS.
- MSVC x64 CTest with RTX hardware owner gates: 35/35 PASS.
- D3D12CarrierNativeAcquire.cpp and D3D12CarrierExecutor.cpp compiled under MSVC x64.
- MSVC Win32 built PE x86 capture DLL/roundtrip.
- D3D11 hook x86: PASS.
- x86 capture -> x64 NRFusionHost64 roundtrip: PASS, including Neural 1.0 and reduced 0.5.

Validation-driven fixes preserved in Git:
- WinAPI version proxy declarations corrected per toolchain.
- harness no longer requires DirectXMath.
- synthetic DX12 tests use portable half conversion.
- residual GPU CTest runs from the source root.
- UNORM midpoint regression accepts the legal 127/128 result.
- Host64 defers GPU/NGX init until shared transport needs it.
- D3D12 owner tests can opt into a real hardware device.
- Host64 and capture roundtrip oversized responsibilities were split.

Phase 08 subgate 08a:
- Diagnostics.cpp reduced from 285 to 248 lines without adding a patcher source.
- NrSession remains the sole standalone owner of timed WorkTicket identity and retirement.
- Existing NrD3D12Diagnostics timing is explicit profiling, not the normal telemetry owner.
- D3D12 timing next needs a focused query-ring/readback owner with cached queue frequency and no waits.


Phase 08 subgates 08b/08c:
- portable retired-timing contract and `FakeTimingSource` are implemented with exact `WorkTicket` identity;
- `NrSession` consumes a retired NR timing once on the next frame and does not replay stale timing;
- `D3D12RetiredTimingSource` owns a bounded 8-slot query/readback ring with cached queue frequency;
- normal timing records 2 timestamps + 1 resolve per sampled workload and never waits for current GPU work;
- hardware timing gate passes with `NRFUSION_TEST_D3D12_HARDWARE=1`; default gate skips before WARP creation.


Phase 08 closure:
- retired timing contract, fake source, D3D12 query/readback owner and end-to-end hardware carrier gate are complete;
- MinGW hardware suite 37/37 PASS; MSVC focused timing/session gates 3/3 PASS;
- normal timing path has no GPU wait/event/sleep and retired timing is consumed once;
- timing/diagnostics first-party files remain <=300 lines;
- physical provider/Host64/patcher cutover remains deferred to the global cutover phase.

Phase 09: NOT STARTED.

Known non-gate issue:
- python tools/check_source_size.py changed still reports the locked vendored
  shaders/vendor/optiscaler_dlssnr/dlssnr.hlsl (1110 lines). This is third-party locked source,
  not a Windows functional failure; the checker needs a vendor exemption before final cutover.

Remaining global gate:
- real-game execution and physical provider/Host64/patcher cutover.

Exact next action:
- start Phase 09 from the existing SyntheticDx11BridgeProvider and current D3D11 hook/bridge tests;
- baseline tests before edits, then separate D3D11 acquisition/hook ownership from D3D11<->D3D12 bridge ownership;
- define slot/fence reuse and controlled x64 hook/acquisition validation;
- keep x86 and CPU frame transport out of Phase 09.
