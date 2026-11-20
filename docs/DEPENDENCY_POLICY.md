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

## Accepted change: PDF object layer (qpdf)

[QPDF_ADOPTION.md](QPDF_ADOPTION.md) is the full proposal: why the current
hand-written incremental writer is not enough, the alternatives compared, the
impact on build/packaging/SBOM/size, and the migration steps.

`qpdf` (Apache-2.0, 12.4.2 `mingw64`) is now an **accepted, required**
dependency: `AIPDF_WITH_QPDF=OFF` fails at configure time. The optional-build
path was removed because it let the suite stay green while the real write path
was never exercised.

Acceptance conditions met: license row in `THIRD_PARTY_LICENSES.md`, SBOM
entry, pinned version with SHA-256 in `scripts/bootstrap-qpdf.ps1`, and an
emergency update path for the PDF parser (rule 7).

## Accepted change: external runtime dependencies (Ghostscript, veraPDF, OFD converter, Tesseract)

Four components are **found at runtime and never bundled**. They are listed in
`THIRD_PARTY_LICENSES.md` with an exact version, the license position and an
update path, so rule 1 is satisfied even though nothing is shipped.

Two decisions are recorded here because they are easy to read the wrong way:

- **Ghostscript stays out.** It is AGPL-3.0, and rule 3 requires an explicit
  product and legal decision before a GPL-only dependency is adopted. That
  decision has not been made, so Ghostscript is never bundled and never
  redistributed. The optimiser still works without it — the structural tier
  comes from the qpdf build that is already linked in; what is missing without
  Ghostscript is the image re-encoding, font re-compression and `/ebook`
  down-sampling tier.
- **The OFD converter is driven, not re-implemented.** This project has no OFD
  reader. `app::OfdConverters` keeps the list of converters whose command line
  has been checked against upstream (their argument conventions differ), falls
  back to the generic `input output` convention for anything else, and checks
  the SHA-256 of the converter binary against the value recorded on first use.
  Because the app **executes** a user-supplied executable, that integrity check
  is what makes a replacement between runs visible to the user.

## Review record

The dependency table in `THIRD_PARTY_LICENSES.md` is updated whenever a new dependency is proposed. A dependency is production-approved only after its status includes an exact version and an accepted distribution strategy.

