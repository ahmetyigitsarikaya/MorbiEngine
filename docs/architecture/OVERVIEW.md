# Morbi Engine — Architecture Overview

Morbi Engine is organized as **16 modular C++20 libraries**, a **`.NET 10` managed scripting bridge (`Managed/Morbi.Engine`)**, a **thin native Editor host (`MorbiEditor.exe`)** embedding a React 18 + TypeScript frontend, a **standalone game launcher template (`LauncherTemplate`)**, and a **headless dedicated server (`MorbiServer.exe`)**.

![Morbi Engine Layered Architecture](../assets/diagrams/morbi-architecture.svg)

---

## 1. Verified CMake Module Dependency Graph

The graph below reflects actual `target_link_libraries` dependencies in `CMakeLists.txt` and `Engine/Source/*/CMakeLists.txt`:

```mermaid
flowchart TD
    subgraph Hosts["Application Hosts"]
        Editor["MorbiEditor.exe\n(Dynamic LoadLibraryW)"]
        Launcher["LauncherTemplate\n(Exported Game EXE)"]
        Server["MorbiServer.exe\n(Headless Dedicated Server)"]
    end

    subgraph RuntimeLayer["Runtime & Simulation"]
        Runtime["Morbi.Runtime (SHARED)"]
        Sim["Morbi.Simulation (STATIC)"]
    end

    subgraph Domain["Domain Subsystems"]
        Scene["Morbi.Scene"]
        Physics["Morbi.Physics"]
        Audio["Morbi.Audio"]
        Network["Morbi.Network"]
        Asset["Morbi.Asset"]
        Script["Morbi.Script"]
        UI["Morbi.UI"]
    end

    subgraph Graphics["Graphics & Shaders"]
        Render["Morbi.Render"]
        Shader["Morbi.Shader"]
        RHI["Morbi.RHI"]
        Vulkan["Morbi.RHI.Vulkan"]
        DX12["Morbi.RHI.DX12 (Win32)"]
    end

    subgraph Foundation["Foundation"]
        Core["Morbi.Core"]
        IO["Morbi.IO"]
        Platform["Morbi.Platform"]
    end

    Editor -.->|"Runtime DLL load"| Runtime
    Launcher -.->|"Runtime DLL load"| Runtime
    Server --> Sim

    Sim --> Core & IO & Scene & Physics & Audio & Network & Script
    Runtime --> Core & IO & Platform & Shader & RHI & Vulkan & DX12 & Render & Scene & Physics & Audio & Network & Script & UI & Asset

    UI --> Core & IO & Platform & RHI & Render & Scene
    Script --> Core & Asset & Scene & Physics & Audio & Platform & UI
    Scene --> Core & IO & Asset
    Render --> Core & RHI & Vulkan & DX12 & Shader
    Vulkan --> Core & RHI
    DX12 --> Core & RHI
    Shader --> Core & RHI
    Asset --> Core & IO & RHI
    Physics --> Core & Scene
    Audio --> Core & IO & Scene
    Network --> Core
    Platform --> Core
    IO --> Core
```

---

## 2. Engine Lifecycle (Startup & Reverse-Order Shutdown)

### Editor & Standalone Game Lifecycle (`EngineRuntime::RunProject`)

```mermaid
sequenceDiagram
    participant Host as Thin Host (MorbiEditor / Launcher)
    participant RT as Morbi.Runtime (EngineRuntime)
    participant Sub as Core / Physics / Audio / Script
    participant GFX as Platform / RHI / RenderGraph
    participant UI as IPC / WebView2 / GameUI

    Host->>Host: Install SEH handler & CoInitializeEx
    Host->>RT: LoadLibraryExW("Morbi.Runtime.dll")
    Host->>RT: Morbi_Runtime_LaunchProject(--editor?)
    RT->>Sub: CrashHandler, PathService VFS, FileWatcher
    RT->>Sub: PhysicsWorld::Initialize, AudioWorld::Initialize
    RT->>Sub: ScriptEngine::Initialize (hostfxr + Roslyn compile)
    RT->>GFX: SDL3 Window::Create (+ viewportChildHwnd in Editor)
    RT->>GFX: RHI::CreateDevice(Vulkan / DX12) -> Initialize
    RT->>GFX: Compile inline HLSL via DXC -> PSOs & RenderGraph
    RT->>UI: BinaryIPCBridge, WebView2Compositor, GameUIRenderer
    RT->>RT: Load startup scene & enter main loop

    Note over RT,UI: Shutdown executes in strict reverse order
    RT->>GFX: rhiDevice->WaitIdle() & release RenderGraph pool
    RT->>UI: GameUI, WebView2, BinaryIPC Shutdown
    RT->>GFX: Destroy PSOs/buffers -> rhiDevice->Shutdown()
    RT->>Sub: Script, Audio, Physics, FileWatcher, Window Shutdown
    Host->>RT: FreeLibrary(Morbi.Runtime.dll)
```

### Headless Dedicated Server Lifecycle (`MorbiServer.exe`)

`MorbiServer` links **only** `Morbi::Simulation` (zero `Platform`, `RHI`, `Render`, or `UI` dependencies). `ServerMain.cpp` installs signal handlers, initializes `SimulationSession` with multi-world `WorldHost` instances (each owning a `Scene`, `PhysicsWorld`, and `SceneReplicationBridge`), runs `RunServerLoop`, and shuts down cleanly.

---

## 3. Known Architectural Debt (Tracked Honestly)

- **Monolithic `EngineRuntime.cpp` (~5,300 LOC):** Currently houses inline HLSL shader strings, editor command routing, smoke-test harness, rendering passes, and PIE management in a single file.
- **Duplicate Source Compilation in `Runtime/CMakeLists.txt`:** `SimulationSession.cpp` and `SceneReplicationBridge.cpp` are compiled into both `Morbi.Simulation` (STATIC) and `Morbi.Runtime` (SHARED).
- **Windows-Centric Platform Layer:** `RHI.DX12`, `WebView2Compositor`, `FileWatcher` (`ReadDirectoryChangesW`), `TextureImporter` (WIC), and `VulkanDevice` (`LoadLibraryA("vulkan-1.dll")`) currently assume Win32.
