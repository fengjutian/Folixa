#!/usr/bin/env python3
"""福昕功能矩阵生成器。

把 `docs/benchmark/foxit-features.csv`（唯一数据源）和
`docs/benchmark/foxit/*.png`（各标签页截图）合成两份文档：

* `docs/FOXIT_FEATURE_MATRIX.md`：功能清单 + 本项目对齐状态 + 差距统计；
* `docs/benchmark/foxit/README.md`：截图档案（标签页、尺寸、SHA-256）。

CSV 列：tab, group, command, status, capability, note
status 只允许：aligned / partial / missing / excluded / na

用法：
    python scripts/build-foxit-matrix.py
    python scripts/build-foxit-matrix.py --check   # 只校验，不写文件
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import struct
import sys
from collections import Counter, OrderedDict
from pathlib import Path

TAB_ORDER = [
    "文件",
    "全局",
    "主页",
    "转换",
    "编辑",
    "页面管理",
    "注释",
    "视图",
    "表单",
    "保护",
    "电子签章",
    "共享",
    "辅助工具",
    "论文工具",
    "文档管理",
    "录制",
    "帮助",
    "AI助手",
]

# 截图文件 → 标签页。文件名前缀决定顺序，改动时一起改这里。
SCREENSHOTS = OrderedDict(
    [
        ("01-file-tab.png", "文件"),
        ("02-home.png", "主页"),
        ("03-convert.png", "转换"),
        ("04-edit.png", "编辑"),
        ("05-page-management.png", "页面管理"),
        ("06-comment.png", "注释"),
        ("07-view.png", "视图"),
        ("08-form.png", "表单"),
        ("09-protect.png", "保护"),
        ("10-sign.png", "电子签章"),
        ("11-share.png", "共享"),
        ("12-accessibility.png", "辅助工具"),
        ("13-thesis.png", "论文工具"),
        ("14-doc-management.png", "文档管理"),
        ("15-record.png", "录制"),
        ("16-help.png", "帮助"),
        ("17-ai-assistant.png", "AI助手"),
    ]
)

STATUS = {
    "aligned": ("✅", "已对齐", "功能在、结果一致、没有明显降级"),
    "partial": ("🟨", "部分", "主路径能走通，深度或入口还差一截"),
    "missing": ("⬜", "未做", "还没有实现，已排进路线或待排"),
    "excluded": ("🚫", "不做", "评分卡明确排除，理由见 FOXIT_BENCHMARK 1.2"),
    "na": ("➖", "非目标", "账号、订阅、云服务或企业集成，不进入产品范围"),
}

CAPTURE_NOTE = """\
**截图来源**：本机安装的 Foxit PDF Editor 2026（Continuous，`2026.1.2.36547`）。
只用它做功能对标，不把它的二进制或资源用作产品依赖；截图不随安装包分发。
"""


def png_size(path: Path) -> tuple[int, int] | None:
    """读 PNG 的 IHDR，拿到宽高；读不出来返回 None。"""
    try:
        with path.open("rb") as handle:
            header = handle.read(24)
        if header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
            return None
        width, height = struct.unpack(">II", header[16:24])
        return width, height
    except OSError:
        return None


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def load_rows(csv_path: Path) -> list[dict[str, str]]:
    with csv_path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        expected = ["tab", "group", "command", "status", "capability", "note"]
        if reader.fieldnames != expected:
            raise SystemExit(f"CSV 表头不对：{reader.fieldnames}，应为 {expected}")
        rows = []
        for line_no, row in enumerate(reader, start=2):
            if None in row or any(value is None for value in row.values()):
                raise SystemExit(f"第 {line_no} 行列数不对（可能有多余的逗号）：{row}")
            status = (row["status"] or "").strip()
            if status not in STATUS:
                raise SystemExit(f"第 {line_no} 行状态非法：{status}")
            if not row["tab"] or not row["command"]:
                raise SystemExit(f"第 {line_no} 行缺少 tab 或 command")
            row["status"] = status
            rows.append(row)
    return rows


def duplicate_commands(rows: list[dict[str, str]]) -> list[str]:
    seen: Counter[tuple[str, str]] = Counter()
    for row in rows:
        seen[(row["tab"], row["command"])] += 1
    return [f"{tab}/{command}" for (tab, command), count in seen.items() if count > 1]


def tab_stats(rows: list[dict[str, str]]) -> dict[str, Counter]:
    stats: dict[str, Counter] = OrderedDict()
    for tab in TAB_ORDER:
        stats[tab] = Counter()
    for row in rows:
        stats.setdefault(row["tab"], Counter())[row["status"]] += 1
    return stats


def render_matrix(rows: list[dict[str, str]], stats: dict[str, Counter]) -> str:
    total = Counter()
    for counter in stats.values():
        total.update(counter)
    total_count = sum(total.values())

    lines: list[str] = []
    lines.append("# 福昕功能矩阵：截图基线与本项目对齐状态")
    lines.append("")
    lines.append(
        "**生成方式：** `python scripts/build-foxit-matrix.py`（数据源 "
        "[`docs/benchmark/foxit-features.csv`](benchmark/foxit-features.csv)）  "
    )
    lines.append("**截图日期：** 2026-11-15  ")
    lines.append("**基线版本：** Foxit PDF Editor 2026.1.2.36547（Continuous）  ")
    lines.append(
        f"**规模：** {len(STATUS)} 种状态 · "
        f"{len([t for t in stats if stats[t] and t != '全局'])} 个标签页 + 1 组全局命令 · "
        f"{total_count} 条命令"
    )
    lines.append("")
    lines.append(
        "这份文档把福昕每个标签页的功能逐条抄下来，并标上本项目的实现状态。"
        "它回答两个问题：**福昕有什么、我们缺什么**。"
        "能力编号（A1、C10……）对应 "
        "[对标基线](FOXIT_BENCHMARK.md) 的 95 项评分卡；标 `—` 表示评分卡没有单列。"
    )
    lines.append("")
    lines.append("> 状态按 **2026-11-15 17:45 的工作区** 判定：包含当时尚未提交、但已经在开发中的"
                 "表单面板、合并/提取与页面装饰入口。"
                 "已实现但入口没接上、或深度明显不足的，按「部分」记。")
    lines.append("")

    lines.append("## 1. 截图档案")
    lines.append("")
    lines.append("![主页 Ribbon](benchmark/foxit/02-home.png)")
    lines.append("")
    lines.append("全部 17 张截图在 [`docs/benchmark/foxit/`](benchmark/foxit/README.md)，"
                 "每张都登记了标签页、像素尺寸与 SHA-256。")
    lines.append("")
    lines.append("| 标签页 | 截图 | 说明 |")
    lines.append("|---|---|---|")
    tab_note = {
        "文件": "文件页 + 最近文档 + 左侧文件命令栏",
        "主页": "打开、阅读、注释、页面、扫描、放映、AI 翻译",
        "转换": "输入、打包、组装、输出格式、OCR、印前",
        "编辑": "文本/对象编辑、插入、链接、书签、附件",
        "页面管理": "页面排序、组装、裁剪、水印/背景/页眉页脚/页码",
        "注释": "标记、标注、绘图、测量、图章、审阅与导入导出",
        "视图": "布局、阅读模式、视觉模式、朗读与比较",
        "表单": "建域、XFA、计算、数据导入导出、JavaScript",
        "保护": "密文、隐藏数据、加密、签名、时间戳、验证",
        "电子签章": "云端签署平台与本地签章",
        "共享": "邮件、协作、审阅与企业管理连接器",
        "辅助工具": "无障碍标签、检查、阅读顺序",
        "论文工具": "论文查重",
        "文档管理": "标签、分类、全文检索、发票与批量",
        "录制": "PDF/屏幕/声音录制与截图",
        "帮助": "手册、插件、激活与订阅",
        "AI助手": "聊天、翻译、朗读、内容检测、生成 PPT",
    }
    for filename, tab in SCREENSHOTS.items():
        lines.append(f"| {tab} | [`{filename}`](benchmark/foxit/{filename}) | "
                     f"{tab_note.get(tab, '')} |")
    lines.append("")

    lines.append("## 2. 状态图例")
    lines.append("")
    lines.append("| 标记 | 状态 | 判定 |")
    lines.append("|---|---|---|")
    for _key, (mark, label, meaning) in STATUS.items():
        lines.append(f"| {mark} | {label} | {meaning} |")
    lines.append("")
    lines.append("对齐率只把 ✅ 算作对齐；🟨 单列，避免「有一个按钮就算对齐」的自我安慰。")
    lines.append("")

    lines.append("## 3. 总览")
    lines.append("")
    lines.append("| 标签页 | 条目 | ✅ 已对齐 | 🟨 部分 | ⬜ 未做 | 🚫 不做 | ➖ 非目标 | 对齐率 |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|---:|")
    for tab in TAB_ORDER:
        counter = stats.get(tab)
        if not counter:
            continue
        count = sum(counter.values())
        aligned = counter["aligned"]
        rate = (aligned / count * 100.0) if count else 0.0
        lines.append(
            f"| {tab} | {count} | {aligned} | {counter['partial']} | "
            f"{counter['missing']} | {counter['excluded']} | {counter['na']} | {rate:.0f}% |"
        )
    lines.append(
        f"| **合计** | **{total_count}** | **{total['aligned']}** | **{total['partial']}** | "
        f"**{total['missing']}** | **{total['excluded']}** | **{total['na']}** | "
        f"**{total['aligned'] / total_count * 100.0:.0f}%** |"
    )
    lines.append("")
    actionable = total["aligned"] + total["partial"] + total["missing"]
    lines.append(
        f"去掉 🚫 与 ➖ 之后，真正要比的是 **{actionable} 条**："
        f"已对齐 {total['aligned']}、部分 {total['partial']}、未做 {total['missing']}。"
    )
    lines.append("")

    lines.append("## 4. 逐标签页清单")
    lines.append("")
    for tab in TAB_ORDER:
        counter = stats.get(tab)
        if not counter:
            continue
        count = sum(counter.values())
        lines.append(f"### {tab}（{count} 条）")
        lines.append("")
        if tab == "全局":
            lines.append("> 手型和选择固定在每个 Ribbon 左侧，这里只记一次。")
            lines.append("")
        lines.append("| 组 | 命令 | 状态 | 能力 | 说明 |")
        lines.append("|---|---|---|---|---|")
        for row in rows:
            if row["tab"] != tab:
                continue
            mark = STATUS[row["status"]][0]
            lines.append(
                f"| {row['group']} | {row['command']} | {mark} | "
                f"{row['capability']} | {row['note']} |"
            )
        lines.append("")

    lines.append("## 5. 差距分析")
    lines.append("")
    lines.append("### 5.1 已对齐的部分")
    lines.append("")
    lines.append(
        "阅读、检索、批注、页面基础操作、打印导出、AI 对话这几块已经能走完整条路："
        "打开 → 读 → 找 → 注 → 存 → 打/导。C 域（批注）在评分卡里是唯一收满的 12/12。"
    )
    lines.append("")
    lines.append("### 5.2 最大的三个缺口")
    lines.append("")
    lines.append(
        "1. **安全与签名（保护 / 电子签章）**：密文、加密、签名、时间戳、验签一条都没落地，"
        "它挡住的是「合同能签能发」这条商业链路。"
    )
    lines.append(
        "2. **页面组装（页面管理 / 转换）**：拆分、替换、裁剪、逆序、合并、水印批量还停在"
        "「插入/删除/旋转/上下移」；福昕这一页有 20 个命令，我们只有 4 个完全对齐。"
    )
    lines.append(
        "3. **文档级工具（附件 / 截图 / 文档管理）**：文档级附件面板、区域截图、"
        "批量重命名和文档属性都缺；这些恰好是高频小工具。"
    )
    lines.append("")
    lines.append("### 5.3 明确不追的部分")
    lines.append("")
    lines.append(
        "云协作、企业内容管理（iManage / SharePoint / eDOCS）、插件 SDK、账号与订阅、"
        "在线查重、发票流程、3D/多媒体、XFA、表单设计器、重排——"
        "共 "
        f"{total['excluded'] + total['na']} 条标成 🚫/➖，理由集中在 "
        "[对标基线 1.2](FOXIT_BENCHMARK.md#12-明确不追的四类)。"
    )
    lines.append("")

    lines.append("## 6. 对齐路线")
    lines.append("")
    lines.append("按「一条用户任务能从头走到尾」排序，而不是按福昕的标签页顺序：")
    lines.append("")
    lines.append("| 批次 | 目标任务 | 要补的命令 | 对应评分卡 |")
    lines.append("|---|---|---|---|")
    lines.append(
        "| P0 | 把一份合同改完、签掉、发出去 | 加密与权限、密文遮盖、元数据清理、"
        "手写签章、证书签名、验签、时间戳 | G1–G7 |"
    )
    lines.append(
        "| P1 | 把几十份 PDF 拼成一本 | 拆分、替换、逆序、交叉合并、裁剪、"
        "调整页面大小、水印/背景/页眉页脚/贝茨编号入口、批量重命名 | D2–D9 |"
    )
    lines.append(
        "| P1 | 在文档里找东西、贴附件、截图 | 文档级附件面板、区域截图、"
        "邮件发送、查找替换、文档属性 | B5、E4、C8 |"
    )
    lines.append(
        "| P2 | 让更多人能用 | 朗读、护眼配色、屏幕阅读器、阅读顺序、"
        "无障碍报告、字数统计 | A9、J1–J5 |"
    )
    lines.append(
        "| P2 | 让 AI 更能干 | 整篇翻译、内容检测、朗读、总结配图、PPT 生成 | L2、L6–L8 |"
    )
    lines.append("")
    lines.append(
        "每一批都要带端到端验收；只把按钮点亮不算对齐。"
        "具体版本的拆分仍以 [版本索引](versions/README.md) 为准。"
    )
    lines.append("")

    lines.append("## 7. 维护方式")
    lines.append("")
    lines.append("1. 换截图：把新图放进 `docs/benchmark/foxit/`，更新生成器里的 `SCREENSHOTS`。")
    lines.append("2. 改状态：只改 CSV 的 `status` 与 `note`，然后重新生成；不要手改本文。")
    lines.append("3. 校验：`python scripts/build-foxit-matrix.py --check` 会检查")
    lines.append("   状态值、重复命令、截图缺失和标签页覆盖。")
    lines.append("4. 新增能力编号：先改 `docs/FOXIT_BENCHMARK.md` 的评分卡，再回填 CSV 的 `capability`。")
    lines.append("")
    lines.append(CAPTURE_NOTE)
    return "\n".join(lines) + "\n"


def render_screenshots_readme() -> str:
    lines = ["# 福昕功能截图档案", ""]
    lines.append(CAPTURE_NOTE)
    lines.append("")
    lines.append("| 标签页 | 文件 | 尺寸 | SHA-256 |")
    lines.append("|---|---|---:|---|")
    base = Path(__file__).resolve().parent.parent / "docs" / "benchmark" / "foxit"
    for filename, tab in SCREENSHOTS.items():
        path = base / filename
        size = png_size(path) if path.exists() else None
        dimension = f"{size[0]}×{size[1]}" if size else "缺失"
        digest = sha256_of(path) if path.exists() else "缺失"
        lines.append(f"| {tab} | `{filename}` | {dimension} | `{digest}` |")
    lines.append("")
    lines.append("## 拍法")
    lines.append("")
    lines.append("1. 打开福昕，切到目标标签页，收起与功能无关的侧栏。")
    lines.append("2. 用 `Win+Shift+S` 只截 Ribbon 本身（高 195px 左右），不要截整屏。")
    lines.append("3. 按标签页顺序命名（`01-file-tab` … `17-ai-assistant`）。")
    lines.append("4. 改完截图重新跑 `python scripts/build-foxit-matrix.py`，哈希会一起更新。")
    lines.append("")
    lines.append("## 约束")
    lines.append("")
    lines.append("- 截图只用于功能对标，不随安装包发布，也不进 `dist/`。")
    lines.append("- 不要把福昕的图标、皮肤或文案抄进产品资源；命令名只作为对标术语出现在文档里。")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="只校验，不写文件")
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    csv_path = root / "docs" / "benchmark" / "foxit-features.csv"
    shot_dir = root / "docs" / "benchmark" / "foxit"
    if not csv_path.exists():
        raise SystemExit(f"找不到数据源：{csv_path}")

    rows = load_rows(csv_path)
    stats = tab_stats(rows)
    problems: list[str] = []

    unknown_tabs = {row["tab"] for row in rows} - set(TAB_ORDER)
    if unknown_tabs:
        problems.append(f"CSV 里有未登记的标签页：{sorted(unknown_tabs)}")
    for filename, tab in SCREENSHOTS.items():
        if not (shot_dir / filename).exists():
            problems.append(f"缺少截图：{filename}（{tab}）")
    duplicated = duplicate_commands(rows)
    if duplicated:
        problems.append(f"同一标签页里命令重复：{duplicated}")

    if problems:
        for problem in problems:
            print(f"[错] {problem}", file=sys.stderr)
        return 1

    if args.check:
        print(f"校验通过：{len(rows)} 条命令 · "
              f"{len([t for t in stats if stats[t] and t != '全局'])} 个标签页 + 全局 · "
              f"{len(SCREENSHOTS)} 张截图")
        return 0

    matrix = render_matrix(rows, stats)
    (root / "docs" / "FOXIT_FEATURE_MATRIX.md").write_text(matrix, encoding="utf-8", newline="\n")
    (shot_dir / "README.md").write_text(render_screenshots_readme(), encoding="utf-8", newline="\n")
    print(f"已生成 docs/FOXIT_FEATURE_MATRIX.md（{len(rows)} 条命令）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
