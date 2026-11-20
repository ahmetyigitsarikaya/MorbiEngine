# MorbiEngine — Live Project Status Dashboard

**Last Updated:** 2026-11-18  
**Canonical Roadmap:** [MORBI_ENGINE_ROADMAP.md](MORBI_ENGINE_ROADMAP.md)

---

## Current Position

- **Current Phase:** Phase 2 — Shader & Pipeline Infrastructure (`IN PROGRESS`)
- **Last Completed Work Package:** Phase 2 WP3 — Unified Pipeline Layout (`COMPLETE`) — [Completion Report](reports/phase-02/PHASE-02-WP03-UNIFIED-PIPELINE-LAYOUT.md)
- **Next Work Package:** Phase 2 WP4 — Graphics Pipeline (`NOT STARTED` — **Awaiting Implementation Approval**; resolves `BL-RHI-002`)

---

## Phase Summary

| Phase | Name | Status |
|---|---|---|
| Phase 0 | Architecture Audit | COMPLETE |
| Phase 1 | RHI & GPU Resource Foundation | COMPLETE |
| Phase 2 | Shader & Pipeline Infrastructure | IN PROGRESS |
| Phase 3 | Render Architecture | NOT STARTED |
| Phase 4 | Modern Rendering Pipeline | NOT STARTED |
| Phase 5 | Animation Asset Pipeline | NOT STARTED |
| Phase 6 | Advanced Animation Runtime | NOT STARTED |
| Phase 7 | Scene / Animation / Renderer Integration | NOT STARTED |
| Phase 8 | Amber & Ramy Game Test | NOT STARTED |
| Phase 9 | Performance & Stability | NOT STARTED |
| Phase 10 | Networking Integration Test | NOT STARTED |

---

## Work Packages Breakdown (Active & Recent Phases)

### Completed WPs
- **Phase 1 WP1** — GPU Resource Lifetime (`COMPLETE`)
- **Phase 1 WP2** — Bindless Resource Reclamation (`COMPLETE`)
- **Phase 2 WP1** — Shader Compilation Infrastructure (`COMPLETE`)
- **Phase 2 WP2** — Binary Reflection (`COMPLETE`)
- **Phase 2 WP3** — Unified Pipeline Layout (`COMPLETE`)

### In-Progress WPs
- None (Phase 2 WP4 — Graphics Pipeline is gated on user approval).

### Blocked WPs
- None.

### Deferred Tasks (from Phase 1)
- **Phase 1 WP3** — PSO Cache (`DEFERRED` -> Phase 2 WP6; `BL-RHI-001`)
- **Phase 1 WP4** — Vertex Layout API (`DEFERRED` -> Phase 2 WP4; `BL-RHI-002`)
- **Phase 1 WP5** — Async Upload (`DEFERRED` -> Separate RHI backlog; `BL-RHI-003`)

### Open Backlog Items (See [BACKLOG.md](BACKLOG.md))
- `BL-PHY-001` (**Discovered during Debug testing**): `build-debug\bin\Debug\MorbiCSharpPhysicsTests.exe` crashes with `SEHException` in `Morbi.Engine.NativeBridge.SyncRigidBody` (`JointAndCompoundAuditDriver.OnStart()`, unrelated to `Morbi.Shader`/`Morbi.RHI`). Release `MorbiCSharpPhysicsTests` passes.
- `BL-SHD-007`: Verify Linux build and `libdxcompiler.so` runtime execution of `Morbi.Shader`, `MorbiShaderTests`, `MorbiShaderReflectionTests`, and `MorbiPipelineLayoutTests` on a Linux host.
- `BL-SHD-006`: WP7/Phase 3 inline HLSL extraction (`BL-SHD-005` resolved in WP3).
- `BL-RHI-004` (`PROPOSED`): Dynamic non-bindless descriptor set pool allocation & `BindDescriptorSet` API (extension point outside WP3 scope).
- `BL-IO-001`: `IO::FileWatcher` Linux `inotify` backend (needed before WP7 Linux parity).
- `BL-RND-001` / `BL-RND-002`: `RenderGraph` transient buffer leak & aliasing/DAG (Phase 3).

