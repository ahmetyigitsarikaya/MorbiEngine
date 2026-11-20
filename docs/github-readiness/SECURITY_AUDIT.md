# Security Audit — Morbi Engine

> **Audit Date:** 2026-11-20  
> **Scope:** All C++, C#, TypeScript, CMake, documentation, configuration, and adjacent workspace directories.  
> **Redaction Policy:** Zero credential values are reproduced in this report.

---

## 1. Executive Summary

- **Core Morbi Engine (`Engine/`, `Editor/`, `Managed/`, `Samples/`, `Tests/`, `cmake/`, `docs/`):** **Zero** hardcoded API keys, tokens, passwords, private keys, or database connection strings were found.
- **Adjacent Separate Subproject (`hub_and_web_syss/`):** Contains **live credentials** in `hub_and_web_syss/api/.env` and a **hardcoded default admin password** in `hub_and_web_syss/api/cmd/seed/main.go`. This entire directory is now excluded in the root `.gitignore` so it cannot be accidentally committed to the Morbi Engine repository.

---

## 2. Detailed Findings

### [CRITICAL] Live Environment File with Credentials (`hub_and_web_syss/api/.env`)
- **Location:** `hub_and_web_syss/api/.env` (lines 2–9)
- **Types Detected:** Database connection string (`DATABASE_URL`), JWT signing secret (`JWT_SECRET`), Cloudflare R2 account/access/secret keys (`R2_ACCOUNT_ID`, `R2_ACCESS_KEY_ID`, `R2_SECRET_ACCESS_KEY`).
- **Git Exposure Status:** Repository has 0 commits yet; `.env` is ignored by `hub_and_web_syss/api/.gitignore` and the entire `/hub_and_web_syss/` directory is now excluded by the root `.gitignore`.
- **Action Required:** Never upload or archive `hub_and_web_syss/api/.env` publicly. If this workspace was ever synced to an external backup or cloud drive, rotate the database password, JWT secret, and Cloudflare R2 keys immediately.

### [HIGH] Hardcoded Default Admin Password (`hub_and_web_syss/api/cmd/seed/main.go`)
- **Location:** `hub_and_web_syss/api/cmd/seed/main.go`, line 19
- **Finding:** `flag.String("password", ..., "Admin password")` contains a hardcoded default password alongside default email `admin@morbiengine.dev`.
- **Action Required:** Require password input via environment variable or interactive prompt if `hub_and_web_syss` is published in its own repository.

### [MEDIUM] Machine-Specific Local Developer Paths (`C:\Users\<user>`)
- **Resolved During This Mission:**
  - Sanitized `AGENTS.md`, `docs/PHYSICS_GAMEPLAY_VALIDATION.md`, `docs/road/decisions/ADR-0003-unified-pipeline-layout.md`, and `docs/road/reports/phase-02/PHASE-02-WP03-UNIFIED-PIPELINE-LAYOUT.md`.
  - Removed 7 raw terminal log dumps in `docs/` (`build_output.txt`, `smoke_test_output.txt`, etc.), `Logs/Crashes/`, `scratch/`, and root `.pdb` files.
  - Added `.agents/ecc-install-state.json`, `Managed/**/obj/`, and `*.pdb` to `.gitignore`.

### [INFO] Benign Findings
- `ThirdParty/Tools/cmake/share/cmake-4.5/Templates/Windows/Windows_TemporaryKey.pfx` is CMake's standard public UWP template certificate (`/ThirdParty/Tools/` is also excluded in `.gitignore`).
- `legacy/FlueperEngine/` contains GPL-licensed reference projects (`Blender`) and proprietary Omniverse/PhysX SDK binaries; `/legacy/` is excluded in `.gitignore` and must never be committed to GitHub.

---

## 3. Repository Hygiene Controls Added

- Created comprehensive root `.gitignore` blocking `.env`, `*.pem`, `*.key`, `*.dmp`, `*.pdb`, `/hub_and_web_syss/`, `/legacy/`, `/testgameassets/`, `/NYCMultiplayerTest/`, `/ECC/`, `/ThirdParty/Tools/`, `/build/`, `/build-debug/`, and `.agents/ecc-install-state.json`.
- Created `SECURITY.md` with private vulnerability reporting guidance.
- Configured `.github/workflows/ci.yml` with least-privilege `permissions: contents: read` and zero hardcoded secrets.
