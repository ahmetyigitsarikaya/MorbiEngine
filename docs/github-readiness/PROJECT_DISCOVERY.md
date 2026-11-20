# Project Discovery — Morbi Engine

> **Verification Basis:** Direct inspection of source files, headers, `CMakeLists.txt`, `cmake/`, `Editor/`, `Managed/`, `Samples/`, and `ThirdParty/` on 2026-11-20.  
> **Development Stage:** Early Development — Active Work in Progress.

---

## 1. Project Overview

**Morbi Engine** is a general-purpose, modular C++20 3D game engine in active early development. It is structured around 16 native C++ modules, dual Vulkan 1.3 and DirectX 12 rendering backends, an in-process DXC dual-target shader pipeline (DXIL + SPIR-V) with binary reflection, a paged Sparse-Set SoA ECS, Jolt Physics v5.2.0, SoLoud spatial audio, an ENet + libsodium authenticated networking and replication stack with a headless dedicated server (`MorbiServer`), `.NET 10` CoreCLR + Roslyn hot-reloadable C# scripting, and an Edge WebView2 + React 18 desktop editor (`MorbiEditor`).

## 2. Verified Technology Stack

| Layer | Verified Technologies |
|---|---|
| **Languages** | C++20 (core engine & runtime), C# / `.NET 10` (`net10.0`, gameplay scripting), TypeScript 5.6 / React 18 (editor UI), HLSL SM 6.x (shaders) |
| **Build & Tooling** | CMake 3.24+, MSVC x64 (primary verified toolchain), Vite 5.4 + Tailwind CSS 3.4 + `vite-plugin-singlefile`, .NET SDK 10 + Roslyn (`Microsoft.CodeAnalysis.CSharp` 5.12.0), Portable CMake/Zig/Dotnet (game export) |
| **Graphics & Shaders** | Custom RHI (`Morbi.RHI`), Vulkan 1.3 (dynamic loading via `vulkan-1.dll`, `VK_NO_PROTOTYPES`), DirectX 12 (`d3d12`, `dxgi`, `ID3D12Device5`), In-process DXC (`dxcompiler.dll` / `dxil.dll`), Khronos `SPIRV-Reflect`, `IDxcContainerReflection` |
| **Platform & Windowing** | SDL3 v3.2.0, Win32 API (`HWND`, `SetWindowRgn`, `ReadDirectoryChangesW`, `DbgHelp`, `WIC`) |
| **Physics & Audio** | Jolt Physics v5.2.0, SoLoud 202002 (Miniaudio + null backend) |
| **Assets & Serialization** | Assimp v6.0.5 (FBX, glTF/GLB, OBJ), Windows Imaging Component (PNG/JPG/BMP/TIFF) + native TGA, `nlohmann/json`, custom `.mAsset` v2 & chunked `.mpak` (`MorbiPak`, ChaCha20/SHA-256) |
| **Networking** | ENet v1.3.18, libsodium 1.0.20 (X25519, Ed25519, XChaCha20-Poly1305 AEAD) |
| **Editor & Runtime UI** | Edge WebView2 (`WebView2Loader.dll`), React 18 + Tailwind CSS embedded as Win32 `RCDATA`, procedural HLSL `GameUIRenderer` |

> **Important Corrections vs. Assumed/Outdated Notes:**
> - Morbi Engine does **not** use `FlueCore`, `PhysX`, `RmlUi`, `ImGui`, `glslangValidator`, or `DirectX 12 Agility SDK`.
> - Managed scripting targets **`.NET 10`** (`net10.0`), not `.NET 8`.

## 3. Verified Repository Structure (Core Engine)

```text
MorbiEngine/
├── CMakeLists.txt                # Root CMake entry (16 Engine modules, Managed, Editor, Tests)
├── CMakePresets.json             # Windows x64 Release / Debug presets
├── cmake/                        # Compiler options, dependency resolution, sync script
├── Engine/Source/
│   ├── Core/                     # Logging, Math (SSE2), TaskScheduler, GenerationalHandle, CrashHandler
│   ├── IO/                       # VFS PathService, BinaryStream, FileWatcher, MorbiPak (.mpak)
│   ├── Platform/                 # SDL3 Window & InputState abstraction
│   ├── RHI/                      # Backend-agnostic RHI interfaces, PipelineLayout validator, NullDevice
│   ├── RHI.Vulkan/               # Vulkan 1.3 dynamic-loaded backend (bindless, timeline semaphores, slab allocator)
│   ├── RHI.DX12/                 # DirectX 12 backend (DRED, bindless SRV heap, Root Signatures)
│   ├── Shader/                   # In-process DXC compiler, V3 CRC32 cache, SPIRV-Reflect & DXIL reflection
│   ├── Asset/                    # Assimp/OBJ/glTF & WIC/TGA importers, AnimationEvaluator, .mAsset serializer
│   ├── Render/                   # RHIFactory & linear RenderGraph
│   ├── Scene/                    # Paged Sparse-Set SoA ECS, hierarchy, JSON scenes, PIE snapshots
│   ├── Physics/                  # Jolt Physics v5.2.0 (rigid bodies, compounds, queries, joints, character)
│   ├── Audio/                    # SoLoud 2D/3D spatial audio & procedural WAV synthesis
│   ├── Network/                  # ENet + libsodium AEAD, replication, interest management, RPCs
│   ├── Script/                   # .NET CoreCLR hostfxr host, 90+ C ABI trampolines, hot-reload
│   ├── UI/                       # WebView2Compositor, BinaryIPCBridge, procedural GameUIRenderer
│   └── Runtime/                  # Morbi.Simulation (static), Morbi.Runtime (shared), MorbiServer, LauncherTemplate
├── Managed/Morbi.Engine/         # .NET 10 C# engine API, Roslyn ScriptCompiler, Span<T> ECS bridge
├── Editor/                       # MorbiEditor thin host + React 18/TS/Vite frontend
├── Samples/                      # Sandbox project & PhysicsShowcase scripts
├── ThirdParty/                   # JoltPhysics v5.2.0, SPIRV-Reflect, portable export Tools
├── Tests/                        # Existing CTest suites (Physics, Network, RHI, Shader)
└── docs/                         # Roadmaps, ADRs, architecture & development documentation
```

