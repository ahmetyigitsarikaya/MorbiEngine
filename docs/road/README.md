# MorbiEngine — Engineering Roadmap & Documentation Index

This directory (`docs/road/`) is the canonical engineering record for **MorbiEngine**. It tracks the master roadmap, live project status, chronological engineering history, technical backlog, architecture decisions, phase specifications, work package completion reports, test evidence, and new work proposals.

---

## Directory Layout

| Path | Purpose |
|---|---|
| [MORBI_ENGINE_ROADMAP.md](MORBI_ENGINE_ROADMAP.md) | Canonical living master roadmap with phase and WP statuses |
| [roadmap_rgm.md](roadmap_rgm.md) | Original attached master roadmap input (preserved as historical source) |
| [STATUS.md](STATUS.md) | Live project status dashboard (current phase/WP, blockers, verified baseline) |
| [CHANGELOG.md](CHANGELOG.md) | Chronological engineering log of inspections, implementations, bugs, and tests |
| [BACKLOG.md](BACKLOG.md) | Centralized tracker for deferred RHI tasks, defects, debt, and future work |
| [decisions/](decisions/README.md) | Architecture Decision Records (`ADR-XXXX-*.md`) |
| [phases/](phases/PHASE-02-SHADER-PIPELINE.md) | Per-phase objectives, scope, dependencies, and acceptance criteria |
| [reports/](reports/phase-02/PHASE-02-WP01-EXECUTION-PLAN.md) | Per-WP execution plans and completion reports |
| [test-results/](test-results/phase-01/PHASE-01-BASELINE-VERIFICATION-2026-11-18.md) | Recorded build, unit, smoke, and GPU validation test evidence |
| [proposals/](proposals/README.md) | Proposals for newly discovered WPs or Phases awaiting approval |

---

## Status Definitions

All Phase and Work Package statuses strictly use the following vocabulary:

- **NOT STARTED:** No implementation work has begun.
- **IN PROGRESS:** Implementation or required verification is ongoing.
- **BLOCKED:** Progress cannot safely continue because of an unresolved dependency or issue.
- **PROPOSED:** A new task or scope extension has been documented but not approved.
- **COMPLETE:** Requirements, tests, audit, and documentation have all been verified.
- **DEFERRED:** Work has intentionally been postponed with its reason documented.

---

## Related Repository Documentation

- [SHADER_RENDER_ANIMATION_AUDIT.md](../SHADER_RENDER_ANIMATION_AUDIT.md) — Phase 0/pre-Phase-2 source-level audit of Shader, Render, and Animation systems
- [MORBIENGINE_OVERVIEW_AND_WORKFLOWS.md](../MORBIENGINE_OVERVIEW_AND_WORKFLOWS.md) — High-level module architecture and runtime workflows
- [MORBIENGINE_DETAILED_FEATURES.md](../MORBIENGINE_DETAILED_FEATURES.md) — Detailed engine feature inventory
- [PHYSICS_MATURITY_AUDIT.md](../PHYSICS_MATURITY_AUDIT.md) & [PHYSICS_FINAL_VERIFICATION.md](../PHYSICS_FINAL_VERIFICATION.md) — Physics subsystem verification records
