# Development Guidelines

## 1. C++20 Engineering Standards (`Engine/Source/`, `Editor/src/`)

- **Ownership & Safety:** Use RAII (`std::unique_ptr`, `std::shared_ptr`, generational handles). Avoid raw owning `new`/`delete`.
- **Warning Discipline:** Code must compile with **0 errors and 0 warnings** under MSVC `/W4` (`cmake --build --preset release`).
- **Error Handling:** Never swallow errors silently. Return `Morbi::Core::Result<T, E>`, `std::optional`, or log explicitly with context.
- **Modularity & Size:** Keep functions focused (<50 LOC where practical), avoid deep nesting via early returns, and respect module dependency boundaries (e.g., `Morbi.Simulation` must never depend on `Platform`, `RHI`, `Render`, or `UI`).
- **GPU Lifetime Safety:** Any GPU resource used on an `ICommandList` must be tracked (`TrackBuffer`/`TrackTexture`/`TrackPipeline`/`TrackPipelineLayout`) and released through deferred destruction.

## 2. C# Scripting Standards (`Managed/Morbi.Engine/`)

- **Target Framework:** `.NET 10` (`net10.0`) with `<Nullable>enable</Nullable>` and `<AllowUnsafeBlocks>true</AllowUnsafeBlocks>` strictly limited to native interop boundaries.
- **Zero-Copy Interop:** Prefer `Span<T>`, `ReadOnlySpan<T>`, and blittable `struct` layouts verified against C++ `static_assert(sizeof(...))` checks.
- **Hot-Reload Safety:** Never hold unmanaged handles or static references that prevent `CollectibleScriptLoadContext` from unloading cleanly.

## 3. Editor Frontend Standards (`Editor/Frontend/`)

- **Strict TypeScript:** `strict: true`, no `any`, explicit interfaces for all component props and native bridge payloads.
- **React & Styling:** Functional components, custom hooks, and Tailwind CSS utility classes.
- **Single-File Embedding:** Ensure `npm run build` produces a self-contained `dist/index.html` compatible with `Editor/src/EmbeddedFrontend.rc`.

## 4. Documentation & Transparency Standards

- Every documented feature must reflect verified repository code and carry an honest maturity status (`Implemented`, `In Progress`, `Experimental`, or `Planned`).
- Record significant architectural decisions as ADRs under `docs/road/decisions/`.
- Never commit build outputs, `.pdb`/`.exe` binaries, crash dumps, `.env` files, or machine-specific local paths.