## 4. Main Entry Points & Runtime Architecture

1. **Editor (`Editor/src/main.cpp` → `MorbiEditor.exe`):** Thin Win32 host that dynamically loads `Morbi.Runtime.dll` via `LoadLibraryExW` and invokes `Morbi_Runtime_LaunchProject` with `--editor`. Embeds the compiled single-file React frontend as Win32 `RCDATA`.
2. **Standalone Game (`Engine/Source/Runtime/LauncherTemplate/src/main.cpp`):** Exported via `ProjectBuilder`, dynamically loads `Morbi.Runtime.dll` without `--editor`.
3. **Dedicated Server (`Engine/Source/Runtime/Server/ServerMain.cpp` → `MorbiServer.exe`):** Links only the static `Morbi::Simulation` library (zero RHI, Render, Platform, or UI dependencies) and runs `SimulationSession` in a fixed-timestep multi-world loop.

## 5. Subsystem Maturity Classification

| Subsystem | Maturity | Evidence Summary |
|---|---|---|
| Core, IO, Platform | **Implemented** | Logging, work-stealing `TaskScheduler`, SSE2 math, generational pools, `MorbiPak` encryption, SDL3 window/input. Windows-centric. |
| Scene (ECS) | **Implemented** | Paged Sparse-Set SoA arenas, 11 component types, hierarchy, JSON save/load, PIE snapshots. |
| RHI Core & PipelineLayout | **Implemented** | Generational handles, `PipelineLayoutValidator`, `NullDevice`. Narrow pipeline state surface; no `BindDescriptorSet` on `ICommandList`. |
| Vulkan 1.3 Backend | **Implemented** | Dynamic rendering, timeline semaphores, custom 32 MB slab allocator, bindless Set 0. Known bugs documented in audit. |
| DirectX 12 Backend | **Implemented** | DRED breadcrumbs, bindless SRV heap, custom Root Signatures, deferred destruction. Partial parity with Vulkan (no general sub-allocator, no debug markers, no Agility SDK). |
| Shader Infrastructure | **Implemented** | In-process DXC dual-target (DXIL + SPIR-V), V3 `.mshc` CRC32 cache with transitive include hashing, SPIRV-Reflect + DXIL binary reflection, cross-backend validator. |
| Render Graph | **In Progress** | Linear pass execution, sequential barriers, name-based transient texture pooling. No DAG/topological sort/culling; memory aliasing is stats-only. |
| Scene Rendering & Lighting | **Experimental** | Forward mesh + 100-bone GPU skinning + wireframe physics debug work; lighting is hardcoded Blinn-Phong (`LightComponent` unused); GBuffer/Shadows/IBL/Post-process are dummy stubs. |
| Animation System | **Implemented** | Forward kinematics, zero-alloc CrossFade, sockets, Mixamo-to-CC3 retargeting, 100-bone GPU skinning, C# API. No state machines/blend trees/IK. |
| Physics (Jolt v5.2.0) | **Implemented** | Rigid bodies, compound shapes, queries, 4 joint types, `CharacterVirtual`, C# bindings. No mesh/convex-hull colliders yet. |
| Audio (SoLoud) | **Implemented** | 2D/3D spatial playback, procedural WAV. No audio streaming or mixing buses. |
| Asset Pipeline | **In Progress** | Assimp/WIC/TGA import, `.mAsset` v2 static mesh/texture cooking, `MorbiPak`. Skeletons/animations not cooked to `.mAsset`; no persistent incremental DB load. |
| Networking & Dedicated Server | **Implemented** | ENet + libsodium AEAD/X25519/Ed25519, delta/keyframe replication, interest management, RPC router, `MorbiServer`. |
| .NET 10 Scripting | **Implemented** | `hostfxr`, Roslyn in-process compiler, `CollectibleScriptLoadContext` hot-reload, zero-copy `Span<T>` ECS access. |
| Editor & Runtime UI | **In Progress** | Live React 18 WebView2 editor (`SetWindowRgn` hole-punching + JSON WebMessages) and procedural SDF `GameUIRenderer` HUD/Gizmo. `BinaryIPCBridge` and DirectComposition visual tree are partially wired; some viewport metrics are synthetic. |

## 6. Environmental & Platform Limitations

- **Verified Platform:** Windows 10/11 x64 with MSVC (C++20) only. Linux and macOS are **not** build-verified; `RHI.DX12`, Win32 `Window`/`FileWatcher`/`WebView2`/`WIC`/`DbgHelp`, and `LoadLibraryA("vulkan-1.dll")` are Windows-specific.
- **External Dependency Coupling (Pre-Cleanup):** Several dependencies (`SoLoud`, `Assimp`, `DXC`, `WebView2Loader.dll`) were initially resolved from local `legacy/FlueperEngine/` paths; decoupling and fallback discovery are tracked as part of this transformation and remaining work packages.
