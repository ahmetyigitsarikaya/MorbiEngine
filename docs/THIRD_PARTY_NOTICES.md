# Third-Party Notices

Morbi Engine uses and integrates the following third-party libraries and toolchains. The **Morbi Engine Source-Available License (MESAL) v1.0** ([`LICENSE`](../LICENSE)) applies exclusively to original Morbi Engine code and assets; it does **not** alter, restrict, or supersede the license of any third-party component listed below.

When distributing a Game Build built with Morbi Engine, you are responsible for complying with the license terms, copyright notices, and binary redistribution obligations of every third-party component included in that build.

---

## 1. Jolt Physics (`v5.2.0`)
- **Upstream:** https://github.com/jrouwe/JoltPhysics
- **Location:** `ThirdParty/JoltPhysics` (and pinned `FetchContent` fallback)
- **License:** MIT License (`ThirdParty/JoltPhysics/LICENSE`)
- **Copyright:** Copyright (c) 2021 Jorrit Rouwe

## 2. Khronos SPIRV-Reflect (`vulkan-sdk-1.4.363.0`)
- **Upstream:** https://github.com/KhronosGroup/SPIRV-Reflect
- **Location:** `ThirdParty/SPIRV-Reflect`
- **License:** Apache License 2.0 (`ThirdParty/SPIRV-Reflect/LICENSE`)
- **Copyright:** Copyright (c) 2017–2024 Google Inc. / The Khronos Group Inc.

## 3. ENet (`v1.3.18`)
- **Upstream:** https://github.com/lsalzman/enet
- **Delivery:** Pinned `FetchContent` (`cmake/MorbiDependencies.cmake`)
- **License:** MIT License
- **Copyright:** Copyright (c) 2002–2024 Lee Salzman

## 4. libsodium (`1.0.20`)
- **Upstream:** https://github.com/jedisct1/libsodium (CMake wrapper: https://github.com/robinlinden/libsodium-cmake)
- **Delivery:** Pinned Git `FetchContent` (`cmake/MorbiDependencies.cmake`)
- **License:** ISC License
- **Copyright:** Copyright (c) 2013–2024 Frank Denis

## 5. nlohmann/json (`v3.11.3` / `v3.12.0`)
- **Upstream:** https://github.com/nlohmann/json
- **License:** MIT License
- **Copyright:** Copyright (c) 2013–2025 Niels Lohmann

## 6. SDL3 (`v3.2.0`)
- **Upstream:** https://github.com/libsdl-org/SDL
- **License:** zlib License
- **Copyright:** Copyright (c) 1997–2025 Sam Lantinga

## 7. Khronos Vulkan Headers
- **Upstream:** https://github.com/KhronosGroup/Vulkan-Headers
- **License:** Apache License 2.0 OR MIT License
- **Copyright:** Copyright (c) 2015–2025 The Khronos Group Inc.

## 8. DirectX Shader Compiler (DXC) & SPIRV-Tools (`spirv-val`)
- **Upstream:** https://github.com/microsoft/DirectXShaderCompiler and https://github.com/KhronosGroup/SPIRV-Tools
- **License:** University of Illinois/NCSA Open Source License (DXC) and Apache License 2.0 (`spirv-val`)

## 9. SoLoud (`202002`)
- **Upstream:** https://github.com/jarikomppa/soloud
- **License:** zlib/libpng License (embedded codecs `miniaudio`, `dr_wav`, `stb_vorbis` under Public Domain / MIT-0)
- **Copyright:** Copyright (c) 2013–2020 Jari Komppa

## 10. Open Asset Import Library — Assimp (`v6.0.5`) & zlib
- **Upstream:** https://github.com/assimp/assimp
- **License:** BSD 3-Clause License (Assimp) and zlib License (zlib)
- **Copyright:** Copyright (c) 2006–2025 assimp team

## 11. Microsoft Edge WebView2 (`WebView2Loader.dll`)
- **Upstream:** https://developer.microsoft.com/en-us/microsoft-edge/webview2/
- **License:** BSD 3-Clause License
- **Copyright:** Copyright (c) Microsoft Corporation

## 12. .NET Runtime (`nethost` / `hostfxr`) & Roslyn (`Microsoft.CodeAnalysis.CSharp` `5.12.0`)
- **Upstream:** https://github.com/dotnet/runtime and https://github.com/dotnet/roslyn
- **License:** MIT License (Roslyn & open-source .NET components) / Microsoft Software License Terms (.NET Library for redistributed runtime binaries)
- **Copyright:** Copyright (c) .NET Foundation and Contributors / Microsoft Corporation

## 13. Editor Frontend Packages (React, TypeScript, Vite, Tailwind CSS)
- **Packages:** `react` (`18.3.1`), `react-dom` (`18.3.1`), `vite`, `vite-plugin-singlefile`, `tailwindcss`, `typescript`
- **License:** MIT License (React/React-DOM are bundled into `MorbiEditor.exe` via `vite-plugin-singlefile`)
- **Copyright:** Copyright (c) Meta Platforms, Inc. and affiliates (React) and respective package authors

## 14. Optional Export Toolchains (Not Committed to Git)
When using optional portable export toolchains (`ThirdParty/Tools/`):
- **CMake:** BSD 3-Clause License (Kitware, Inc.)
- **Zig:** MIT License (plus bundled LLVM/libc components under Apache-2.0 with LLVM Exception and respective licenses)
- **Portable .NET Runtime:** Microsoft Software License Terms (.NET Library)
