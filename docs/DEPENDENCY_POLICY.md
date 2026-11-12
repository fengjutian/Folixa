# Dependency and licensing policy

## Rules

1. Every production dependency must have an owner, pinned version, source URL, license and update policy.
2. Dependencies are acquired by the build system or an approved package manager; binary files are not committed by default.
3. GPL-only dependencies require an explicit product and legal decision before adoption.
4. LGPL dependencies must be dynamically linked unless a reviewed relinking mechanism is provided.
5. Model weights, OCR trained data, fonts, icons and sample documents are dependencies and require the same review as code.
6. Release builds must generate a software bill of materials and bundle required notices and source offers.
7. Security-critical parsers and cryptographic libraries must have a documented emergency update path.
8. Third-party commercial application artifacts are research inputs only and must not enter product source, tests or releases.

## Review record

The dependency table in `THIRD_PARTY_LICENSES.md` is updated whenever a new dependency is proposed. A dependency is production-approved only after its status includes an exact version and an accepted distribution strategy.

