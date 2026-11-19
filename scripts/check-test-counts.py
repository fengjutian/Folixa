# -*- coding: utf-8 -*-
"""文档里的「专项单测条数」必须等于实测条数 —— 当前口径的对账门。

为什么要有这道门：
  评分卡与功能矩阵里到处写着「`XxxTests` N 条」，这是给能力**背书**的数字。
  代码加了用例、文档没跟上，这条数字就变成过期承诺 —— 看着还有 N 条兜底，
  实际已经变了。当前口径（能力表 / 矩阵 CSV / 本轮版本审计段）与实测对不上，
  就是在对外报一个假的覆盖率。

口径（别用数 slot 的写法）：
  真实用例数 = 测试 exe 报的 Totals - 2
  - Qt 会隐式调用并报告 initTestCase() / cleanupTestCase()，即使源码里没声明；
    所以 Totals 恒比真实用例数多 2（这一条实测过：I18nTests Totals 22 → 真实 20）。
  - 数据驱动用例（带 _data()）Totals 把每个数据行各算一条，Totals-2 依然最准；
    数 slot 会把 initTestCase 当用例，也会漏掉数据驱动的多行，两个方向都错。

范围（当前口径才要求准）：
  - docs/FOXIT_BENCHMARK.md 的第 2 节能力表 —— 当前能力陈述
  - docs/benchmark/foxit-features.csv 全部行 —— 矩阵唯一数据源
  - docs/VERSION_AUDIT.md 的最新一轮段 —— 本轮重算记录
不在范围（历史存档，保留原样）：
  - 版本审计的更早轮次段：仓库自述是「当时看到的账」存档
  - 评分卡第 3 节校准记录、第 4~7 节
  - 负例对照里的「N 条变红」：那是变红的用例数，不是目标总数

用法：
  python scripts/check-test-counts.py                 # 对账，退出码非零 = 对不上
  python scripts/check-test-counts.py --bin-dir <dir>  # 指定测试二进制目录
"""
from __future__ import annotations

import argparse
import glob
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCORE = os.path.join(ROOT, "docs", "FOXIT_BENCHMARK.md")
CSV_PATH = os.path.join(ROOT, "docs", "benchmark", "foxit-features.csv")
AUDIT = os.path.join(ROOT, "docs", "VERSION_AUDIT.md")

# `FooTests` 13 条 / `FooTests`（13 条）
COUNT_RE = re.compile(r"(`?)([A-Za-z][A-Za-z0-9]*Tests)\1\s*[（(]?\s*(\d+)\s*条")
TOTALS_RE = re.compile(
    r"Totals:\s*(\d+)\s+passed,\s*(\d+)\s+failed,\s*(\d+)\s+skipped"
)


def read_text(path: str) -> str:
    with open(path, "rb") as handle:
        raw = handle.read()
    return raw.decode("utf-8-sig")


def measured(bin_dir: str) -> dict[str, int]:
    """跑每个测试 exe，读它自己报的 Totals，算出真实用例数。"""
    out: dict[str, int] = {}
    for exe in sorted(glob.glob(os.path.join(bin_dir, "*Tests.exe"))):
        name = os.path.splitext(os.path.basename(exe))[0]
        try:
            proc = subprocess.run(
                [exe], capture_output=True, text=True, timeout=600,
                encoding="utf-8", errors="replace",
            )
        except Exception as exc:  # pragma: no cover - 环境问题不该静默
            print(f"[!] 跑不动 {name}: {exc}")
            continue
        blob = (proc.stdout or "") + (proc.stderr or "")
        match = TOTALS_RE.search(blob)
        if not match:
            print(f"[!] {name}: 没有读到 Totals，跳过")
            continue
        total = sum(int(match.group(i)) for i in (1, 2, 3))
        out[name] = total - 2
    return out


def line_ranges(text: str, start_marker: str, end_marker: str) -> set[int]:
    """半开区间 [start, end) 内的行号（1-based）。"""
    start = text.index(start_marker)
    end = text.index(end_marker)
    return set(range(text[:start].count("\n") + 1, text[:end].count("\n") + 1))


def check(path: str, allowed: set[int] | None, truth: dict[str, int]) -> list[str]:
    problems: list[str] = []
    text = read_text(path)
    lines = text.split("\n")
    for match in COUNT_RE.finditer(text):
        name, claimed = match.group(2), int(match.group(3))
        if name not in truth:
            continue
        line_no = text.count("\n", 0, match.start()) + 1
        if allowed is not None and line_no not in allowed:
            continue
        if "变红" in lines[line_no - 1]:
            continue
        actual = truth[name]
        if actual != claimed:
            problems.append(
                f"{os.path.relpath(path, ROOT)}:{line_no}  "
                f"{name} 文档写 {claimed} 条 / 实测 {actual} 条"
            )
    return problems


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--bin-dir",
        default=os.path.join(ROOT, "build", "windows-debug", "bin"),
        help="测试二进制所在目录",
    )
    args = parser.parse_args()

    if not os.path.isdir(args.bin_dir):
        print(f"[!] 没有测试二进制目录：{args.bin_dir}")
        print("    （这道门需要先构建测试目标；CI 上在 Debug 构建之后跑）")
        return 2

    truth = measured(args.bin_dir)
    if not truth:
        print(f"[!] {args.bin_dir} 里没有可用的测试 exe")
        return 2
    print(f"[i] 实测 {len(truth)} 个测试目标的真实用例数")

    score_text = read_text(SCORE)
    audit_text = read_text(AUDIT)
    score_range = line_ranges(score_text, "### A. 阅读与显示", "## 3. 汇总与达标线")
    audit_range = line_ranges(audit_text, "## 1.4 ", "## 1. 结论")

    problems: list[str] = []
    problems += check(SCORE, score_range, truth)
    problems += check(CSV_PATH, None, truth)
    problems += check(AUDIT, audit_range, truth)

    print()
    if problems:
        for item in problems:
            print(f"[X] {item}")
        print(f"\n[!] {len(problems)} 处条数与实测对不上")
        return 1
    print("[OK] 当前口径里的专项单测条数与实测一致")
    return 0


if __name__ == "__main__":
    sys.exit(main())
