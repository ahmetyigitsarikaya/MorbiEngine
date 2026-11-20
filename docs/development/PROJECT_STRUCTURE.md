# Project Structure

```text
MorbiEngine/
├── CMakeLists.txt                   # Root CMake project (16 engine modules, Managed, Editor, Samples, Tests)
├── CMakePresets.json                # Windows x64 Release and Debug presets
├── cmake/
│   ├── MorbiCompilerOptions.cmake   # C++20, MSVC /W4, optional AVX2, shared DLL export rules
│   ├── MorbiDependencies.cmake      # Vendored, FetchContent, and SDK dependency resolution
│   └── SyncDir.ps1                  # Windows robocopy helper for optional portable Tools sync
├── Engine/
│   └── Source/
│       ├── Core/                    # Types, Result, StringId, Log, SSE2 Math, TaskScheduler, CrashHandler
│       ├── IO/                      # PathService VFS, BinaryStream, FileWatcher, MorbiPak (.mpak)
│       ├── Platform/                # SDL3 Window & InputState
│       ├── RHI/                     # IDevice, ICommandList, PipelineLayoutValidator, NullDevice
│       ├── RHI.Vulkan/              # Vulkan 1.3 dynamic-loaded backend
│       ├── RHI.DX12/                # DirectX 12 backend (Win32)
│       ├── Shader/                  # In-process DXC, ShaderCache V3, SPIRV-Reflect, DXIL reflection
│       ├── Asset/                   # ModelImporter, TextureImporter, AnimationEvaluator, .mAsset
│       ├── Render/                  # RHIFactory & RenderGraph
│       ├── Scene/                   # Paged Sparse-Set SoA ECS, hierarchy, JSON scenes, PIE snapshots
│       ├── Physics/                 # Jolt Physics v5.2.0 wrapper (bodies, queries, joints, character)
│       ├── Audio/                   # SoLoud 2D/3D spatial audio
│       ├── Network/                 # ENet + libsodium AEAD, replication, interest management, RPCs
│       ├── Script/                  # .NET 10 CoreCLR hostfxr bridge & C ABI trampolines
│       ├── UI/                      # WebView2Compositor, BinaryIPCBridge, GameUIRenderer
│       └── Runtime/                 # Morbi.Simulation (static), Morbi.Runtime (shared), MorbiServer, LauncherTemplate
├── Managed/
│   └── Morbi.Engine/                # .NET 10 C# engine API, Roslyn compiler, Span<T> ECS bridge
├── Editor/
│   ├── CMakeLists.txt               # MorbiEditor thin host & frontend build integration
│   ├── src/                         # Win32 thin host (main.cpp) & EmbeddedFrontend.rc
│   └── Frontend/                    # React 18 + TypeScript + Tailwind CSS + Vite singlefile UI
├── Samples/
│   ├── Sandbox/                     # Default sample project (game.morbiproj, scenes, scripts, assets)
│   └── PhysicsShowcase/             # C# physics demonstration scripts
├── ThirdParty/
│   ├── JoltPhysics/                 # Vendored Jolt Physics v5.2.0
│   └── SPIRV-Reflect/               # Vendored Khronos SPIRV-Reflect
├── Tests/                           # Existing CTest suites (Physics, Network, RHI, Shader)
└── docs/
    ├── README.md                    # Documentation index
    ├── architecture/                # Subsystem architecture & Mermaid diagrams
    ├── development/                 # Build, dependency, structure, and coding guides
    ├── assets/                      # Custom SVG banners & architecture diagrams
    ├── road/                        # Master roadmap, ADRs, status, backlog, and phase reports
    └── github-readiness/            # Audit, cleanup, security, dependency, and validation reports
```
