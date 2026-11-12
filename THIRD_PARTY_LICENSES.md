# Third-party licenses

This file is an initial dependency register, not a substitute for the license
texts shipped with a release. Exact versions, transitive dependencies and
notices must be generated and reviewed for every release.

| Component | Version / source | Planned use | License review status |
|---|---|---|---|
| Qt | 6.8.3 (`mingw_64`), official binary kit via `aqtinstall` | Desktop UI, networking and local IPC | LGPL v3, dynamically linked; distribute the Qt binaries and license notices alongside the application |
| MinGW-w64 | GCC 13.1.0 (`qt.tools.win64_mingw1310`) | Build-time toolchain only | GPLv3 with GCC Runtime Library Exception; runtime libraries come from the Qt kit |
| PDFium | API 159 (`chromium/8152`), `bblanchon/pdfium-binaries` | PDF parsing and page rendering | BSD-3-Clause; `pdfium.dll` plus 19 third-party license texts are bundled in the package |
| SQLite | Pending version pin | Metadata and FTS5 | Pending version pin |
| OpenSSL | Pending version pin | Cryptography and signatures | Pending version pin |
| FreeType | Pending version pin | Font processing | Pending version pin |
| HarfBuzz | Pending version pin | Text shaping | Pending version pin |
| ICU | Pending version pin | Unicode processing | Pending version pin |
| Tesseract | Pending version pin | Optional OCR | Pending version pin and trained-data review |

## Distribution strategy

- Qt and the MinGW runtime libraries are acquired by the build environment, never
  committed to the repository. `cmake/toolchains/qt-mingw.cmake` pins the exact
  versions used for the current release, and `scripts/bootstrap-toolchain.ps1`
  installs them reproducibly.
- `scripts/package.ps1` copies the Qt runtime libraries and the platform plugin
  next to the executables, bundles this file and the project `LICENSE`, writes a
  per-package `NOTICE.txt`, and generates a CycloneDX 1.5 SBOM that records the
  SHA-256 and license of every shipped binary.
- The Qt kit also ships its own SPDX SBOM (`qtbase-<版本>.spdx.json`); it is
  copied into the package under `licenses/` and referenced from the generated
  SBOM.
- The SHA-256 of the packaged ZIP is written next to it as `.sha256`.
- v0.2.0 adds the reproducible dependency environment, the software bill of
  materials and the generated license notice bundle.

## Outstanding

- The project itself has no public license yet; `LICENSE` reserves all rights,
  so the current packages are internal development artifacts only.
- The license texts bundled in `licenses/` cover the components shipped today
  (Qt LGPL-3.0, MinGW-w64 GCC runtime). Components added in later versions
  must extend `licenses/CHECKSUMS.txt` in the same commit that introduces them.

No Foxit binary, resource, icon, text, private protocol or other proprietary
artifact is a redistributable dependency of this project.
