#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""权限拦截覆盖守卫：改文档结构的入口，必须有 requirePermission 检查。

为什么要有这道门
----------------
PDF 的权限位（/P）是**运行时**拦截：文档所有者可以逐项禁掉「打印 / 复制 /
批注 / 表单 / 页面组装 / 内容修改」，应用在**每个会改文档的操作入口**都要查一次。
仓库里这条检查一直写在 `PdfView::requirePermission` 里，由 30 多个调用点各自守住。

问题在于：这条约定**只存在于人的记忆里**。2026-11-20 这一轮核对就发现 27 个
入口漏了检查——其中「交换页面 / 逆序页面 / 交叉合并」与它们的兄弟操作
（移动 / 插入 / 删除 / 旋转）明明在同一组里，兄弟有、这几个没有；
「自动识别网络链接」看着像只读检测，实际会往页面里**写**链接注解。
漏掉的入口 = 权限位形同虚设，而这类缺陷不会让任何测试变红。

两条规则
--------
R1 **入口必须有检查**：凡是会改当前文档（页面结构 / 内容 / 批注）的
    `PdfView` 成员函数，函数体里必须出现 `requirePermission(`。
    判定用「写入原语」而不是函数名单——新增命令不用改这张名单也会被扫出来。
R2 **豁免必须写理由**：确实不该拦的（纯检测、只读、已被上游拦过的共享助手）
    登记在 `docs/benchmark/permission-coverage-exemptions.csv`，
    每条写清为什么。豁免过期（函数已删 / 已改）同样报错。

为什么要做静态扫描而不是单测
--------------------------
权限拦截在 `PdfView` 里，拦下来会弹 `QMessageBox` 问所有者密码——
这是 GUI 行为，本地不跑抢桌面的验收脚本（见用户明确要求），
CI 才有条件跑界面验收。所以这道门用**静态扫描**守住结构：
它不证明运行时行为，但能保证「凡是写了写入原语的入口都挂了检查」，
而漏检查恰恰是这类缺陷的主要成因。

用法：python scripts/check-permission-coverage.py
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VIEW = os.path.join(ROOT, "src", "app", "editor", "PdfView.cpp")
EXEMPT_PATH = os.path.join(ROOT, "docs", "benchmark", "permission-coverage-exemptions.csv")

# 写入原语：出现在函数体里就说明这个函数会改当前文档。
#
# 为什么按「原语」而不是按「函数名单」：函数名单要跟着新功能一起维护，
# 新加一个改文档的命令时没人会想到去改名单；原语是代码事实，扫出来就是事实。
#
# annotationController_->xxx  ：批注与页面的增删改
# document_.restorePageFrom   ：把一页换回当前文档
# recordContentEdit / refreshAfterDecoration：内容改动后记账与重绘
# setPageBackground / setTextObjectText：页面级写入
# SafeSave::writeAtomically：直接把字节写回磁盘（权限管理面板那条路径）
MUTATION_MARKERS = [
    re.compile(r"annotationController_->(movePage|insertBlankPage|deletePage|rotatePage|replacePageWithBackup)\s*\("),
    re.compile(r"document_\.restorePageFrom\s*\("),
    re.compile(r"\brecordContentEdit\s*\("),
    re.compile(r"\brefreshAfterDecoration\s*\("),
    re.compile(r"\.setPageBackground\s*\("),
    re.compile(r"\.setTextObjectText\s*\("),
    re.compile(r"\bSafeSave::writeAtomically\s*\("),
]

FUNC_RE = re.compile(r"^\w[\w:<>\*&\s]*\s+PdfView::(\w+)\s*\(")
PERM_RE = re.compile(r"requirePermission\s*\(")


def load_exemptions():
    if not os.path.exists(EXEMPT_PATH):
        return []
    lines = []
    with io.open(EXEMPT_PATH, "r", encoding="utf-8-sig") as handle:
        for index, raw in enumerate(handle):
            text = raw.strip()
            if not text:
                continue
            if index == 0 and text.lower().startswith("function"):
                continue
            parts = [part.strip() for part in text.split(",")]
            if len(parts) < 2:
                continue
            lines.append({"function": parts[0], "reason": parts[1]})
    return lines


def scan_functions(source):
    """返回 [(函数名, 函数体文本, 起始行号)]，只取 PdfView 的成员函数。"""
    lines = source.splitlines()
    results = []
    current_name = None
    current_start = 0
    body = []
    depth = 0
    started = False
    for index, line in enumerate(lines):
        if not started:
            match = FUNC_RE.match(line)
            if match:
                current_name = match.group(1)
                current_start = index + 1
                body = [line]
                depth = line.count("{") - line.count("}")
                started = True
                if depth <= 0:
                    results.append((current_name, "\n".join(body), current_start))
                    started = False
            continue
        body.append(line)
        depth += line.count("{") - line.count("}")
        if depth <= 0:
            results.append((current_name, "\n".join(body), current_start))
            started = False
    return results


def main():
    if not os.path.exists(VIEW):
        print("找不到 %s" % VIEW)
        sys.exit(2)

    source = io.open(VIEW, "r", encoding="utf-8-sig").read()
    functions = scan_functions(source)
    exemptions = load_exemptions()
    exempt_map = {}
    for item in exemptions:
        exempt_map.setdefault(item["function"], item["reason"])

    problems = []
    covered = 0
    for name, body, line in functions:
        if not any(marker.search(body) for marker in MUTATION_MARKERS):
            continue
        if PERM_RE.search(body):
            covered += 1
            continue
        if name in exempt_map:
            if not exempt_map[name].strip():
                problems.append("豁免没写理由：%s" % name)
            continue
        problems.append(
            "改文档的入口没有权限检查：PdfView::%s（行 %d）——"
            "缺 requirePermission(" % (name, line)
        )

    # 豁免不能腐烂：登记的函数已经不在文件里了，或已经自己带上检查了。
    seen = dict((name, body) for name, body, _ in functions)
    for name, reason in exempt_map.items():
        if name not in seen:
            problems.append("豁免指向不存在的函数：PdfView::%s" % name)
        elif PERM_RE.search(seen[name]):
            problems.append(
                "豁免已过期（该函数自己已经有权限检查了）：PdfView::%s" % name
            )

    exempted = len([n for n, b, _ in functions
                    if n in exempt_map and not PERM_RE.search(b)])
    print("扫描 %s：改文档的入口 %d 个（已挂权限检查 %d 个，登记豁免 %d 个），豁免表 %d 条"
          % (os.path.relpath(VIEW, ROOT), covered + exempted, covered, exempted,
             len(exempt_map)))
    if problems:
        print("")
        print("!! 权限拦截覆盖守卫未通过：")
        for item in problems:
            print("   - " + item)
        sys.exit(1)
    print("每个改文档的入口都有权限检查（或有登记过的豁免）")


if __name__ == "__main__":
    main()
