# Security Policy

## Project Maturity & Scope

Morbi Engine is currently in its **early development stage** and is not yet intended for production deployment. Security hardening—including memory safety across native C++ boundaries, VFS/archive parsing (`MorbiPak`), networking (`Morbi.Network` AEAD/handshake), and `.NET` script sandboxing—is an ongoing engineering effort.

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub issues.**

If you discover a potential security issue (for example: buffer overflows in asset/network parsers, path traversal in `PathService` / `MorbiPak`, cryptographic flaws in `Morbi.Network`, or unsafe IPC/WebView2 handling):

1. Use **GitHub Private Vulnerability Reporting** (Security Advisories) on this repository if enabled, or contact the repository maintainer directly via their GitHub profile.
2. Include:
   - Affected subsystem and file paths
   - Steps to reproduce or minimal proof-of-concept
   - Potential impact assessment
   - Suggested remediation (if known)

## Disclosure Expectations

- Maintain confidentiality until the maintainer has had a reasonable opportunity to investigate and address the report.
- Never include real credentials, private keys, or personal data in issue reports or pull requests.
