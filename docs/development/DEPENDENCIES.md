# Dependencies & Licensing Reference

This document summarizes all verified third-party dependencies used by Morbi Engine across native C++, `.NET 10` managed scripting, and the React 18 Editor frontend. For full license texts and notices, see [`docs/THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).

---

## 1. Native C++ Dependencies (`cmake/MorbiDependencies.cmake`)

| Library | Version | Delivery | License | Consumer Module(s) |
|---|---|---|---|---|
| **Jolt Physics** | `v5.2.0` | Vendored (`ThirdParty/JoltPhysics`) + SHA-256 `FetchContent` fallback | MIT | `Morbi.Physics` |
| **Khronos SPIRV-Reflect** | `vulkan-sdk-1.4.363.0` | Vendored (`ThirdParty/SPIRV-Reflect`) | Apache-2.0 | `Morbi.Shader` |
| **ENet** | `v1.3.18` | Pinned SHA-256 `FetchContent` | MIT | `Morbi.Network` |
| **libsodium** | `1.0.20` (`9b2848df`) | Pinned Git `FetchContent` | ISC | `Morbi.Network` |
| **nlohmann/json** | `3.11.3` / `3.12.0` | Local or `FetchContent` | MIT | `Morbi.Core`, `IO`, `Scene`, `UI`, `Simulation` |
| **SDL3** | `3.2.0` | Local, `find_package`, or `FetchContent` | zlib | `Morbi.Platform` |
| **Vulkan Headers** | `1.4.356` / Vulkan SDK | Local or `find_package(Vulkan)` (`VK_NO_PROTOTYPES`) | Apache-2.0 OR MIT | `Morbi.RHI.Vulkan` |
| **DirectX Shader Compiler (DXC)** | Runtime DLL (`dxcompiler.dll`, `dxil.dll`) | `VULKAN_SDK` / `PATH` / `-DMORBI_DXC_DIR` | NCSA / UIUC (+ Apache-2.0 for `spirv-val`) | `Morbi.Shader` |
| **SoLoud** | `202002` | Optional local source (`WITH_MINIAUDIO`, `WITH_NULL`) | zlib/libpng | `Morbi.Audio` |
| **Assimp** (+ zlib) | `6.0.5` | Optional local static lib | BSD-3-Clause + zlib | `Morbi.Asset` |
| **WebView2Loader** | `1.0.4022.49` | Optional runtime DLL (`-DMORBI_WEBVIEW2_LOADER_PATH`) | BSD-3-Clause | `Morbi.UI` / `MorbiEditor` |
| **.NET CoreCLR `nethost`** | Host `.NET 10` SDK | Auto-discovered from `.NET` SDK packs | MIT / Microsoft .NET Library | `Morbi.Script` |

## 2. Managed C# Dependencies (`Managed/Morbi.Engine/Morbi.Engine.csproj`)

| Package | Version | License | Purpose |
|---|---|---|---|
| `Microsoft.CodeAnalysis.CSharp` | `5.12.0` | MIT | In-process Roslyn compilation of project C# scripts |

## 3. Editor Frontend Dependencies (`Editor/Frontend/package.json`)

- **Runtime:** `react` (`^18.3.1`, MIT), `react-dom` (`^18.3.1`, MIT)
- **Build / Dev:** `typescript` (`^5.6.3`), `vite` (`^5.4.11`), `@vitejs/plugin-react` (`^4.3.3`), `vite-plugin-singlefile` (`^2.3.4`), `tailwindcss` (`^3.4.15`), `postcss` (`^8.4.49`), `autoprefixer` (`^10.4.20`)

## 4. Project Licensing Status

Original Morbi Engine source code is governed by the **[Morbi Engine Source-Available License (MESAL) v1.0](../../LICENSE)** (a custom source-available license, **not** an OSI-approved open-source license; requires legal review prior to public release). MESAL v1.0 applies exclusively to original Morbi Engine works and does not alter, restrict, or supersede the licenses of any third-party libraries listed above or in [`docs/THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).
