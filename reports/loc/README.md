# Line-of-code report

Generated 2026-11-20T14:13:30+08:00 from `HEAD 6d59a72` + uncommitted changes.

Only files tracked by git are counted; binaries are skipped. A line counts as a comment when its first non-space characters are `//`, `/*`, `*` or `#`; mixed code+comment lines count as code.

Categories: `own-code` (default), `generated-data` (`src/pdf/ops/spell/*Words.h`, `src/i18n/*.ts`), `vendored` (`tools/pdfium/`).

## Scopes

| scope | files | total | blank | comment | code |
|---|---:|---:|---:|---:|---:|
| A_all_tracked_code | 944 | 448,624 | 26,416 | 43,695 | 378,513 |
| B_excl_generated_data | 939 | 261,860 | 26,377 | 43,679 | 191,804 |
| C_own_code | 914 | 251,524 | 25,536 | 36,443 | 189,545 |
| D_src_tests_worker | 838 | 230,075 | 23,118 | 35,149 | 171,808 |

## Files

- `loc_by_file.csv` - every counted file with its directory keys
- `loc_by_directory.csv` - rollup by second-level directory
- `loc_by_topdir.csv` - rollup by top-level directory
- `loc_by_language.csv` - rollup by language
- `loc_summary.csv` - scopes A-D above
- `loc_by_directory.png` - own code by directory (bar)
- `loc_areas_stacked.png` - code / comment / blank split (stacked)
- `loc_concentration.png` - cumulative distribution of own code

Regenerate with `python reports/loc/loc_report.py`.
