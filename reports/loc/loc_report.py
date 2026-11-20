"""Repo line-of-code report: CSV exports + charts (Pillow, no matplotlib).

Usage: python loc_report.py
Outputs into reports/loc/.
"""

from __future__ import annotations

import csv
import os
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "reports" / "loc"

LANG = {
    ".cpp": "C++", ".cc": "C++", ".cxx": "C++", ".c": "C",
    ".h": "C/C++ header", ".hpp": "C/C++ header", ".hxx": "C/C++ header",
    ".cs": "C#", ".py": "Python", ".ps1": "PowerShell", ".psm1": "PowerShell",
    ".ts": "TypeScript", ".tsx": "TypeScript", ".js": "JavaScript", ".mjs": "JavaScript",
    ".astro": "Astro", ".css": "CSS", ".scss": "CSS", ".html": "HTML",
    ".cmake": "CMake", ".sql": "SQL", ".sh": "Shell", ".bat": "Batch",
}
PREFIX_COMMENT = ("//", "#", "/*", "*")


def tracked_files() -> list[str]:
    out = subprocess.run(
        ["git", "ls-files"], cwd=ROOT, capture_output=True, text=True, check=True
    ).stdout
    return [line.strip() for line in out.splitlines() if line.strip()]


def read_lines(path: Path) -> list[str] | None:
    data = path.read_bytes()
    if b"\x00" in data[:4096]:
        return None
    text = data.decode("utf-8", errors="replace")
    text = text.lstrip("\ufeff")
    lines = text.splitlines()
    return lines


def count(path: Path) -> tuple[int, int, int]:
    lines = read_lines(path) or []
    blank = comment = 0
    for raw in lines:
        stripped = raw.strip()
        if not stripped:
            blank += 1
        elif stripped.startswith(PREFIX_COMMENT):
            comment += 1
    return len(lines), blank, comment


def dir_keys(path: str) -> tuple[str, str, str]:
    parts = path.split("/")[:-1]
    if not parts:
        return "(root)", "(root)", "(root)"
    l1 = parts[0]
    l2 = "/".join(parts[:2])
    l3 = "/".join(parts[:3])
    return l1, l2, l3


# ---------------------------------------------------------------- collect
rows = []
for f in tracked_files():
    ext = os.path.splitext(f)[1].lower()
    if ext not in LANG:
        continue
    full = ROOT / f
    if not full.is_file():
        continue
    total, blank, comment = count(full)
    l1, l2, l3 = dir_keys(f)
    generated = f.startswith("src/pdf/ops/spell/") and f.endswith("Words.h") or (
        f.startswith("src/i18n/") and ext == ".ts"
    )
    vendored = f.startswith("tools/pdfium/")
    category = "generated-data" if generated else ("vendored" if vendored else "own-code")
    rows.append(
        dict(
            file=f, dir_l1=l1, dir_l2=l2, dir_l3=l3, ext=ext, language=LANG[ext],
            total=total, blank=blank, comment=comment, code=total - blank - comment,
            category=category,
        )
    )

if not rows:
    sys.exit("no code files found")

# ---------------------------------------------------------------- CSV
OUT.mkdir(parents=True, exist_ok=True)


def write_csv(name: str, fields: list[str], data: list[dict]) -> None:
    with (OUT / name).open("w", newline="", encoding="utf-8-sig") as fh:
        w = csv.DictWriter(fh, fieldnames=fields)
        w.writeheader()
        for r in data:
            w.writerow({k: r[k] for k in fields})


write_csv(
    "loc_by_file.csv",
    ["file", "dir_l1", "dir_l2", "dir_l3", "ext", "language", "category",
     "total", "code", "comment", "blank"],
    rows,
)


def aggregate(rows: list[dict], key: str) -> list[dict]:
    acc: dict[str, dict] = defaultdict(
        lambda: dict(files=0, total=0, code=0, comment=0, blank=0)
    )
    for r in rows:
        a = acc[r[key]]
        a["files"] += 1
        for f in ("total", "code", "comment", "blank"):
            a[f] += r[f]
    out = [dict({key: k}, **v) for k, v in acc.items()]
    out.sort(key=lambda d: d["code"], reverse=True)
    return out


own = [r for r in rows if r["category"] == "own-code"]
gen = [r for r in rows if r["category"] == "generated-data"]
vend = [r for r in rows if r["category"] == "vendored"]

by_dir = aggregate(own, "dir_l2")
write_csv(
    "loc_by_directory.csv",
    ["dir_l2", "files", "total", "code", "comment", "blank"],
    by_dir,
)

by_top = aggregate(own, "dir_l1")
write_csv(
    "loc_by_topdir.csv",
    ["dir_l1", "files", "total", "code", "comment", "blank"],
    by_top,
)

by_lang = aggregate(own, "language")
write_csv(
    "loc_by_language.csv",
    ["language", "files", "total", "code", "comment", "blank"],
    by_lang,
)


