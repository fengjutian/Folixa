# -*- coding: utf-8 -*-
"""生成「要修改的功能列表」文档：docs/FEATURE_MODIFICATION_BACKLOG.md

三梯队：
  第一梯队 —— 欠账（评分卡写「对齐」但承诺范围没达成）：逐条写欠什么 + 补齐后的验收方式。
  第二梯队 —— 矩阵 partial（功能在但有明显降级）：按标签页列出，边界直接从说明列的加粗段抽。
  第三梯队 —— 结构性改动（不是功能缺口，是口径与流程的改动）。

数字全部**从数据源现算**（矩阵 CSV / 欠账 CSV / 评分卡），不手写；
生成完由 scripts/check-docs-consistency.py 对账。

用法：python scripts/dump-feature-modification-list.py
"""
import collections
import csv
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV_PATH = os.path.join(ROOT, "docs", "benchmark", "foxit-features.csv")
DEBT_PATH = os.path.join(ROOT, "docs", "benchmark", "scorecard-debt.csv")
SCORE_PATH = os.path.join(ROOT, "docs", "FOXIT_BENCHMARK.md")
OUT_PATH = os.path.join(ROOT, "docs", "FEATURE_MODIFICATION_BACKLOG.md")

TAB_ORDER = [
    "文件", "主页", "转换", "编辑", "页面管理", "注释", "视图", "表单", "保护",
    "电子签章", "共享", "辅助工具", "论文工具", "文档管理", "录制", "帮助", "AI助手",
]

# partial 行的归因规则：**先匹配先生效**。
# 默认归因是「要新实现」—— 矩阵里 partial 的定义就是「功能在，但降级在我们这边」；
# 只有说明列明确指向外部原因（设备 / 第三方软件 / 新依赖）才改判。
KIND_RULES = [
    ("要新依赖", [
        "TWAIN", "veraPDF", "Java", "JavaScript", "Ghostscript", "OpenCV",
        "外部转换器", "OFD 转换器", "QtWebEngine", "解码音视频",
    ]),
    ("要真机 / 要第三方软件", [
        "扫描仪", "真机", "OneNote", "打印驱动", "双面", "读屏", "游戏栏",
        "Provider", "系统语音", "本机没有", "本机无", "设备", "依赖本机装",
    ]),
    ("AI 输出格式", [
        "Markdown", "抽取式", "不是 .pptx", "不是 xlsx",
    ]),
    ("范围窄", [
        "没有手型", "只有整窗", "无区域", "只能复制文本", "用后回到选择",
        "没有内置模板库", "标准 14 字体三档", "不是无障碍配置向导",
        "未接朗读设置", "无批量筛选", "只有文字图章", "没有全屏", "无音色",
        "没有全屏沉浸", "只给标准",
    ]),
]
DEFAULT_KIND = "要新实现"


def read_matrix():
    with io.open(CSV_PATH, "r", encoding="utf-8-sig", newline="") as fh:
        return list(csv.DictReader(fh))


def read_debt():
    with io.open(DEBT_PATH, "r", encoding="utf-8-sig", newline="") as fh:
        return list(csv.DictReader(fh))


def read_scorecard_names():
    """编号 -> 能力名（评分卡第 2 节）。"""
    names = {}
    pattern = re.compile(r"^\|\s*([A-L]\d+)\s*\|\s*([^|]+?)\s*\|")
    for line in io.open(SCORE_PATH, "r", encoding="utf-8-sig").read().splitlines():
        m = pattern.match(line.strip())
        if m:
            names[m.group(1)] = m.group(2).strip()
    return names


def classify(note):
    """返回 (归因, 边界文字)。

    边界优先取说明列里加粗的段（那是「这条差在哪」的标注）；没加粗就整句取用。
    归因按规则先匹配先生效；都没命中就是「要新实现」——
    矩阵里 partial 的定义就是「功能在，但降级在我们这边」。
    """
    bold = re.findall(r"\*\*(.+?)\*\*", note)
    evidence = "；".join(bold[:2]) if bold else note
    for kind, keys in KIND_RULES:
        for key in keys:
            if key in note:
                return kind, evidence
    return DEFAULT_KIND, evidence


