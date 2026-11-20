# Morbi Engine Documentation

> **Development Stage:** Early Development — Active Work in Progress. Documentation reflects verified repository implementations and clearly distinguishes implemented, in-progress, experimental, and planned systems.

## Architecture Documentation

- [**Architecture Overview**](architecture/OVERVIEW.md) — Layered system design, CMake module dependency graph, and runtime/editor/server lifecycles
- [**Engine Core, ECS, Networking & Scripting**](architecture/ENGINE_CORE.md) — `Morbi.Core`, `Morbi.IO`, `Morbi.Platform`, `Morbi.Scene`, `Morbi.Network`, and `.NET 10` `Morbi.Script`
- [**Graphics & RHI Architecture**](architecture/GRAPHICS_ARCHITECTURE.md) — `Morbi.RHI`, `Morbi.RHI.Vulkan`, `Morbi.RHI.DX12`, honest backend parity matrix, frame execution, and command submission
- [**Render Pipeline & Render Graph**](architecture/RENDER_PIPELINE.md) — `Morbi.Render`, linear `RenderGraph`, forward mesh & skinning passes, procedural HUD/Gizmo, and current limitations
- [**GPU Resource Lifetime & Synchronization**](architecture/GPU_RESOURCE_LIFETIME.md) — Generational handles, `UnsubmittedRefCount`, timeline semaphores/fences, and deferred destruction
- [**Shader Pipeline**](architecture/SHADER_PIPELINE.md) — In-process DXC dual-target compilation (DXIL + SPIR-V), V3 `.mshc` CRC32 cache, `SPIRV-Reflect` + DXIL reflection, and `PipelineLayoutBuilder`
- [**Animation System**](architecture/ANIMATION_SYSTEM.md) — Skeletal hierarchy, forward kinematics, zero-alloc CrossFade, Mixamo-to-CC3 retargeting, and 100-bone GPU skinning
- [**Asset Pipeline**](architecture/ASSET_PIPELINE.md) — Assimp/WIC/TGA importers, `.mAsset` v2 cooking, `MorbiPak` `.mpak` archives, and `AssetRegistry`
- [**Physics & Audio**](architecture/PHYSICS_AND_AUDIO.md) — Jolt Physics v5.2.0 simulation and SoLoud 3D spatial audio
- [**Editor, Runtime UI & Dedicated Server**](architecture/EDITOR_ARCHITECTURE.md) — Thin host `MorbiEditor.exe`, React 18 RCDATA embedding, WebView2 `SetWindowRgn`, `BinaryIPCBridge`, `MorbiServer`, and `ProjectBuilder`

## Development Guides

- [**Building Morbi Engine**](development/BUILDING.md) — Prerequisites, CMake presets, frontend & managed builds, and verified commands
- [**Dependencies & Licensing**](development/DEPENDENCIES.md) — Complete third-party dependency matrix and license obligations
- [**Project Structure**](development/PROJECT_STRUCTURE.md) — Repository layout and module responsibilities
- [**Development Guidelines**](development/DEVELOPMENT_GUIDELINES.md) — C++20, C# `.NET 10`, and React/TypeScript standards

## Engineering Governance & Existing Reference Docs

- [**Roadmap & Engineering Records (`docs/road/`)**](road/README.md) — Master roadmap, ADRs (`ADR-0001`–`ADR-0003`), status dashboard, backlog, and phase reports
- [**Physics Developer Guide**](PHYSICS_GUIDE.md) — Jolt Physics v5.2.0 coordinate system, C# API, joints, and character controller
- [**Third-Party Notices**](THIRD_PARTY_NOTICES.md) — Full open-source attributions and license notices

## Repository Readiness Reports (`docs/github-readiness/`)

- [Initial Repository State](github-readiness/INITIAL_REPOSITORY_STATE.md)
- [Project Discovery](github-readiness/PROJECT_DISCOVERY.md)
- [Repository Audit](github-readiness/REPOSITORY_AUDIT.md)
- [Cleanup Report](github-readiness/CLEANUP_REPORT.md)
- [Security Audit](github-readiness/SECURITY_AUDIT.md)
- [Dependency Audit](github-readiness/DEPENDENCY_AUDIT.md)
- [Validation Report](github-readiness/VALIDATION_REPORT.md)
- [Additional Work Packages](github-readiness/ADDITIONAL_WORK_PACKAGES.md)
- [Final Report](github-readiness/FINAL_REPORT.md)