def totals(rs: list[dict], scope: str) -> dict:
    return dict(
        scope=scope, files=len(rs),
        total=sum(r["total"] for r in rs),
        blank=sum(r["blank"] for r in rs),
        comment=sum(r["comment"] for r in rs),
        code=sum(r["code"] for r in rs),
    )


summary = [
    totals(rows, "A_all_tracked_code"),
    totals(own + vend, "B_excl_generated_data"),
    totals(own, "C_own_code"),
    totals([r for r in own if r["dir_l1"] in ("src", "tests", "worker")],
           "D_src_tests_worker"),
]
write_csv("loc_summary.csv", ["scope", "files", "total", "code", "comment", "blank"], summary)
for s in summary:
    print(s)


def cumulative_share(data: list[dict], value_key: str = "code") -> list[float]:
    total = sum(r[value_key] for r in data) or 1
    out, run = [], 0
    for r in data:
        run += r[value_key]
        out.append(run / total)
    return out


def dirs_for_80pct(data: list[dict]) -> int:
    for i, share in enumerate(cumulative_share(data), start=1):
        if share >= 0.8:
            return i
    return len(data)


def write_readme() -> None:
    from datetime import datetime

    head = subprocess.run(
        ["git", "rev-parse", "--short", "HEAD"], cwd=ROOT,
        capture_output=True, text=True, check=True,
    ).stdout.strip()
    dirty = bool(subprocess.run(
        ["git", "status", "--porcelain"], cwd=ROOT,
        capture_output=True, text=True, check=True,
    ).stdout.strip())
    lines = [
        "# Line-of-code report",
        "",
        f"Generated {datetime.now().astimezone().isoformat(timespec='seconds')} "
        f"from `HEAD {head}`{' + uncommitted changes' if dirty else ''}.",
        "",
        "Only files tracked by git are counted; binaries are skipped. "
        "A line counts as a comment when its first non-space characters are "
        "`//`, `/*`, `*` or `#`; mixed code+comment lines count as code.",
        "",
        "Categories: `own-code` (default), `generated-data` "
        "(`src/pdf/ops/spell/*Words.h`, `src/i18n/*.ts`), `vendored` "
        "(`tools/pdfium/`).",
        "",
        "## Scopes",
        "",
        "| scope | files | total | blank | comment | code |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    for s in summary:
        lines.append(
            f"| {s['scope']} | {s['files']} | {s['total']:,} | {s['blank']:,} "
            f"| {s['comment']:,} | {s['code']:,} |"
        )
    lines += [
        "",
        "## Files",
        "",
        "- `loc_by_file.csv` - every counted file with its directory keys",
        "- `loc_by_directory.csv` - rollup by second-level directory",
        "- `loc_by_topdir.csv` - rollup by top-level directory",
        "- `loc_by_language.csv` - rollup by language",
        "- `loc_summary.csv` - scopes A-D above",
        "- `loc_by_directory.png` - own code by directory (bar)",
        "- `loc_areas_stacked.png` - code / comment / blank split (stacked)",
        "- `loc_concentration.png` - cumulative distribution of own code",
        "",
        "Regenerate with `python reports/loc/loc_report.py`.",
        "",
    ]
    (OUT / "README.md").write_text("\n".join(lines), encoding="utf-8")


write_readme()


# ---------------------------------------------------------------- charts
from PIL import Image, ImageDraw, ImageFont  # noqa: E402

FONT_CANDIDATES = [
    r"C:\Windows\Fonts\msyh.ttc",
    r"C:\Windows\Fonts\msyhl.ttc",
    r"C:\Windows\Fonts\segoeui.ttf",
    r"C:\Windows\Fonts\arial.ttf",
]


def load_font(size: int) -> ImageFont.FreeTypeFont:
    for p in FONT_CANDIDATES:
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, size)
            except OSError:
                continue
    return ImageFont.load_default(size)


BG = (250, 250, 248)
FG = (32, 36, 44)
MUTED = (120, 128, 140)
ACCENT = (46, 104, 184)
ACCENT2 = (222, 143, 46)


def bar_chart(path: Path, data: list[dict], label_key: str, value_key: str,
              title: str, subtitle: str, unit: str = "code lines") -> None:
    W, H = 1500, 176 + 46 * len(data)
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    f_title = load_font(38)
    f_sub = load_font(22)
    f_lbl = load_font(24)
    f_val = load_font(22)
    d.text((60, 40), title, font=f_title, fill=FG)
    d.text((60, 90), subtitle, font=f_sub, fill=MUTED)

    top = 150
    left = 60
    label_w = 420
    bar_x = left + label_w
    max_v = max(r[value_key] for r in data) or 1
    bar_max = W - bar_x - 190
    for i, r in enumerate(data):
        y = top + i * 46
        d.text((left, y - 14), str(r[label_key]), font=f_lbl, fill=FG)
        w = int(bar_max * r[value_key] / max_v)
        d.rounded_rectangle(
            [bar_x, y - 12, bar_x + max(w, 2), y + 14], radius=4, fill=ACCENT
        )
        d.text((bar_x + w + 12, y - 12), f"{r[value_key]:,}", font=f_val, fill=MUTED)
    d.text((bar_x, H - 40), unit, font=f_sub, fill=MUTED)
    img.save(path)


