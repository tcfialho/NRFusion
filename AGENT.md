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

Date: 2026-09-24 BRT
Branch: standalone/integration
Validated code head: fdec6dc
Current branch head before closure docs: fdec6dc

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
- real VkDevice/resource harness is implemented at 0470771;
- portable and Windows hosted validation PASS for 0470771, including source-size and native harness build/run.

Phase 11 subgate 11d WIP:
- D3D12 shared producer harness creates shareable RGBA16F textures plus producer/consumer shared fences and exposes adapter LUID/allocation size;
- Vulkan memory import now validates external-image format/tiling/usage support with vkGetPhysicalDeviceImageFormatProperties2;
- Win32 import handles are duplicated so caller ownership is preserved, and the duplicates are closed by the application after successful import per Vulkan Win32 ownership rules;
- memory import, semaphore/sync, native lifecycle and commands remain split into owners <=300 lines;
- external interop harness matches Vulkan and D3D12 devices by LUID, enables Win32 external-memory/semaphore plus timeline semaphore support, imports two textures and two fences, and tests producer-wait -> consumer-signal;
- latest link fix at 35b6272 adds only SyntheticDx12Provider.cpp + d3dcompiler to the focused external interop test target;
- portable run 35957702217 PASS, including source-size and source checkpoint artifact;
- Windows run 35957702242 PASS;
- local/notebook access is intentionally unavailable in this session; no physical Vulkan gate was attempted.

Phase 11 subgate 11e:
- external semaphore importability is validated with vkGetPhysicalDeviceExternalSemaphoreProperties before D3D12_FENCE import;
- Vulkan external image acquire/release uses explicit VK_QUEUE_FAMILY_EXTERNAL <-> local queue-family ownership barriers;
- external interop harness records acquire -> GPU copy -> release and 32 recreation cycles, but correctly reports CTest SKIP 77 on hosted runners without matching real external interop;
- Vulkan Acquire is now a separate portable seam: captured image facts + provenance/layout/usage/queue ownership become FrameContext without inferring facts from opaque handles;
- VulkanCarrierSession now uses NrSession/WorkTicket claim-submit-abandon semantics, matching the D3D12 carrier ledger model;
- Windows hosted validation PASS at e7be568;
- focused portable workflow now builds/runs Vulkan contract/acquire/session tests explicitly;
- focused portable run 35996493589 PASS: 18/18, source-size PASS and source checkpoint artifact uploaded;
- workflow duplicate checkpoint blocks were removed in b63d6f6;
- local/notebook access remained disabled; no physical Vulkan gate was attempted.

Phase 11 subgate 11f WIP:
- NrSession ledger now exposes claimed-execution inspection/consumption so Vulkan execution cannot create a parallel work ledger;
- Vulkan Acquire preserves explicit color/output native facts for later identity validation;
- VulkanCarrierExecutionPlan is portable and bound to VulkanCarrierWork + a pre-claimed WorkTicket;
- plan validates frame/work/runtime generation, resource identity, queue family, layout, usage, dimensions, format, provenance, ownership, recreation generation and compose intent;
- external interop plans carry explicit producer wait + consumer signal timeline contracts and external queue ownership;
- portable tests cover execution before claim, stale work, generation mismatch, queue/layout/usage/dimension/identity/provenance failures, duplicate claim consumption, abandon and valid local/external plans;
- new/modified implementation/test files checked manually are all <=300 lines;
- code commits: ea7098c, c12cb6c, d49c898;
- focused portable run 36012347260 PASS, including source-size and source checkpoint artifact nrfusion-source-d49c898b86e1617dbc025fcf714259e0420a7691;
- Windows hosted run 36012347315 PASS;
- local/notebook access remains disabled; no physical Vulkan gate was attempted.

