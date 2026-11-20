# Phase 0 — Architecture Audit

**Status:** COMPLETE  
**Dependencies:** None

---

## Objective

Establish an evidence-based baseline audit of MorbiEngine's existing subsystems prior to executing Phase 1 and Phase 2 roadmap work.

## Scope

Audited subsystems:
- RHI (`Morbi.RHI`)
- Vulkan 1.3 Backend (`Morbi.RHI.Vulkan`)
- DirectX 12 Backend (`Morbi.RHI.DX12`)
- Renderer & RenderGraph (`Morbi.Render`)
- Shader Infrastructure (`Morbi.Shader`)
- Animation Infrastructure (`Morbi.Asset`, `Morbi.Scene`)
- Physics (`Morbi.Physics`, Jolt v5.2.0)
- Networking (`Morbi.Network`, ENet + libsodium)
- Asset Pipeline (`Morbi.Asset`, `Morbi.IO`)
- Runtime and Editor (`Morbi.Runtime`, `Editor/`)

## Acceptance Criteria

- Existing architecture, module boundaries, technical debt, and cross-backend divergences documented from direct source inspection.

## Completed Work & References

- [SHADER_RENDER_ANIMATION_AUDIT.md](../../SHADER_RENDER_ANIMATION_AUDIT.md) — Detailed technical audit of Shader, Render, and Animation systems.
- [PHYSICS_MATURITY_AUDIT.md](../../PHYSICS_MATURITY_AUDIT.md) — Physics architecture audit.
- [MORBIENGINE_OVERVIEW_AND_WORKFLOWS.md](../../MORBIENGINE_OVERVIEW_AND_WORKFLOWS.md) — Engine subsystem workflow reference.

## Known Limitations / Carried-Forward Findings

- `RenderGraph::CreateTransientBuffer` GPU buffer leak (`BL-RND-001`)
- Inline HLSL shaders and prototype scene rendering in `EngineRuntime.cpp` (`BL-SHD-006`)
- Regex-based `ShaderCompiler::ReflectSourceAndBinary` stub (`BL-SHD-004`)
