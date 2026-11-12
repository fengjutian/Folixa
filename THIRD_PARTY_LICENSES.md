# Third-party licenses

This file is an initial dependency register, not a substitute for the license
texts shipped with a release. Exact versions, transitive dependencies and
notices must be generated and reviewed for every release.

| Component | Version / source | Planned use | License review status |
|---|---|---|---|
| Qt | 6.8.3 (`mingw_64`), official binary kit via `aqtinstall` | Desktop UI, networking and local IPC | LGPL v3, dynamically linked; distribute the Qt binaries and license notices alongside the application |
| MinGW-w64 | GCC 13.1.0 (`qt.tools.win64_mingw1310`) | Build-time toolchain only | GPLv3 with GCC Runtime Library Exception; runtime libraries come from the Qt kit |
| PDFium | Pending exact revision | Rendering and text extraction | Pending exact revision and transitive notice generation |
| SQLite | Pending version pin | Metadata and FTS5 | Pending version pin |
| OpenSSL | Pending version pin | Cryptography and signatures | Pending version pin |
| FreeType | Pending version pin | Font processing | Pending version pin |
| HarfBuzz | Pending version pin | Text shaping | Pending version pin |
| ICU | Pending version pin | Unicode processing | Pending version pin |
| Tesseract | Pending version pin | Optional OCR | Pending version pin and trained-data review |

## Distribution strategy

- Qt and the MinGW runtime libraries are acquired by the build environment, never
  committed to the repository. `cmake/toolchains/qt-mingw.cmake` pins the exact
  versions used for the current release.
- `windeployqt` copies the Qt runtime libraries next to the executables; the
  release package must also ship the Qt license texts.
- v0.2.0 adds the reproducible dependency environment, the software bill of
  materials and the generated license notice bundle.

No Foxit binary, resource, icon, text, private protocol or other proprietary
artifact is a redistributable dependency of this project.
