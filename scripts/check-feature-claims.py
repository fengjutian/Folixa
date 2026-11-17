#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""功能宣称守卫：aligned 行的说明里出现「降级措辞」时，必须有登记过的豁免。

为什么要这道门
--------------
整改方案（docs/DEGRADED_FEATURE_REMEDIATION.md §9.1）提出「备注出现未豁免的降级词时
禁止标记 aligned」。方向对，但不能照字面实现：实测整改文档给的词表
（「未做 / 只有 / 不支持 / 未验证 / 不是」）在现有 aligned 行里会误伤 ——
「不是」在两行里指的是**说清边界**而不是降级（表单数据文件「填完仍然是可继续填写的表单，
不是拍平的」、AI 内容检测「模型解析失败时显示无法判断而不是编一个结论」）。
所以这里的词表只收三类真正说明「这条命令没做全」的措辞：

  能力未落地类：还没有接这一步 / 未接 / 零实现 / 不支持
  只做了一部分类：只导 / 只到 / 只含 / 只写 / 只支持 / 只处理 / 只认 / 只走 / 仍无 / 仍只 / 空壳
  验证缺口类：未验证 / 没有真机

命中不等于判错 —— 命中而没有豁免才是错。豁免清单在
docs/benchmark/feature-claim-exemptions.csv，每条必须写清「为什么这条仍然算对齐」。
豁免也会过期：说明列里那个短语没了（说明被改写了）或者那一行已经退回 partial，
都算失败 —— 免得豁免变成永久通行证。

用法：python scripts/check-feature-claims.py
"""
import csv
import io
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MATRIX = os.path.join(ROOT, "docs", "benchmark", "foxit-features.csv")
EXEMPT = os.path.join(ROOT, "docs", "benchmark", "feature-claim-exemptions.csv")

PHRASES = [
    # 能力未落地类
    "还没有接这一步", "未接", "零实现", "不支持",
    # 只做了一部分类
    "只导", "只到", "只含", "只写", "只支持", "只处理", "只认", "只走",
    "仍无", "仍只", "空壳",
    # 验证缺口类
    "未验证", "没有真机",
]

problems = []


def load_matrix():
    with io.open(MATRIX, "r", encoding="utf-8-sig", newline="") as fh:
        return list(csv.DictReader(fh))


def load_exemptions():
    path = os.path.join(ROOT, "docs", "benchmark", "feature-claim-exemptions.csv")
    if not os.path.exists(path):
        return []
    with io.open(path, "r", encoding="utf-8-sig", newline="") as fh:
        return list(csv.DictReader(fh))


def main():
    rows = load_matrix()
    exemptions = load_exemptions()

    # 豁免索引：按 (标签页, 分组, 命令) 归组
    index = {}
    for row in exemptions:
        key = (row.get("tab", "").strip(), row.get("group", "").strip(), row.get("command", "").strip())
        index.setdefault(key, []).append(row)

    # ---- 1. aligned 行里的降级措辞必须有豁免 ----
    used = set()
    for row in rows:
        if row["status"] != "aligned":
            continue
        key = (row["tab"].strip(), row["group"].strip(), row["command"].strip())
        note = row["note"]
        for phrase in PHRASES:
            if phrase not in note:
                continue
            matched = None
            for ex in index.get(key, []):
                if ex.get("phrase", "").strip() == phrase:
                    matched = ex
                    break
            if matched is None:
                problems.append(
                    "aligned 行出现降级措辞「%s」且没有豁免：%s | %s | %s"
                    % (phrase, key[0], key[1], key[2])
                )
            else:
                used.add((key, phrase))
                if not matched.get("reason", "").strip():
                    problems.append(
                        "豁免没写理由：%s | %s | %s（%s）"
                        % (key[0], key[1], key[2], phrase)
                    )

    # ---- 2. 豁免不能过期 ----
    for row in exemptions:
        key = (row.get("tab", "").strip(), row.get("group", "").strip(), row.get("command", "").strip())
        phrase = row.get("phrase", "").strip()
        if not phrase:
            problems.append("豁免缺短语字段：%s | %s | %s" % key)
            continue
        target = None
        for r in rows:
            if (r["tab"].strip(), r["group"].strip(), r["command"].strip()) == key:
                target = r
                break
        if target is None:
            problems.append("豁免指向矩阵里不存在的行：%s | %s | %s" % key)
            continue
        if target["status"] != "aligned":
            problems.append(
                "豁免已过期（该行已不是 aligned）：%s | %s | %s（%s）"
                % (key[0], key[1], key[2], phrase)
            )
            continue
        if phrase not in target["note"]:
            problems.append(
                "豁免已过期（说明列里已没有该短语）：%s | %s | %s（%s）"
                % (key[0], key[1], key[2], phrase)
            )

    aligned_count = sum(1 for r in rows if r["status"] == "aligned")
    print("矩阵 aligned 行：%d 条；豁免清单：%d 条" % (aligned_count, len(exemptions)))
    if problems:
        print("")
        print("!! 功能宣称守卫未通过：")
        for item in problems:
            print("   - " + item)
        sys.exit(1)
    print("aligned 行的降级措辞都有登记过的豁免")


if __name__ == "__main__":
    main()
