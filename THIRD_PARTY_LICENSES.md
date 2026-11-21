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
| Ghostscript | Optional, discovered at runtime; not bundled (not present on the build machine, so the real optimisation path has never run here) | Optional external runtime dependency | PDF optimisation and PDF/A-2b conversion (`app::DocumentConverter::plan` drives `gswin64c` / `gs`) | AGPL-3.0 — **never bundled and never redistributed**: rule 3 needs an explicit product and legal decision before a GPL dependency is adopted, and that decision has not been made | User-managed install; the app must keep working when it is absent (without it the optimiser falls back to the structural tier provided by the linked qpdf) |
| veraPDF | Optional, discovered at runtime; not bundled (a Java program; not present on the build machine) | Optional external runtime dependency | PDF/A compliance certification (`pdf::certifyPdfA` asks the local veraPDF for a verdict) | MPL-2.0; not redistributed, so no license text is shipped | User-managed install on `PATH` or in `C:/verapdf/bin` (see `pdf/compliance/VeraPdfChecker.cpp`); the app must keep working when it is absent — it then says so instead of pretending the file is compliant |
| OFD → PDF converter | Optional, discovered at runtime; not bundled (user-supplied; the supported list lives in `src/app/controllers/OfdConverters.cpp`) | Optional external runtime dependency | OFD → PDF batch conversion (`app::OfdBatchConverter`): this project **drives an external converter and does not implement an OFD reader** | Determined by whichever converter the user supplies. The two registered ones are `ofd2pdf` (upstream is based on Spire.PDF and states that Spire.PDF's licence was removed for copyright reasons — so it must not be redistributed) and `ofd-converter` (Apache-2.0). Neither is redistributed | User-managed install; the app records the SHA-256 of the converter it first ran and re-checks it on every later run, warning the user when the file changed underneath it |
| qpdf | 12.4.2 official `mingw64` release, pinned by `scripts/bootstrap-qpdf.ps1`; `qpdf30.dll` is bundled in every build | Direct, required build and runtime dependency | PDF object layer: pre-save structural validation, object-level writes, font and page-graphics parsing | Apache-2.0; `licenses/Apache-2.0.txt` and `qpdf30.dll` are shipped with every package | Bump `Version`, `Asset` and `Sha256` in `scripts/bootstrap-qpdf.ps1`, rerun `scripts/verify-qpdf.ps1` and the full test suite |
| QuickJS-ng | 0.9.0 (tag `v0.9.0`), fetched by `scripts/bootstrap-quickjs.ps1`; source compiled into the executable | Direct, required build dependency | AcroForm JavaScript runtime (events, calculations, validation) | MIT; `licenses/QuickJS-MIT.txt` is shipped with every package | Bump `Version` and `Sha256` in `scripts/bootstrap-quickjs.ps1`, rerun `QuickJsFormScriptTests` and the full suite |
| Qlementine | 1.4.2 (commit `13f72eb8b53bafd9ac24e5562d8ddc28d5440469`), fetched by CMake FetchContent and statically linked; one local patch applies a `QComboBox` recursion fix (`cmake/patches/qlementine-1.4.2-combobox-recursion.patch`) | Direct, required build dependency | Modern Qt Widgets style and control rendering | MIT; `licenses/Qlementine-MIT.txt` is shipped with every package | Update the commit pin in `CMakeLists.txt` and re-check whether the patch is still needed, then rebuild the UI and rerun the full test suite |
| pyHanko / pyhanko-cli | pyHanko 0.38.0, pyhanko-cli 0.6.0, PyInstaller 6.16.0; full dependency lock in `scripts/pyhanko-requirements.txt` | Direct, bundled standalone executable | PKCS#12 signing, PAdES validation information, RFC 3161 timestamps and signature validation | pyHanko is MIT; Python and all bundled dependency license texts are shipped under `pyhanko/licenses/` | Update the requirements lock and the three bootstrap pins, rebuild the standalone runtime and rerun signature plus package gates |
| SCOWL word lists | SCOWL 2018.04.16, taken from the npm package `@neopass/wordlist@0.5.2` (SHA-256 `29f13d889586c51deea87531350d1c697411c1b69454231e90dbe9658116c541`) | Direct, compiled into the binary as `src/pdf/ops/spell/EnglishWords.h` and `EnglishCommonWords.h` | English spell-check dictionary (78 899 known words plus a 38 051-word common subset used for near-neighbour lookups) | SCOWL's own MIT-style permission: free to use, copy, modify and distribute provided the copyright notice appears in all copies and in the supporting documentation; Copyright 2000-2016 Kevin Atkinson | Update: regenerate with `src/pdf/ops/spell/generate_dictionaries.py` (it verifies the upstream SHA-256), then rerun `Wave21SpellTests` |
| jieba Chinese word list | `jieba/dict.txt` (SHA-256 `7197c3211ddd98962b036cdf40324d1ea2bfaa12bd028e68faa70111a88e12a8`) | Direct, compiled into the binary as `src/pdf/ops/spell/ChineseWords.h` and `ChineseCommonWords.h` | Chinese spell-check dictionary (45 476 words, of which 14 244 are the high-frequency subset) | MIT, Copyright (c) 2013 Sun Junyi; the copyright and permission notice are reproduced in `src/pdf/ops/spell/SOURCES.md` | Update: regenerate with `src/pdf/ops/spell/generate_dictionaries.py` (it verifies the upstream SHA-256), then rerun `Wave21SpellTests` |

Components that are **not** dependencies and therefore are not listed as pins:
SQLite and OpenSSL are not linked by this project (the local index is a plain
on-disk file format, and Qt uses the Windows TLS backend), while FreeType,
HarfBuzz and ICU are reached only through the PDFium pin above.

## External runtime dependencies

Four components are found at runtime rather than linked or bundled. They are
listed in the register above so rule 1 is satisfied, but two of them need the
reason spelled out, because the decision is easy to read the wrong way:

- **Ghostscript is AGPL-3.0.** Rule 3 requires an explicit product and legal
  decision before a GPL-only dependency is adopted, and that decision has not
  been made. It is therefore never bundled and never redistributed. The
  optimiser still works without it: the structural tier is provided by the
  qpdf build that is already linked in. What is missing without Ghostscript is
  the image re-encoding, font re-compression and `/ebook` down-sampling tier.
- **The OFD converter is driven, not re-implemented.** This project has no OFD
  reader. `app::OfdConverters` keeps a list of the converters whose command
  line has been checked against upstream (their argument conventions differ,
  so guessing would write output to the wrong place), falls back to the generic
  `input output` convention for anything else, and checks the SHA-256 of the
  converter binary against the value recorded on first use. The app therefore
  **executes** a user-supplied executable; the integrity check is what makes
  that visible when the file changes between runs.

<!-- dependency-pins
qt=6.8.3
mingw=13.1.0
pdfium=chromium/8152
pdfium-api=159
pyhanko=0.38.0
pyhanko-cli=0.6.0
pyinstaller=6.16.0
qlementine=1.4.2
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
