#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""partial 守卫：矩阵里每条 partial 都必须说清「差在哪」，并且与评分卡口径对得上。

为什么要有这道门
----------------
矩阵里的 partial 是「功能在，但有明显降级」。可这套状态是**手工维护**的，
于是有两种旧账会悄悄攒起来：

1. **只写了实现、没写边界**：说明列写「整篇页面逆序可写盘」，读者看不出差在哪，
   归因脚本也只能判成「要新实现」。2026-11-20 这一轮人工核出的四条旧账
   （逆序 / 签章 / 时间戳文档 / 限制访问）全是这个形状。
2. **评分卡早判「对齐」、矩阵还挂着 partial**：评分卡是能力级、矩阵是命令级，
   两档口径本来就不同；但**真的**出现「能力已对齐、命令却还写着降级」时，
   没有任何东西会提醒。

三条规则
--------
R1 **边界可证**：每条 partial 行的说明列必须用 `**加粗**` 标出降级边界。
   加粗的那段就是矩阵文档里给人看的边界；没有加粗段 = 这条 partial 说不清差在哪。
R2 **口径一致**：能力编号在评分卡里判「对齐」，而矩阵里同一编号还有 partial 行
   → 报矛盾，除非在豁免表里登记（`docs/benchmark/matrix-scorecard-exemptions.csv`）。
R3 **豁免不腐烂**：豁免表登记的那条矩阵行已经不是 partial → 报失败。
   否则豁免会变成永久通行证：降级被补上了、矩阵也升成 aligned，豁免却还留着。

命中不等于判错
--------------
R2 命中多半是**两档口径的正常差异**（评分卡评能力有无、矩阵评命令够不够细），
所以必须有豁免机制；但豁免必须写清理由，而且会被 R3 盯着过期。
"""
import csv
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV_PATH = os.path.join(ROOT, "docs", "benchmark", "foxit-features.csv")
SCORE = os.path.join(ROOT, "docs", "FOXIT_BENCHMARK.md")
EXEMPT_PATH = os.path.join(ROOT, "docs", "benchmark", "matrix-scorecard-exemptions.csv")

problems = []


def read_csv(path):
    if not os.path.exists(path):
        return None
    with io.open(path, "r", encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


matrix = read_csv(CSV_PATH)
if matrix is None:
    print("矩阵 CSV 不存在：%s" % CSV_PATH)
    sys.exit(1)

# ---- 评分卡状态 ----
score = io.open(SCORE, "r", encoding="utf-8-sig").read()
states = {}
for line in score.splitlines():
    match = re.match(r"^\|\s*([A-L]\d+)\s*\|", line)
    if not match:
        continue
    columns = [cell.strip() for cell in line.strip().strip("|").split("|")]
    if len(columns) < 6:
        continue
    states[match.group(1)] = columns[3]


def aligned_in_scorecard(code):
    state = states.get(code, "")
    return state.replace("*", "").startswith("对齐")


# ---- 矩阵里的 partial 行 ----
partial_rows = [row for row in matrix if row.get("status", "").strip() == "partial"]

# R1：partial 必须有加粗的边界段。
missing_boundary = []
for row in partial_rows:
    if not re.findall(r"\*\*(.+?)\*\*", row.get("note", "")):
        missing_boundary.append(row)
        problems.append("partial 行没有标出降级边界（说明列里没有加粗段）：%s / %s / %s"
                        % (row.get("capability", ""), row.get("tab", ""), row.get("command", "")))

# R2：评分卡判「对齐」× 矩阵 partial → 必须有豁免。
conflicts = []
for row in partial_rows:
    code = row.get("capability", "").strip()
    if not code or code in ("—", "-"):
        continue
    if aligned_in_scorecard(code):
        conflicts.append(row)

# ---- 豁免表 ----
exemptions = read_csv(EXEMPT_PATH)
if exemptions is None:
    problems.append("豁免表不存在：docs/benchmark/matrix-scorecard-exemptions.csv")
    exempt_index = {}
else:
    exempt_index = {}
    for row in exemptions:
        key = (row.get("capability", "").strip(), row.get("tab", "").strip(),
               row.get("command", "").strip())
        if not row.get("reason", "").strip():
            problems.append("豁免表里 %s / %s / %s 没写理由" % key)
        exempt_index[key] = row

for row in conflicts:
    key = (row.get("capability", "").strip(), row.get("tab", "").strip(),
           row.get("command", "").strip())
    if key not in exempt_index:
        problems.append("评分卡 %s 判「对齐」，矩阵这条却还是 partial：%s / %s / %s"
                        "（要么矩阵状态过时，要么在豁免表里登记理由）"
                        % (key[0], key[0], key[1], key[2]))

# R3：豁免过期 —— 登记的那条已经不是 partial。
partial_keys = set(
    (row.get("capability", "").strip(), row.get("tab", "").strip(),
     row.get("command", "").strip())
    for row in partial_rows)
for key in exempt_index:
    if key not in partial_keys:
        problems.append("豁免过期：%s / %s / %s 在矩阵里已经不是 partial（降级补上了？）"
                        % key)

print("矩阵 partial %d 条；评分卡判「对齐」引发的口径冲突 %d 条；豁免登记 %d 条"
      % (len(partial_rows), len(conflicts), len(exempt_index)))
if missing_boundary:
    print("没有标出边界的 partial：%d 条" % len(missing_boundary))

print()
if problems:
    print("!! partial 守卫未通过：")
    for item in problems:
        print("   - " + item)
    sys.exit(1)
print("每条 partial 都说清了差在哪，评分卡口径也对得上")