Phase 11 subgate 11g WIP:
- VulkanCarrierExecutor now receives caller-owned VkCommandBuffer and records commands only;
- executor validates execution plan, native context, command buffer, dispatch, image presence and queue-family identity before consuming the claim;
- claimed execution is consumed exactly once immediately before recording; validation failures leave the claim available, duplicate execution fails closed;
- local resources record layout transition -> copy/blit -> layout restore;
- external resources record VK_QUEUE_FAMILY_EXTERNAL acquire -> copy/blit -> external release;
- VulkanNativeCommands copy/blit now receives explicit source/destination layouts instead of assuming GENERAL;
- producer wait / consumer signal contracts remain in the execution plan for caller-owned submission; executor does not submit queues or wait;
- dedicated Windows test covers invalid command buffer, one-shot claim consumption, explicit transfer layouts, duplicate execution and external recording;
- no vkQueueWaitIdle, vkWaitForFences, CPU readback, SyntheticDx12Provider fallback or patcher expansion was added;
- new/modified handwritten files remain <=300 lines;
- code commit 92411f1 initially exposed a target-link composition failure only: executor object leaked into the external interop object library;
- fix 9958475 isolates the executor to its dedicated target;
- focused portable run 36023547892 PASS with source checkpoint artifact nrfusion-source-9958475155e0f9c0000f9f20f8a8973770842bfd;
- Windows hosted run 36023547968 PASS, including nrfusion_vulkan_carrier_executor_tests;
- local/notebook access remains disabled; no physical Vulkan gate was attempted.

Phase 11 subgate 11h — hosted implementation freeze:
- nrfusion_vulkan_carrier_device_tests uses real VkImage/VkCommandBuffer objects when a usable Vulkan runtime exists and runs 32 create -> initialize-layout -> executor -> caller-submit/wait -> destroy cycles;
- Acquire now carries explicit resourceGeneration; execution rejects recreation-generation mismatch before recording;
- compose-back is Vulkan-native copy/blit to the caller output image, with caller-owned resource recreation and no DX12 fallback;
- external sync failure coverage includes missing producer handle and wrong consumer direction;
- external interop harness now runs 32 full recreation cycles plus 128 submissions reusing the same imported images and timeline semaphores;
- focused portable run 36035658289 PASS with source checkpoint artifact nrfusion-source-a4593eb934027f8c3ff9572b22f43ae0e9647f40;
- Windows hosted run 36035658103 PASS overall;
- on that Windows run, nrfusion_vulkan_native_device_tests and nrfusion_vulkan_carrier_executor_tests PASS;
- nrfusion_vulkan_carrier_device_tests and nrfusion_vulkan_external_interop_tests correctly report SKIP 77 on hosted Windows, so they are not physical runtime evidence;
- all files substantively touched in this subgate remain <=300 lines;
- local/notebook access remains disabled, so no physical Vulkan carrier or D3D12<->Vulkan external-memory/semaphore runtime gate was executed.

Phase 11 subgate 11i — physical gate orchestration ready:
- tools/validate_phase11_hardware.ps1 configures an x64 build and compiles only nrfusion_vulkan_carrier_device_tests plus nrfusion_vulkan_external_interop_tests;
- the script sets NRFUSION_TEST_VULKAN_HARDWARE=1 and NRFUSION_TEST_VULKAN_EXTERNAL_HARDWARE=1;
- it executes both binaries directly instead of through CTest, so exit code 77 cannot be accepted as a skipped-success physical gate;
- optional -VulkanHeaders points to a directory containing vulkan/vulkan.h; otherwise normal Vulkan SDK/environment discovery is used;
- the script restores the previous NRFUSION_VULKAN_HEADERS value and both hardware flags after execution;
- hosted Windows workflow now parses the Phase 11 physical script on every relevant push;
- Windows hosted run 36053130693 PASS, including PowerShell parser validation and normal Windows regression coverage;
- validate_phase11_hardware.ps1 is 76 lines and stays under the 300-line limit;
- no physical gate was executed because local/notebook access remains disabled.

Phase 11 remains IN PROGRESS.
Hosted/portable implementation and the physical gate entrypoint are frozen; closure now depends only on physical Vulkan evidence.

Exact next action when local access is explicitly re-enabled:
- fast-forward the physical checkout to origin/standalone/integration;
- run tools/validate_phase11_hardware.ps1 with persistent logging outside the worktree;
- require both binaries to return exit code 0, including the 32 recreation + 128 reuse external cycles;
- if the script PASSes, record Phase 11 closure; otherwise collect logs and fix via GitHub before rerunning.


