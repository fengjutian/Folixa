#!/usr/bin/env python3
"""核对翻译目录覆盖率。

扫描 `src/` 里所有需要翻译的调用，和 `src/i18n/*.ts` 里实际有的词条对比：

* **漏翻**：源码里有这条文案，目录里没有（或翻译为空）→ 退出码 1；
* **陈旧**：目录里有，源码里已经找不到 → 打印提示，退出码仍为 0；
* **对不上上下文**：`i18n::tr("上下文", …)` 写的上下文在目录里没有 → 退出码 1。

识别四种写法：
    tr("中文")                                   # 上下文由所在类推断
    i18n::tr("aipdf::pdf::Xml", "中文")
    QCoreApplication::translate("上下文", "中文")
    QT_TRANSLATE_NOOP("上下文", "中文")

用法：
    python scripts/check-i18n.py                 # 检查
    python scripts/check-i18n.py --list          # 逐条列出问题词条
    python scripts/check-i18n.py --ts 某.ts      # 换一份目录
"""
from __future__ import annotations

import argparse
import io
import re
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path

TR_RE = re.compile(r'(?<![\w.])tr\(\s*"((?:[^"\\]|\\.)*)"')
I18N_TR_RE = re.compile(r'i18n::tr\(\s*"([^"]+)"\s*,\s*"((?:[^"\\]|\\.)*)"')
TRANSLATE_RE = re.compile(
    r'QCoreApplication::translate\(\s*"([^"]+)"\s*,\s*"((?:[^"\\]|\\.)*)"')
NOOP_RE = re.compile(r'QT_TRANSLATE_NOOP\(\s*"([^"]+)"\s*,\s*"((?:[^"\\]|\\.)*)"')

NAMESPACE_RE = re.compile(r'\bnamespace\s+([A-Za-z_][\w:]*)\s*\{')
CLASS_DECL_RE = re.compile(r'\b(?:class|struct)\s+([A-Z]\w*)\b')
CLASS_METHOD_RE = re.compile(r'\b([A-Z]\w*)::(\w+)\s*\(')


def strip_comments(text: str) -> str:
    """去掉注释与字符串里不影响判断的干扰：注释整段抹掉，避免文档示例被当成调用。"""
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def namespace_prefix(text: str) -> str:
    """文件级命名空间前缀：取出现次数最多的非匿名 namespace。"""
    names = [m.group(1) for m in NAMESPACE_RE.finditer(text) if m.group(1) != "{"]
    if not names:
        return ""
    # 嵌套写法（`namespace aipdf::app {` 或 `namespace aipdf { namespace app {`）
    # 都归一成第一段最长的那个前缀。
    names.sort(key=len, reverse=True)
    prefix = names[0]
    return prefix


def context_at(text: str, position: int, prefix: str) -> str | None:
    """推断某个位置所在类的上下文（命名空间 + 类名）。"""
    before = text[:position]
    matches = list(CLASS_METHOD_RE.finditer(before))
    if matches:
        return qualify(prefix, matches[-1].group(1))
    matches = list(CLASS_DECL_RE.finditer(before))
    if matches:
        return qualify(prefix, matches[-1].group(1))
    return None


def qualify(prefix: str, name: str) -> str:
    if not prefix:
        return name
    return f"{prefix}::{name}"


def load_catalog(path: Path) -> dict[str, set[str]]:
    tree = ET.parse(path)
    catalog: dict[str, set[str]] = defaultdict(set)
    for context in tree.getroot().findall("context"):
        name = (context.findtext("name") or "").strip()
        for message in context.findall("message"):
            source = message.findtext("source") or ""
            translation = message.find("translation")
            if translation is None:
                continue
            if translation.get("type") in {"unfinished", "obsolete", "vanished"}:
                continue
            text = translation.text or ""
            if text.strip():
                catalog[name].add(source)
    return catalog


