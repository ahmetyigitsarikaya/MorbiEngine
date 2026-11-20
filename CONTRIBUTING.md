# Contributing to Morbi Engine

Thank you for your interest in **Morbi Engine**!

> **Early Development Notice:** Morbi Engine is currently in its early development stage. Internal architectures, public APIs, build configurations, and module boundaries are actively evolving. Please open an issue or discussion before starting large architectural changes so work stays aligned with the current roadmap.

---

## How to Contribute

1. **Report Bugs:** Use the Bug Report or Graphics & Rendering Issue templates and include your OS, GPU, driver version, graphics backend (`Vulkan` / `DirectX12`), and clear reproduction steps.
2. **Propose Features:** Use the Feature Proposal template and describe the motivation, affected subsystems, and architectural tradeoffs.
3. **Improve Documentation:** Help keep architecture documents, build guides, and diagrams accurate against the actual codebase.
4. **Submit Code Changes:** Keep pull requests focused, small, and easy to review.

---

## Engineering Standards

Please review [`docs/development/DEVELOPMENT_GUIDELINES.md`](docs/development/DEVELOPMENT_GUIDELINES.md) before writing code. Key conventions:

- **C++20 Core:** Modern C++20, RAII ownership (`std::unique_ptr` / `std::shared_ptr`), zero raw owning pointers, `/W4` clean builds with zero warnings on MSVC.
- **C# Scripting (`.NET 10`):** `#nullable enable`, minimal allocations, `Span<T>` / `ReadOnlySpan<T>` for interop with native ECS arenas.
- **Editor Frontend (React 18 + TypeScript):** Strict TypeScript (`strict: true`, no `any`), functional components, Tailwind CSS utility classes.
- **Documentation Honesty:** Never document planned or experimental features as complete or production-ready. Update relevant architecture docs or ADRs (`docs/road/decisions/`) when modifying core designs.

---

## Pull Request Checklist

- [ ] Code builds cleanly with `cmake --build --preset release` (0 errors, 0 warnings).
- [ ] Frontend changes build cleanly with `npm run build` in `Editor/Frontend`.
- [ ] No generated build outputs, `.pdb`/`.exe` binaries, crash dumps, or machine-specific local paths are included.
- [ ] Documentation and diagrams are updated if public APIs, module boundaries, or build steps changed.
- [ ] Commit messages and PR descriptions clearly explain *what* changed and *why*.

---

## Licensing Note

Morbi Engine is licensed under the **[Morbi Engine Source-Available License (MESAL) v1.0](LICENSE)** (a custom source-available license, **not** an OSI-approved open-source license; subject to legal review prior to public release). Submitting patches or pull requests directly to the official repository is permitted under MESAL v1.0 Section 3.1; please confirm contributor ownership/licensing terms with the project maintainer before submitting substantial code contributions.