Phase 12 subgate 12a WIP:
- Phase 12 started under the explicit Phase 11 physical-hardware blocker; Phase 11 remains IN PROGRESS and is not reclassified as closed;
- SyntheticOpenGlProvider was split so GL/D3D12 shared-resource import/copy/sync lives in SyntheticOpenGlProviderInterop.cpp;
- SyntheticOpenGlProvider.cpp is now orchestration-only and no longer waits on the CPU for a busy slot; it scans the fixed in-flight ring and fails closed when no slot is retired;
- Initialize now requires GraphicsApi::OpenGL, a current HGLRC and the required resolved interop entry points before creating the private D3D12 side;
- the prior headless test no longer treats private-D3D12 initialization plus texture id 1 as OpenGL interop evidence;
- nrfusion_synthetic_opengl_test is part of the Windows fast gate and PASSed at code head 875e000;
- portable OpenGlCarrierAcquire now requires explicit current-context identity, resource generation, memory-object/semaphore Win32 capability facts, copy-image capability, RGBA16F Texture2D facts and provenance/reliability;
- ProviderPolicy selects Synthetic for OpenGL only when RuntimeCapabilities.openGlCarrier is explicitly true; D3D12/D3D11/Vulkan synthetic capabilities cannot unlock the route;
- new dedicated portable tests cover OpenGL Acquire fail-closed behavior and ProviderPolicy gating without modifying the grandfathered >300-line game_probe_tests.cpp;
- initial portable compile failure at ce244cd was a missing SyntheticProvider.hpp include only; fixed at 35af18c;
- focused portable run 36060926802 PASS with source checkpoint artifact nrfusion-source-35af18c381695413ffe61b9703f25bb38e473322;
- Windows hosted run 36060926786 was still in progress at session freeze;
- all new/substantively modified handwritten files remain <=300 lines.

Phase 12 subgate 12b:
- previous Windows run 36060926786 completed PASS before this subgate;
- OpenGlCarrierSyncIdentity binds each workId to an explicit slot plus GL-input, D3D12-output and GL-release fence values;
- GL_EXT_memory_object, GL_EXT_memory_object_win32, GL_EXT_semaphore and GL_EXT_semaphore_win32 must be advertised, not merely expose non-null PFNs;
- glSemaphoreParameterui64vEXT programs GL_D3D12_FENCE_VALUE_EXT before every imported D3D12-fence signal/wait operation;
- input path is GPU-only: GL copy -> GL fence signal -> ID3D12CommandQueue::Wait -> D3D12 work -> queue Signal;
- output path is GPU-only: GL wait on D3D12 completion -> GL copy -> GL release signal;
- each in-flight slot now owns its own shared ID3D12Fence/HANDLE, so an unrelated slot cannot advance retirement state;
- slot reuse requires the GL release value to be visible on that slot's fence; missing output consume leaves the slot occupied and fails closed;
- Shutdown cleans partially initialized D3D12/fence state even when Initialize never reached ready_;
- initial Windows compile failure at 3e10c91 was one stale SharedSlot::workId reference; fixed in 6ae7647;
- static review then found the cross-slot shared-fence retirement hazard; fixed structurally in 1c4eff5;
- focused portable run 36074539887 PASS, including source-size and source checkpoint nrfusion-source-1c4eff5f9cee76dd3a88d65dfcfe339c47191c3f;
- Windows hosted run 36074539924 PASS; nrfusion_synthetic_opengl_test PASS and all 10 targeted tests PASS;
- touched OpenGL files remain <=300 lines: provider header 220, lifecycle 205, interop 241, orchestration 142, sync header 54, sync test 51;
- no real WGL external-object runtime evidence has been claimed and IntegratedCapabilities().openGlCarrier must remain false.

Phase 12 subgate 12c WIP:
- real WGL-context harness creates a hidden CS_OWNDC window/context and fails closed when required GL_EXT external-object entry points are unavailable;
- production provider now queries GL_DEVICE_LUID_EXT and creates its private D3D12 device on the matching DXGI adapter instead of using the default adapter;
- D3D12_RESOURCE imports pass size=0 for broader compatibility, matching the Khronos EXT_external_objects_win32 guidance;
- harness creates real shared RGBA16F D3D12 input/output resources plus a shared D3D12 fence and imports them into GL memory/semaphore objects;
- validation path is GL texture copy -> GL fence signal -> D3D12 queue wait/copy -> D3D12 fence signal -> GL wait/copy -> GL release signal;
- validation-only CPU readback checks the copied RGBA16F pattern; no CPU pixel path was added to production runtime;
- harness contains 32 resource/resize recreation cycles, 128 reuse cycles and a full WGL context close/reopen cycle;
- tools/validate_phase12_hardware.ps1 builds only the OpenGL external interop target, sets NRFUSION_TEST_OPENGL_HARDWARE=1 and executes the binary directly so SKIP 77 cannot count as physical PASS;
- cmake/NRFusionWindows.cmake was reduced to 293 lines by moving OpenGL Windows targets into cmake/NRFusionOpenGlWindows.cmake;
- all new/substantively modified handwritten files are <=300 lines; largest touched OpenGL file is SyntheticOpenGlProviderLifecycle.cpp at 263 lines;
- wglGetProcAddress sentinel values 1/2/3/-1 are now rejected fail-closed by the production loader;
- code sequence: ad639e3 harness, ef726da source split, 043c414 WGL sentinel hardening;
- focused portable run 36076073284 and Windows run 36076073277 were still in progress at session freeze.