def collect_usages(src: Path) -> tuple[dict[tuple[str, str], list[str]], list[str]]:
    """返回 {(上下文, 源文案): [出现位置]} 与「上下文推不出来的位置」。"""
    usages: dict[tuple[str, str], list[str]] = defaultdict(list)
    unknown: list[str] = []
    for path in sorted(src.rglob("*")):
        if path.suffix not in {".cpp", ".h"}:
            continue
        text = strip_comments(io.open(path, encoding="utf-8").read())
        prefix = namespace_prefix(text)
        for match in I18N_TR_RE.finditer(text):
            usages[(match.group(1), match.group(2))].append(f"{path}:{line_of(text, match.start())}")
        for match in TRANSLATE_RE.finditer(text):
            usages[(match.group(1), match.group(2))].append(f"{path}:{line_of(text, match.start())}")
        for match in NOOP_RE.finditer(text):
            usages[(match.group(1), match.group(2))].append(f"{path}:{line_of(text, match.start())}")
        for match in TR_RE.finditer(text):
            context = context_at(text, match.start(), prefix)
            location = f"{path}:{line_of(text, match.start())}"
            if context is None:
                unknown.append(location)
                continue
            usages[(context, match.group(1))].append(location)
    return usages, unknown


def line_of(text: str, position: int) -> int:
    return text.count("\n", 0, position) + 1


def main() -> int:
    parser = argparse.ArgumentParser(description="检查 .ts 翻译目录覆盖率")
    parser.add_argument("--src", default="src", help="源码目录（默认 src）")
    parser.add_argument("--ts", default="src/i18n/ai-pdf-editor_en.ts",
                        help="翻译目录（默认英文目录）")
    parser.add_argument("--list", action="store_true", help="逐条列出问题")
    args = parser.parse_args()

    src = Path(args.src)
    catalog_path = Path(args.ts)
    if not catalog_path.exists():
        print(f"找不到翻译目录：{catalog_path}")
        return 1

    catalog = load_catalog(catalog_path)
    usages, unknown = collect_usages(src)

    catalog_sources = {source for sources in catalog.values() for source in sources}

    missing: list[tuple[str, str, list[str]]] = []
    mismatched_context: list[tuple[str, str, list[str]]] = []
    for (context, source), locations in usages.items():
        if source in catalog.get(context, set()):
            continue
        if source in catalog_sources:
            # 文案有翻译，但登记的上下文对不上：运行时查不到这条翻译。
            mismatched_context.append((context, source, locations))
        else:
            missing.append((context, source, locations))

    stale = sorted(catalog_sources - {source for _, source in usages})

    total = len(usages)
    print(f"检查 {src} → {catalog_path}")
    print(f"源码里的待翻译文案：{total} 条（{len(usages) - len(missing) - len(mismatched_context)} 条已覆盖）")
    print(f"目录里的词条：{sum(len(sources) for sources in catalog.values())} 条")

    if missing:
        print(f"\n漏翻 {len(missing)} 条：")
        for context, source, locations in sorted(missing):
            print(f"  [{context}] {source}")
            if args.list:
                for location in locations:
                    print(f"      {location}")
    if mismatched_context:
        print(f"\n上下文对不上 {len(mismatched_context)} 条（目录里有这条文案，但上下文不同）：")
        for context, source, locations in sorted(mismatched_context):
            print(f"  [{context}] {source}")
            if args.list:
                for location in locations:
                    print(f"      {location}")
    if stale:
        print(f"\n目录里陈旧、源码里已找不到 {len(stale)} 条（不影响运行，建议清理）：")
        for source in stale:
            print(f"  {source}")
    if unknown:
        print(f"\n有 {len(unknown)} 处 tr() 的上下文推断不出来（请改成 i18n::tr(\"上下文\", …)）：")
        for location in unknown:
            print(f"  {location}")

    failed = bool(missing or mismatched_context or unknown)
    print("\n结论：" + ("有问题，见上。" if failed else "全部覆盖。"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