def line_chart(path: Path, data: list[dict], label_key: str, value_key: str,
               title: str, subtitle: str) -> None:
    """Cumulative distribution curve: how concentrated the code is."""
    W, H = 1500, 760
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    f_title = load_font(38)
    f_sub = load_font(22)
    f_tick = load_font(20)
    d.text((60, 40), title, font=f_title, fill=FG)
    d.text((60, 90), subtitle, font=f_sub, fill=MUTED)

    left, right, top, bottom = 130, W - 90, 160, H - 90
    total = sum(r[value_key] for r in data) or 1
    cum, pts = 0, []
    for i, r in enumerate(data, start=1):
        cum += r[value_key]
        x = left + (right - left) * (i - 1) / max(len(data) - 1, 1)
        y = bottom - (bottom - top) * cum / total
        pts.append((x, y, i))

    # grid + y ticks
    for k in range(5):
        gy = bottom - (bottom - top) * k / 4
        d.line([left, gy, right, gy], fill=(226, 228, 232), width=1)
        d.text((left - 78, gy - 12), f"{100*k/4:.0f}%", font=f_tick, fill=MUTED)
    d.line([left, bottom, right, bottom], fill=(150, 156, 166), width=2)
    d.line([left, top, left, bottom], fill=(150, 156, 166), width=2)

    d.line([(p[0], p[1]) for p in pts], fill=ACCENT, width=4, joint="curve")
    for x, y, i in pts:
        d.ellipse([x - 5, y - 5, x + 5, y + 5], fill=ACCENT)
    d.text((left, H - 56), f"directories ranked by {value_key} (largest first)",
           font=f_sub, fill=MUTED)
    d.text((left, top - 34), "cumulative share of own code", font=f_sub, fill=MUTED)

    # annotate the 80% crossing
    for x, y, i in pts:
        if y <= bottom - (bottom - top) * 0.8:
            d.line([left, y, x, y], fill=ACCENT2, width=2)
            d.text((left - 76, y - 12), "80%", font=f_tick, fill=ACCENT2)
            break
    img.save(path)


bar_data = [r for r in by_dir if r["code"] >= 500][:16]
bar_chart(
    OUT / "loc_by_directory.png", bar_data, "dir_l2", "code",
    "Own code by directory",
    f"tracked files, generated word lists and vendored pdfium headers excluded · {len(own)} files",
)
line_chart(
    OUT / "loc_concentration.png", by_dir, "dir_l2", "code",
    "Code concentration curve",
    f"{len(by_dir)} directories, own code only · 80% of lines live in the top "
    f"{dirs_for_80pct(by_dir)}",
)

# stacked chart: code vs comment vs blank for the main areas
def stacked_chart(path: Path, data: list[dict], title: str, subtitle: str) -> None:
    W, H = 1500, 120 + 60 * len(data)
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    f_title, f_sub, f_lbl, f_val = load_font(38), load_font(22), load_font(24), load_font(20)
    d.text((60, 40), title, font=f_title, fill=FG)
    d.text((60, 90), subtitle, font=f_sub, fill=MUTED)
    left, label_w = 60, 420
    bar_x = left + label_w
    bar_max = W - bar_x - 220
    max_v = max(r["total"] for r in data) or 1
    top = 160
    for i, r in enumerate(data):
        y = top + i * 60
        d.text((left, y - 12), str(r["dir_l2"]), font=f_lbl, fill=FG)
        x = bar_x
        for key, color in (("code", ACCENT), ("comment", ACCENT2), ("blank", (205, 210, 218))):
            w = int(bar_max * r[key] / max_v)
            if w:
                d.rectangle([x, y - 12, x + w, y + 16], fill=color)
            x += w
        d.text((bar_x + int(bar_max * r["total"] / max_v) + 12, y - 10),
               f"{r['total']:,}", font=f_val, fill=MUTED)
    # legend
    lx = W - 300
    for j, (name, color) in enumerate((("code", ACCENT), ("comment", ACCENT2), ("blank", (205, 210, 218)))):
        d.rectangle([lx, 40 + j * 30, lx + 18, 58 + j * 30], fill=color)
        d.text((lx + 28, 38 + j * 30), name, font=f_sub, fill=FG)
    img.save(path)


stacked_chart(
    OUT / "loc_areas_stacked.png", [r for r in by_dir if r["code"] >= 500][:12],
    "Code vs comment vs blank",
    "own code only, by directory",
)

print("wrote", OUT)
for p in sorted(OUT.iterdir()):
    print(" -", p.name, p.stat().st_size)