Phase 12 subgate 12d — production result publication:
- hosted 12c validation closed green: focused portable 36076073284 PASS and Windows 36076073277 PASS; the real external-object harness correctly SKIPped 77 on hosted Windows;
- SyntheticOpenGlProvider::Submit now prepares the GPU input only and does not signal output-ready;
- GetD3D12Work(handle) exposes the work-bound private D3D12 device/queue plus lowColor and lowNeuralOut resources so the caller can execute the real model on the provider queue;
- PublishD3D12Result(handle) validates the exact outer/inner slot identity, extracts residual from the caller-written lowNeuralOut, composes the final native-resolution result into the GL-shared output resource, submits that publish pass and only then signals output-ready;
- Poll, GetResidual and RecordOpenGlOutputConsume all fail closed before output publication;
- shared resource naming now distinguishes final D3D12/GL output from the inner lowResidual;
- OpenGL outer slot selection follows SyntheticDx12Provider::NextSlotForSubmit so an out-of-order outer retirement cannot overwrite a still-live inner ring slot;
- resize/recreation fails closed while any prior GL release value is not retired; no CPU wait was added to the normal path;
- each slot has a separate publish allocator so publishing cannot reset an allocator still used by input preparation;
- physical harness now adds 32 cycles through the actual SyntheticOpenGlProvider: GL source -> provider Submit -> identity-model D3D12 copy into lowNeuralOut -> PublishD3D12Result -> GL output consume -> validation readback;
- focused portable run 36077955388 PASS, source-size PASS, source checkpoint nrfusion-source-fdec6dcae9a6de16137c45c7c90ba9d1c2800d94;
- Windows hosted run 36077955377 PASS; nrfusion_synthetic_opengl_test PASS and nrfusion_opengl_external_interop_tests SKIP 77 as expected without required real external-object support;
- all substantively modified files remain <=300 lines.

Phase 12 remains IN PROGRESS only because the real WGL/D3D12 external-object gate has not executed on physical hardware.
Hosted/portable implementation is frozen.

Capability note:
- GameProbe::IntegratedCapabilities() describes hooks actually shipped by the patcher/distribution, not standalone carrier capability;
- no OpenGL hook is shipped today, so IntegratedCapabilities().openGlCarrier must remain false even after a future Phase 12 physical PASS;
- physical Phase 12 PASS qualifies the carrier implementation; advertised/shipped OpenGL support belongs to the later real hook/cutover gate.

Exact next action when local access is explicitly re-enabled:
- fast-forward the physical checkout to origin/standalone/integration;
- run tools/validate_phase12_hardware.ps1 with persistent logging outside the worktree;
- require direct exit code 0, including 32 transport recreation cycles, 128 transport reuse cycles, full WGL context recreation and 32 production-provider publish cycles;
- if PASS, record Phase 12 carrier closure while leaving IntegratedCapabilities().openGlCarrier=false until an actual shipped OpenGL hook/cutover exists;
- if FAIL, collect logs and fix via GitHub before rerunning.

