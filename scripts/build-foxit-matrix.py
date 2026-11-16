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
    lines.append("> 状态按 **2026-11-16 重算**（基线提交 `bf6671a`，含当日页面组装与表单两批实现）："
                 "逐条回到 `src/` 里核"
                 "「底层实现 → 能否写盘 → 界面入口 → 验收」四层，四层齐备才算「已对齐」。"
                 "只实现了一半、或按钮在但功能不对口的，按「部分」记。")
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
    lines.append(
        "2026-11-16 重算新增的已对齐能力：改字与改对象（就地编辑 + 文字/对象属性）、"
        "添加链接与自动识别网页链接、复制页 / 调整页面大小 / 页面背景、加密与权限位、"
        "证书签名与验签与时间戳、本地论文查重与查重历史、无障碍检查与辅助功能报告、"
        "应用内用户手册 / 命令行帮助 / 关于对话框。"
    )
    lines.append("")
    lines.append(
        "同日第二批（页面组装 + 表单）又补上：页面拆分四种口径（范围 / 每 N 页 / 按书签 / "
        "按体积）、任意位置移动页、替换页、裁剪页面、贝茨编号与自定义页码格式、"
        "从其他文件插入页、表格检测；表单侧则是建域全类型（经典 xref 与交叉引用流都能写）、"
        "单选按钮 /Parent 分组、Code128 真条码、表单域 JS 子集与「计算次序」、"
        "XFDF 表单数据、表单模板与「重置表单」、表单工具设置与 AI 设计助手。"
        "**后两项缺专项单测，命令矩阵里仍记「部分」而不是「已对齐」。**"
    )
    lines.append("")
    lines.append("### 5.2 最大的三个缺口")
    lines.append("")
    lines.append(
        "1. **转换与合规交付（转换 / AI助手）**：转换 25 条只对齐 5 条——PDF→Office、"
        "HTML 导出、印前检查、PDF 转曲、整页栅格化、PDF/A 合规校验器都没有；"
        "AI 助手 10 条只对齐「对话」1 条，朗读、内容检测、PPT 生成全空。"
    )
    lines.append(
        "2. **保护与签章（保护 / 电子签章）**：两个页签加起来 28 条，只对齐 2 条——"
        "加密、权限、遮盖、元数据清理全是「部分」；证书签名、验签、时间戳依赖随包 pyHanko，"
        "界面只弹原始输出；密文四件套（标记为密文 / 马赛克 / 擦除 / 应用密文）一条没做。"
    )
    lines.append(
        "3. **无障碍与朗读（辅助工具 / 视图）**：朗读彻底没做；阅读顺序只按文本层输出，"
        "不读 StructTreeRoot 标签树；Ribbon 按钮没有 accessibleName，屏幕阅读器读不出主命令区；"
        "护眼与夜间配色没有，只有浅色主题。"
    )
    lines.append("")
    lines.append("### 5.3 明确不追的部分")
    lines.append("")
    lines.append(
        "云协作、企业内容管理（iManage / SharePoint / eDOCS）、插件 SDK、账号与订阅、"
        "3D/多媒体/作品集、XFA 动态表单、文本重排——共 "
        f"{total['excluded'] + total['na']} 条标成 🚫/➖，理由集中在 "
        "[对标基线 1.2](FOXIT_BENCHMARK.md#12-明确不追的四类)。"
        "**原口径里的「在线查重、发票流程、表单设计器」已不在此列**："
        "查重改成本地实现并已对齐，发票六条命令早已有真实入口，表单建域也已落地并对齐。"
    )
    lines.append("")

    lines.append("## 6. 对齐路线")
    lines.append("")
    lines.append("按「一条用户任务能从头走到尾」排序，而不是按福昕的标签页顺序：")
    lines.append("")
    lines.append("| 批次 | 目标任务 | 要补的命令 | 对应评分卡 |")
    lines.append("|---|---|---|---|")
    lines.append(
        "| P0 | 把一份合同改完、签掉、发出去 | 密文四件套（标记为密文 / 马赛克 / 擦除 / 应用密文）、"
        "批量加密、保护设置面板、签名列表与证书链 UI | G1–G7 |"
    )
    lines.append(
        "| P1 | 把几十份 PDF 拼成一本 | 页面标签 /PageLabels、书签排序与层级编辑的专项单测、"
        "批处理 JSON 报告 | D2–D9 |"
    )
    lines.append(
        "| P1 | 转换出能交付的合规格式 | PDF→Office、HTML 导出、印前检查、PDF 转曲、"
        "整页栅格化 PDF、PDF/A 合规校验器 | H5–H8 |"
    )
    lines.append(
        "| P1 | 在文档里找东西、贴附件、截图 | 文档级附件面板、区域截图、查找替换、"
        "文档属性、拼写检查 | B5、E4、C8 |"
    )
    lines.append(
        "| P2 | 让更多人能用 | 朗读、护眼配色、屏幕阅读器可读（Ribbon accessibleName）、"
        "真实标签阅读顺序、字数统计 | A9、J1–J5 |"
    )
    lines.append(
        "| P2 | 让 AI 更能干 | 整篇翻译、内容检测、朗读、总结配图、PPT 生成 | L2、L6–L8 |"
    )
    lines.append("")
    lines.append(
        "每一批都要带端到端验收；只把按钮点亮不算对齐。"
        "依赖外部引擎的命令（LibreOffice / Ghostscript / pyHanko / ofd2pdf）"
        "要在界面上标注前提，缺引擎时禁用入口而不是运行时才报错。"
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
