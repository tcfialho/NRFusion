# PRIORIDADE MÁXIMA — OPERACIONAL GITHUB-FIRST

O desenvolvimento normal usa GitHub como fonte canônica e evita depender do Agent Mode/Windows físico.

Regras obrigatórias:
- standalone/integration é a branch canônica até o cutover final.
- Fazer leitura, edição, commits, push e acompanhamento de CI pelo conector GitHub sempre que o trabalho não exigir hardware local.
- Usar GitHub Actions Ubuntu como primeiro gate para contratos/testes portáteis.
- Usar GitHub-hosted Windows como gate automático de compilação/testes Windows que não exigem GPU/driver físico.
- Usar o notebook Windows via Agent Mode somente para gates que realmente dependem de GPU física, driver, hook/interop real, x86->x64 local, jogo real ou artefato local não reproduzível no CI hospedado.
- Antes de um gate físico: exigir branch remota estável e CI hospedado relevante aprovado; então fazer git fetch + git pull --ff-only.
- Não corrigir código diretamente no notebook após falha de gate físico. Coletar o log, corrigir via GitHub, validar CI e só então repetir o gate.
- No Agent Mode, preferir 1 chamada grande por gate + 1 leitura de resultado, com log persistente fora do worktree.
- Nunca interpretar queda de Agent Mode como falha do código sem evidência do processo/log local.
- Não manter divergência deliberada entre checkout local e origin/standalone/integration.
- Pushes de documentação não devem disparar gates caros.
- O gate físico é confirmação final de uma capacidade específica, não loop de desenvolvimento.

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

### GitHub-first development discipline

- Prefer GitHub connector reads/writes over Remote Desktop for ordinary source development.
- Build coherent multi-file commits atomically when practical; keep commit scope small and reviewable.
- Portable CI is the first executable gate for portable code.
- GitHub-hosted Windows fast CI is the first Windows compile/test gate.
- Physical Windows/RTX is reserved for capabilities unavailable on hosted runners.
- A physical-gate failure produces a log/evidence bundle; fixes go back through GitHub and hosted CI before another physical run.
- Keep the physical checkout clean and fast-forward-only from the canonical integration branch.
- Do not use the physical machine as a second development branch.

### Build and validation discipline

- Never use full Windows CI as the inner development loop.
- First use structural checks, focused unit tests and targeted compilation.
- Keep focused validation targets isolated from large libraries when they can compile only the production units they actually exercise.
- Do not use Windows validation as an intermediate development gate.
- Do not ask the user to run Windows tests or builds during implementation.
- Develop and review with the available environment: static analysis, portable compilation, fakes and focused non-Windows tests.
- GitHub-hosted Windows fast compile/tests may run automatically on relevant pushes; physical Windows/RTX validation stays deferred to explicit hardware gates.
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
Validated code head: 47536ba

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

Phase 09: CLOSED.
Phase 10: CLOSED.
Phase 11: IN PROGRESS.

Source-size vendor handling:
- tools/check_source_size.py exempts shaders/vendor/optiscaler_dlssnr/dlssnr.hlsl only while its Git blob SHA matches the locked upstream vendor blob;
- any byte change to that shader removes the exemption automatically and restores the 300-line violation.

Remaining global gate:
- real-game execution and physical provider/Host64/patcher cutover.

Phase 09 closure:
- hosted portable and Windows fast validation PASS;
- physical x64 D3D11 Acquire/bridge/hook gate PASS on real hardware;
- x64 hook is isolated from the legacy x86 CaptureD3D11 monolith;
- D3D11 carrier files touched in Phase 09 remain <=300 lines.

Phase 10 subgates 10a/10b:
- CaptureProvider32 split into connection/config/HANDLE ownership and pipelined frame/ack owners;
- IPC v3 carries explicit connectionGeneration and validates generation + session on both sides;
- Host64 guide upload no longer waits on the CPU; resize only reuses retired guide work;
- focused Windows gate now compiles Host64 IPC translation units without linking the full core;
- portable and Windows hosted validation PASS at de62b05.

Phase 10 closure:
- review findings for Host64 GPU lifetime, fence ordering, guide lifetime, bounded setup IO, reconnect OVERLAPPED reuse, partial IO and stale HANDLE ownership are fixed;
- portable and Windows hosted validation PASS;
- physical Win32 -> x64 Host64 gate PASS after real host restart, fresh connectionGeneration, N/N-1, Neural 1.0 and reduced 0.5;
- Capture/Host owners touched in Phase 10 remain <=300 lines.

Phase 11 subgate 11a:
- portable VulkanCarrierContract added with explicit memory, handle, layout, queue and timeline ownership evidence;
- SyntheticVulkanProvider split from 377 lines into 146-line orchestration and 224-line interop/sync/commands owners;
- fake-handle Vulkan success paths removed; without a real backend the provider fails closed;
- ipc_host_test reduced to 248 lines by removing simulated Vulkan interop evidence;
- patcher compatibility preserved without modifying the grandfathered patcher;
- portable CI PASS at 3566c14;
- Windows hosted run 35947607432 was still in progress at session freeze.

Phase 11 subgates 11b/11c:
- official Khronos Vulkan-Headers are pinned in Windows hosted validation without a full SDK install;
- VulkanContextContract carries instance, physical device, device, queue and queue-family identity;
- native owners compile against official Vulkan types; the public provider header no longer declares invented Vulkan ABI structs/constants;
- imported D3D12 memory selects memoryTypeIndex from image requirements intersected with vkGetMemoryWin32HandlePropertiesKHR;
- provider native lifecycle, interop and commands are split into owners <=300 lines;
- provider commands delegate to VulkanNativeCommands using official VkImageMemoryBarrier/VkImageCopy/VkImageBlit types;
- code head 47536ba passed portable and Windows hosted validation;
- real VkDevice/resource harness is implemented at 0470771 and portable validation PASS;
- Windows run 35952257874 for 0470771 was still active at session freeze.

Exact next action:
- inspect Windows run 35952257874 first; do not duplicate it;
- if green, run nrfusion_vulkan_native_device_tests on the notebook with NRFUSION_TEST_VULKAN_HARDWARE=1;
- treat that as native VkDevice/resource evidence only, separate from external-memory interop evidence;
- after the physical VkDevice gate, add the D3D12 shared-resource/fence import harness for external memory + timeline semaphore ownership;
- do not mark Phase 11 closed until acquire, interop, sync, compose/recreation and long-run evidence are all distinct and real.