Phase 13 subgate 13a — route contract:
- Phase 13 started under the explicit physical blockers for Phases 11 and 12; neither prior phase is reclassified as closed;
- repository audit found no existing D3D10 runtime owner, hook or bridge; D3D10 is currently detection-only and explicitly unsupported;
- official DXGI sharing semantics require treating D3D10.1 shared handles as legacy non-NT handles; they are suitable for D3D10/D3D11 OpenSharedResource but not as the NT-handle contract expected by D3D12 OpenSharedHandle;
- the qualified proof route is therefore D3D10.1 keyed-mutex shared surface -> D3D11 legacy OpenSharedResource -> GPU copy into a D3D11.1 NT-shared surface -> D3D12 OpenSharedHandle;
- compose-back is the reverse bridge and requires two more full-frame GPU copies, for an explicit worst-case total of two inbound and two outbound copies;
- D3D10.1/D3D11 synchronization is required to use keyed mutex AcquireSync with timeout 0 so backpressure fails closed instead of blocking the CPU;
- D3D11/D3D12 synchronization should reuse the existing fence bridge rather than creating another policy/ledger;
- portable D3D10CarrierRoute contract encodes D3D10.1 availability, same-adapter requirement, RGBA16F Texture2D evidence, legacy shared surface, keyed mutex, zero-timeout policy, D3D11 legacy open, D3D11 NT share, D3D12 NT import, GPU copies and compose-back;
- valid route reports exactly 2 inbound + 2 outbound full-frame copies;
- code commit e69731c;
- focused portable run 36078628140 PASS with source checkpoint nrfusion-source-e69731c9a52f56ebcafdb94a50e4659602fc04cb;
- Windows hosted run 36078628160 PASS;
- new route files are 63/80/94 lines and remain under the 300-line cap.

Phase 13 subgate 13b — cross-API micro-harness:
- code head 8a23d4c;
- dedicated Windows harness creates D3D10.1, D3D11.1 and D3D12 on the same non-software DXGI adapter;
- D3D10 creates legacy keyed-mutex shared RGBA16F input/output surfaces; D3D11 opens them via OpenSharedResource;
- the D3D11->D3D12 stage uses a D3D11.1 Texture2D created with SHARED_NTHANDLE | SHARED_KEYEDMUTEX, obtains an NT handle with IDXGIResource1::CreateSharedHandle, and opens it on D3D12;
- D3D10.1/D3D11 keyed mutex acquisitions use timeout 0 and accept only S_OK, so WAIT_TIMEOUT cannot be misclassified as success;
- D3D11/D3D12 ordering reuses D3D11D3D12FenceBridge;
- the identity proof performs exactly four carrier full-frame GPU copies per roundtrip: D3D10 source -> legacy input, D3D11 legacy input -> NT bridge, D3D11 NT bridge -> legacy output, D3D10 legacy output -> destination;
- D3D12 opens and participates in the queue handoff but the proof deliberately uses an identity/no-op model, so there is no fifth model copy hidden in carrier accounting;
- validation-only D3D10 staging copy + Map verifies the RGBA16F payload and is excluded explicitly from carrierCopies_;
- hosted harness executes 64 reuse roundtrips plus 16 resource/resize recreation roundtrips;
- first Windows run 36080242669 failed after successful compile because D3D11 NT texture was created without SHARED_KEYEDMUTEX;
- diagnostic run 36080655085 isolated that creation failure; official CreateSharedHandle semantics require SHARED_NTHANDLE + SHARED_KEYEDMUTEX;
- run 36081014555 then reached payload validation and exposed missing keyed ownership around the D3D11 NT resource;
- final fix 8a23d4c honors that mutex around the two D3D11 access windows while D3D12 access remains ordered by shared fences;
- focused portable run 36081364306 PASS, including source-size;
- Windows hosted run 36081364289 PASS; nrfusion_d3d10_external_bridge_tests executed and Passed (not SKIP);
- source checkpoint nrfusion-source-8a23d4c3a3879915fe24b575ab524e2a7c810b63;
- tools/validate_phase13_hardware.ps1 builds only this target, sets NRFUSION_TEST_D3D10_HARDWARE=1 and executes it directly so SKIP 77 cannot become physical PASS;
- all new/substantively modified files remain <=300 lines; cmake/NRFusionWindows.cmake is 294 lines.

Phase 13 remains IN PROGRESS.
The route is runtime-demonstrated on hosted Windows, but the physical GPU/driver gate and explicit sync/CPU measurement remain open.

Exact next action when local access is explicitly re-enabled:
- fast-forward the physical checkout to origin/standalone/integration;
- run tools/validate_phase13_hardware.ps1 with persistent logging outside the worktree;
- require direct exit code 0 for 64 reuse + 16 recreation cycles and exact four-copy carrier accounting;
- collect CPU/sync timing for the physical run, keeping validation-only readback separate from steady-state carrier cost;
- if the physical gate and measurement pass, record Phase 13 closure; otherwise collect logs and fix through GitHub before rerunning.
