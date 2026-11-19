# Third-party licenses and dependency register

This file is the authoritative register of third-party components that are
either linked into the product, bundled in the package, or required at runtime.
Every row states an exact pin, the relationship to the product, the license
position, and the update path required by
[docs/DEPENDENCY_POLICY.md](docs/DEPENDENCY_POLICY.md) rule 1.

`scripts/verify-dependencies.ps1` parses this file and fails the build when a
pin drifts from the build scripts, when a row loses its version, license or
update path, or when a package ships a binary that is not registered here.

## Register

| Component | Exact version | Relationship | Use | License position | Update path |
|---|---|---|---|---|---|
| Qt | 6.8.3 `mingw_64` (`qtbase`, `qtsvg`, `aqtinstall`) | Direct, dynamically linked | Desktop UI, networking, local IPC, SVG icons | LGPL-3.0; binaries and notices shipped in the package | Bump `scripts/bootstrap-toolchain.ps1` and `cmake/toolchains/qt-mingw.cmake` in one commit, then run the full pipeline |
| MinGW-w64 | GCC 13.1.0 (`qt.tools.win64_mingw1310`) | Build-time toolchain and runtime DLLs | Compiler; `libgcc_s_seh-1`, `libstdc++-6`, `libwinpthread-1` are shipped | GPL-3.0 with GCC Runtime Library Exception | Follows the Qt kit; bump both bootstrap script and toolchain file together |
| PDFium | API 159 (`chromium/8152`) | Direct, bundled as `pdfium.dll` | PDF parsing, rendering, text and annotations | BSD-3-Clause; `pdfium.dll` plus 19 bundled license texts | Bump `Version`, `Sha256` and `ExpectedApiVersion` in `scripts/bootstrap-pdfium.ps1`, then rerun compat and fuzz acceptance |
| PDFium's bundled third parties | Fixed by `chromium/8152` (freetype, harfbuzz, ICU, libjpeg-turbo, libpng, zlib, lcms, abseil, agg23, fast_float, highway, libopenjpeg, simdutf, dragonbox, llvm-libc) | Transitive, inside `pdfium.dll` | Text shaping, fonts, images, compression | License texts shipped under `licenses/pdfium/` and verified against `licenses/pdfium-CHECKSUMS.txt` | Move only by upgrading PDFium; the checksum manifest must be regenerated in the same commit |
| Tesseract | Optional, discovered at runtime; not bundled | Optional external runtime dependency | OCR engine preferred when installed locally | Apache-2.0; not redistributed, so no license text is shipped | User-managed install (`AIPDF_TESSERACT_PATH` or `tools/tess/`); the app must keep working when it is absent |
| qpdf | 12.4.2 official `mingw64` release, pinned by `scripts/bootstrap-qpdf.ps1`; `qpdf30.dll` is bundled in every build | Required build and runtime dependency | PDF object layer: pre-save structural validation, object-level writes, font and page-graphics parsing | Apache-2.0; `licenses/Apache-2.0.txt` and `qpdf30.dll` are shipped with every package | Bump `Version`, `Asset` and `Sha256` in `scripts/bootstrap-qpdf.ps1`, rerun `scripts/verify-qpdf.ps1` and the full test suite |
| pyHanko / pyhanko-cli | pyHanko 0.38.0, pyhanko-cli 0.6.0, PyInstaller 6.16.0; full dependency lock in `scripts/pyhanko-requirements.txt` | Direct, bundled standalone executable | PKCS#12 signing, PAdES validation information, RFC 3161 timestamps and signature validation | pyHanko is MIT; Python and all bundled dependency license texts are shipped under `pyhanko/licenses/` | Update the requirements lock and the three bootstrap pins, rebuild the standalone runtime and rerun signature plus package gates |
| SCOWL word lists | SCOWL 2018.04.16, taken from the npm package `@neopass/wordlist@0.5.2` (SHA-256 `29f13d889586c51deea87531350d1c697411c1b69454231e90dbe9658116c541`) | Direct, compiled into the binary as `src/pdf/ops/spell/EnglishWords.h` and `EnglishCommonWords.h` | English spell-check dictionary (78 899 known words plus a 38 051-word common subset used for near-neighbour lookups) | SCOWL's own MIT-style permission: free to use, copy, modify and distribute provided the copyright notice appears in all copies and in the supporting documentation; Copyright 2000-2016 Kevin Atkinson | Update: regenerate with `src/pdf/ops/spell/generate_dictionaries.py` (it verifies the upstream SHA-256), then rerun `Wave21SpellTests` |
| jieba Chinese word list | `jieba/dict.txt` (SHA-256 `7197c3211ddd98962b036cdf40324d1ea2bfaa12bd028e68faa70111a88e12a8`) | Direct, compiled into the binary as `src/pdf/ops/spell/ChineseWords.h` and `ChineseCommonWords.h` | Chinese spell-check dictionary (45 476 words, of which 14 244 are the high-frequency subset) | MIT, Copyright (c) 2013 Sun Junyi; the copyright and permission notice are reproduced in `src/pdf/ops/spell/SOURCES.md` | Update: regenerate with `src/pdf/ops/spell/generate_dictionaries.py` (it verifies the upstream SHA-256), then rerun `Wave21SpellTests` |

Components that are **not** dependencies and therefore are not listed as pins:
SQLite and OpenSSL are not linked by this project (the local index is a plain
on-disk file format, and Qt uses the Windows TLS backend), while FreeType,
HarfBuzz and ICU are reached only through the PDFium pin above.

<!-- dependency-pins
qt=6.8.3
mingw=13.1.0
pdfium=chromium/8152
pdfium-api=159
pyhanko=0.38.0
pyhanko-cli=0.6.0
pyinstaller=6.16.0
-->

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