def main():
    matrix = read_matrix()
    debt = read_debt()
    names = read_scorecard_names()

    counter = collections.Counter(r["status"] for r in matrix)
    total = len(matrix)
    partial_rows = [r for r in matrix if r["status"] == "partial"]

    lines = []
    lines.append("# 要修改的功能列表")
    lines.append("")
    lines.append("**生成方式：** `python scripts/dump-feature-modification-list.py`（数字全部从数据源现算，不手写）  ")
    lines.append("**数据来源：** [`benchmark/foxit-features.csv`](benchmark/foxit-features.csv)（矩阵唯一数据源）、"
                 "[`benchmark/scorecard-debt.csv`](benchmark/scorecard-debt.csv)（欠账清单）、"
                 "[FOXIT_BENCHMARK.md](FOXIT_BENCHMARK.md)（评分卡）  ")
    lines.append("**对账：** `python scripts/check-docs-consistency.py`（本文件的三个数字要与矩阵、评分卡、欠账清单一致）")
    lines.append("")
    lines.append("命令矩阵共 **%d** 条：aligned **%d**、partial **%d**、missing **%d**、excluded **%d**、na **%d**。"
                 % (total, counter["aligned"], counter["partial"], counter["missing"],
                    counter["excluded"], counter["na"]))
    lines.append("")
    lines.append("下面三梯队是**全部还要动的地方**，按「先补最像话的」排序：")
    lines.append("")
    lines.append("| 梯队 | 数量 | 含义 |")
    lines.append("|---|---:|---|")
    lines.append("| 第一梯队：欠账 | %d | 评分卡写了「对齐」，但按它自己的验收列，承诺范围没达成 |" % len(debt))
    lines.append("| 第二梯队：矩阵 partial | %d | 功能在，但有明显降级 |" % len(partial_rows))
    lines.append("| 第三梯队：结构性改动 | 4 | 不是功能缺口，是口径与流程要改 |")
    lines.append("")

    # ---------------- 第一梯队 ----------------
    lines.append("## 第一梯队：欠账 %d 项" % len(debt))
    lines.append("")
    lines.append("这些项在评分卡里是「对齐」，但验收列自己写着边界 —— 按评分卡 R4 第②类（**实现缺口**），")
    lines.append("它们欠的东西必须登记，**欠账没清零就不算超过福昕**。")
    lines.append("")
    lines.append("| 编号 | 能力 | 归因 | 欠什么 | 补齐后的验收方式 |")
    lines.append("|---|---|---|---|---|")
    for row in debt:
        lines.append("| %s | %s | %s | %s | %s |" % (
            row["code"], names.get(row["code"], row["name"]), row["kind"],
            row["owed"], row["acceptance"]))
    lines.append("")
    kind_counter = collections.Counter(r["kind"] for r in debt)
    lines.append("按归因：" + "、".join("%s **%d** 项" % (k, v) for k, v in kind_counter.most_common()) + "。")
    lines.append("")

    # ---------------- 第二梯队 ----------------
    lines.append("## 第二梯队：矩阵 partial %d 项" % len(partial_rows))
    lines.append("")
    lines.append("这些是**命令级**的降级（矩阵严格档），按标签页列出；边界优先取说明列里加粗的那段，没加粗就取整句。")
    lines.append("评分卡里对应的能力可能仍是「对齐」（两档口径：矩阵严格、评分卡宽松）。")
    lines.append("")
    by_tab = collections.defaultdict(list)
    # 同名能力在矩阵里会出现两次（同一个命令挂在两个标签页下），
    # 归因必须一致 —— 按能力名分组，合并说明列再判一次。
    group_text = collections.defaultdict(list)
    for row in partial_rows:
        group_text[row["command"]].append(row["note"])
    group_kind = {}
    for command, notes in group_text.items():
        group_kind[command] = classify(" ".join(notes))[0]
    for row in partial_rows:
        kind = group_kind[row["command"]]
        _unused, evidence = classify(row["note"])
        by_tab[row["tab"]].append((row, kind, evidence))
    for tab in TAB_ORDER:
        items = by_tab.get(tab)
        if not items:
            continue
        lines.append("### %s（%d 条）" % (tab, len(items)))
        lines.append("")
        lines.append("| 能力 | 能力编号 | 归因 | 边界（说明列原文） |")
        lines.append("|---|---|---|---|")
        for row, kind, evidence in items:
            cap = row["capability"] if row["capability"] != "—" else "—"
            text = evidence.replace("|", "／").replace("\n", " ")
            if len(text) > 120:
                text = text[:120] + "…"
            lines.append("| %s | %s | %s | %s |" % (row["command"], cap, kind, text))
        lines.append("")
    tab_counter = collections.Counter(r["tab"] for r in partial_rows)
    lines.append("按标签页：" + "、".join("%s **%d**" % (t, tab_counter[t]) for t in TAB_ORDER if tab_counter[t]) + "。")
    lines.append("")
    kind_counter2 = collections.Counter(group_kind[r["command"]] for r in partial_rows)
    lines.append("按归因：" + "、".join("%s **%d**" % (k, v) for k, v in kind_counter2.most_common()) + "。")
    lines.append("")
    lines.append("> 归因是**按说明列原文自动判**的：先看有没有明确指向外部原因（新依赖 / 真机 / 第三方软件 / AI 输出格式），")
    lines.append("> 没有就归「要新实现」——矩阵里 partial 的定义本来就是「功能在，但降级在我们这边」。")
    lines.append("")

    # ---------------- 第三梯队 ----------------
    lines.append("## 第三梯队：结构性改动（4 项）")
    lines.append("")
    lines.append("这些不是功能缺口，是**口径与流程**要改；不先定下来，下一轮还会重演同样的错位。")
    lines.append("")
    lines.append("| # | 改动 | 为什么必须先有决策 | 完成判据 |")
    lines.append("|---|---|---|---|")
    lines.append("| 1 | 矩阵 schema 改三轴（实现 / 对齐 / 验证） | CSV 是唯一数据源，生成器、一致性守卫、"
                 "全部统计脚本都按 5 态读；改 schema 要连这些一起改一轮 | `build-foxit-matrix.py`、"
                 "`check-docs-consistency.py`、dump 脚本全部改完且门禁全绿 |")
    lines.append("| 2 | 技术栈采纳（QtWebEngine / veraPDF / TWAIN / JS 运行时 / 进程隔离） | "
                 "撞 `docs/DEPENDENCY_POLICY.md`：每个组件要有负责人、钉死版本、许可证、SBOM 条目、更新路径 |"
                 " `DEPENDENCY_POLICY.md` 逐条登记 + SBOM 生成通过 + 依赖门绿 |")
    lines.append("| 3 | 排期映射（整改方案 §7 的阶段 A–D → 具体版本号） | "
                 "「4–6 个版本」没落到 1.x / 2.0.0 任何一格，这正是版本审计里「计划与代码脱节」的成因 |"
                 " 每个 P0 项挂到具体里程碑 + 唯一验收脚本，且 VERSION_AUDIT §5 的三选一已定 |")
    lines.append("| 4 | 界面验收脚本进流水线（`verify-forms.ps1` / `verify-editing.ps1` / `verify-i18n.ps1`） |"
                 " 版本审计 §7 已记录「验收脚本没有全部进 CI」；这三张本地不能跑（会抢桌面），只能由 CI 跑 |"
                 " CI 上三张脚本实跑通过 |")
    lines.append("")
    lines.append("## 怎么用这份清单")
    lines.append("")
    lines.append("- **第一梯队**是评分卡承诺没兑现的部分，补齐方式是写实现 + 写验收，不是改文档；")
    lines.append("  补完一项就从 `scorecard-debt.csv` 删一行（一致性守卫会当场发现对不上）。")
    lines.append("- **第二梯队**是命令级降级，按标签页分批做；「未归类」那几条要先人工判归因再排。")
    lines.append("- **第三梯队**不排版本，先把决策补上；决策没下之前不要写进任何版本计划。")
    lines.append("")

    with io.open(OUT_PATH, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines))
    print("已生成 %s" % os.path.relpath(OUT_PATH, ROOT))
    print("  第一梯队 欠账 %d 项 / 第二梯队 partial %d 条 / 第三梯队 4 项" % (len(debt), len(partial_rows)))
    print("  partial 归因：" + "、".join("%s %d" % (k, v) for k, v in kind_counter2.most_common()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
