# -*- coding: utf-8 -*-
"""跨文档能力主张守卫：同一项能力在不同文档里不能各说各话。

为什么要有这道门
----------------
已经有两道文档门，各管一半：

  - `check-feature-claims.py`：只看**矩阵 CSV 里 aligned 行的说明列**，
    看有没有没登记的降级措辞；
  - `check-docs-consistency.py`：只对**数字**（条数、对齐数、达成数、总项数）。

两者都盖不到一种情况：**能力早就落地了，别的文档还留着「没做」的旧说法**。

真发生过：H5（PDF → Office）在代码与矩阵里早已是整篇导出（DOCX 出真图片与
真表格、PPTX 一页一张带图带表、XLSX 按页拆工作表），可评分卡的进度快照、
整改方案 §3.1 还写着「Excel / PPT 零实现、Word 只导一页文本层」。
**三份文档对同一项能力三个说法，而上面两道门一路绿灯**——因为数字全都对得上，
错的只是措辞。

这道门怎么判
------------
不拿通用关键词扫全仓。`check-feature-claims.py` 的词表教训已经写在它的
注释里：通用词表会误伤「说清边界」的句子，也会漏掉真正的降级词。

所以这里**逐条登记**：每个「能力 × 文档 × 过时措辞」写进
`docs/benchmark/doc-claim-contradictions.csv`，命中即失败。登记表里每条都要写
清「为什么这句现在是错的」。

两个可配的范围：

  - `since`（可空）：只从该标记（章节标题 / 轮次标题）出现的那一行起往后算。
    版本审计是**按轮次追加的历史文档**——历史轮次里的旧结论是「当时的账」，
    不该改也不该禁；但从最新一轮重算起就不许再出现旧说法。
    留空 = 整份文件都算。

外加一条结构检查：**Markdown 表格行必须以 `|` 收尾**。

这条是踩出来的：评分卡里 H5、J5、J6、J7 四行漏了收尾竖线。渲染上看不出来
（GFM 容忍），但任何按 `|` 切列的脚本都会把这四行整行丢掉——评分卡「102 项」
差点被读成 98 项。

用法：python scripts/check-doc-claims.py
"""
import csv
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REGISTRY = os.path.join(ROOT, "docs", "benchmark", "doc-claim-contradictions.csv")
MATRIX = os.path.join(ROOT, "docs", "benchmark", "foxit-features.csv")

problems = []


def read_lines(path):
    """读文件并保留行号信息。返回 [(行号, 原文)]。"""
    with io.open(path, "r", encoding="utf-8-sig") as handle:
        return [(i, line) for i, line in enumerate(handle.read().splitlines(), 1)]


def markdown_files():
    """所有要扫的 Markdown：docs/ 下的全部 + 仓库根的 README。"""
    out = []
    docs_dir = os.path.join(ROOT, "docs")
    for base, _dirs, files in os.walk(docs_dir):
        for name in files:
            if name.endswith(".md"):
                out.append(os.path.join(base, name))
    readme = os.path.join(ROOT, "README.md")
    if os.path.exists(readme):
        out.append(readme)
    return sorted(out)


# ---- 1. 登记表本身要对得上 ----
matrix_codes = set()
with io.open(MATRIX, "r", encoding="utf-8-sig", newline="") as handle:
    for row in csv.DictReader(handle):
        code = (row.get("capability") or "").strip()
        if code:
            matrix_codes.add(code)

with io.open(REGISTRY, "r", encoding="utf-8-sig", newline="") as handle:
    registry = list(csv.DictReader(handle))

print("矛盾登记表：%d 条" % len(registry))

for row in registry:
    code = (row.get("capability") or "").strip()
    rel = (row.get("file") or "").strip()
    phrase = (row.get("phrase") or "").strip()
    since = (row.get("since") or "").strip()
    reason = (row.get("reason") or "").strip()

    if not code:
        problems.append("登记表有一行没写能力编号")
        continue
    if code not in matrix_codes:
        problems.append("登记表里的 %s 不在矩阵的能力编号里（登记表可能已过期）" % code)
    if not rel:
        problems.append("登记表里 %s 没写文件路径" % code)
        continue
    if not phrase:
        problems.append("登记表里 %s（%s）没写禁用措辞" % (code, rel))
        continue
    if not reason:
        problems.append("登记表里 %s（%s）没写「为什么这句现在是错的」" % (code, rel))
        continue

    target = os.path.join(ROOT, rel)
    if not os.path.exists(target):
        problems.append("登记表里 %s 指向的文件不存在：%s" % (code, rel))
        continue

    lines = read_lines(target)

    # since：从标记所在的那一行起往后才算；历史轮次里的旧说法不拦。
    start = 1
    if since:
        start = None
        for lineno, text in lines:
            if since in text:
                start = lineno
                break
        if start is None:
            problems.append("登记表里 %s（%s）的 since 标记「%s」在文件里找不到"
                            % (code, rel, since))
            continue

    hits = [lineno for lineno, text in lines if lineno >= start and phrase in text]
    for lineno in hits:
        problems.append(
            "%s（%s）里出现已过时的说法「%s」（第 %d 行）：%s"
            % (code, rel, phrase, lineno, reason)
        )

# ---- 2. Markdown 表格行必须闭合 ----
unclosed = []
for path in markdown_files():
    in_fence = False
    with io.open(path, "r", encoding="utf-8-sig") as handle:
        for lineno, line in enumerate(handle.read().splitlines(), 1):
            stripped = line.strip()
            if stripped.startswith("```") or stripped.startswith("~~~"):
                in_fence = not in_fence
                continue
            if in_fence:
                continue
            if not stripped.startswith("|"):
                continue
            if set(stripped) <= set("|-: "):
                continue
            if not stripped.endswith("|"):
                unclosed.append((path, lineno, stripped[:60]))

if unclosed:
    for path, lineno, head in unclosed:
        problems.append(
            "表格行没收尾（缺末尾的 |）：%s:%d  %s" % (os.path.relpath(path, ROOT), lineno, head)
        )

print("表格行闭合检查：%d 处没收尾" % len(unclosed))

print()
if problems:
    print("!! 跨文档主张守卫未通过：")
    for item in problems:
        print("   - " + item)
    sys.exit(1)
print("同一项能力在各文档里的说法不打架，表格行也都闭合")