---

## Current Blockers & Environment Limitations

- **No blocker** on Phase 2 progression; awaiting user approval to begin **Phase 2 WP4 — Graphics Pipeline**.
- **Vulkan Validation Layers Unavailable on Host:** `VK_LAYER_KHRONOS_validation` is not installed on this machine. DX12 Debug Layer and DRED are active and passing in tests.
- **Linux Toolchain:** Unavailable on this Windows 11 host (`NOT VERIFIED` for Linux).

---

## Latest Verified Build & Test Baseline (2026-11-18, Post-WP3)

| Check | Configuration | Result | Exit Code | Evidence |
|---|---|---|---|---|
| `build\bin\Release\MorbiPipelineLayoutTests.exe` | Release | **128/128 assertions PASSED** | `0` | [WP3 Verification](test-results/phase-02/PHASE-02-WP03-VERIFICATION-2026-11-18.md) |
| `build-debug\bin\Debug\MorbiPipelineLayoutTests.exe` | Debug | **128/128 assertions PASSED** | `0` | [WP3 Verification](test-results/phase-02/PHASE-02-WP03-VERIFICATION-2026-11-18.md) |
| `build\bin\Release\MorbiShaderTests.exe` | Release | **181/181 assertions PASSED** | `0` | [WP3 Verification](test-results/phase-02/PHASE-02-WP03-VERIFICATION-2026-11-18.md) |
| `build-debug\bin\Debug\MorbiShaderTests.exe` | Debug | **181/181 assertions PASSED** | `0` | [WP3 Verification](test-results/phase-02/PHASE-02-WP03-VERIFICATION-2026-11-18.md) |
| `build\bin\Release\MorbiShaderReflectionTests.exe` | Release | **190/190 assertions PASSED** | `0` | [WP3 Verification](test-results/phase-02/PHASE-02-WP03-VERIFICATION-2026-11-18.md) |
| `build-debug\bin\Debug\MorbiShaderReflectionTests.exe` | Debug | **190/190 assertions PASSED** | `0` | [WP3 Verification](test-results/phase-02/PHASE-02-WP03-VERIFICATION-2026-11-18.md) |
| `ctest --test-dir build -C Release --output-on-failure` | Release | **8/8 PASSED** (49.89s) | `0` | [WP3 Verification](test-results/phase-02/PHASE-02-WP03-VERIFICATION-2026-11-18.md) |
| `ctest --test-dir build-debug -C Debug -E MorbiCSharpPhysicsTests --output-on-failure` | Debug (filtered) | **7/7 PASSED** (52.72s) | `0` | [WP3 Verification](test-results/phase-02/PHASE-02-WP03-VERIFICATION-2026-11-18.md) |
| `MorbiRHIDescriptorTests.exe` (Release & Debug) | Release & Debug | **860/860 assertions PASSED** | `0` | [WP3 Verification](test-results/phase-02/PHASE-02-WP03-VERIFICATION-2026-11-18.md) |
| `MorbiRHIGenerationalHandleHashTests.exe` (Release & Debug) | Release & Debug | **507/507 assertions PASSED** | `0` | [WP3 Verification](test-results/phase-02/PHASE-02-WP03-VERIFICATION-2026-11-18.md) |
| `MorbiEditor.exe --smoke-test --api vulkan` (Release & Debug) | Release & Debug | **PASSED** (Vulkan 1.3) | `0` | [WP3 Verification](test-results/phase-02/PHASE-02-WP03-VERIFICATION-2026-11-18.md) |
| `MorbiEditor.exe --smoke-test --api dx12` (Release & Debug) | Release & Debug | **PASSED** (DX12) | `0` | [WP3 Verification](test-results/phase-02/PHASE-02-WP03-VERIFICATION-2026-11-18.md) |
