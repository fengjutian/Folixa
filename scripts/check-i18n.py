#!/usr/bin/env python3
"""核对翻译目录覆盖率。

扫描 `src/` 里所有需要翻译的调用，和 `src/i18n/*.ts` 里实际有的词条对比：

* **漏翻**：源码里有这条文案，目录里没有（或翻译为空）→ 退出码 1；
* **上下文对不上**：目录里有这条文案，但登记的上下文不同（运行时会查不到）→ 退出码 1；
* **推断不出上下文**：`tr()` 所在函数认不出来 → 退出码 1；
* **陈旧**：目录里有，源码里已经找不到 → 只提示，不影响退出码。

识别四种写法：

    tr("中文")                                   # 上下文由所在类推断
    i18n::tr("aipdf::pdf::Xml", "中文")
    QCoreApplication::translate("上下文", "中文")
    QT_TRANSLATE_NOOP("上下文", "中文")

字面量可以跨行拼接（`tr("上半句"\n   "下半句")`），转义（`\\n`、`\\"`）会还原成
真实字符再和目录比对——目录里的 <source> 存的就是运行时字符串。

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

# Windows 控制台默认是 GBK，打印中文/符号（如 ▸）会抛 UnicodeEncodeError，
# 让一条「只是提示陈旧词条」的输出把门禁判成失败。统一按 UTF-8 输出，
# 无法编码的字符替换掉而不是中断检查。
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

C_STRING = r'"(?:[^"\\]|\\.)*"'
CONCAT = rf'({C_STRING}(?:\s*{C_STRING})*)'

TR_RE = re.compile(rf'(?<![\w.])tr\(\s*{CONCAT}')
I18N_TR_RE = re.compile(rf'i18n::tr\(\s*({C_STRING})\s*,\s*{CONCAT}')
TRANSLATE_RE = re.compile(
    rf'QCoreApplication::translate\(\s*({C_STRING})\s*,\s*{CONCAT}')
NOOP_RE = re.compile(rf'QT_TRANSLATE_NOOP\(\s*({C_STRING})\s*,\s*{CONCAT}')
LITERAL_RE = re.compile(C_STRING)

NAMESPACE_RE = re.compile(r'\bnamespace\s+([A-Za-z_][\w:]*)\s*\{')
CLASS_DECL_RE = re.compile(r'^(?:class|struct)\s+([A-Z]\w*)\b', re.MULTILINE)
# 函数定义写在行首（本仓库的代码风格）：`void MainWindow::saveDocument() {`、
# 构造函数 `MainWindow::MainWindow(QWidget* parent)`。只认行首的 `Class::method(`，
# 避免把表达式里的 `QString::fromUtf8(`、`QTimer::singleShot(` 当成「当前类」。
CLASS_METHOD_RE = re.compile(
    r'^(?:[A-Za-z_][\w:<>,*&\s]*?)?\b([A-Z]\w*)::(~?\w+)\s*\(', re.MULTILINE)

UNESCAPES = {
    'n': '\n',
    't': '\t',
    'r': '\r',
    '0': '\0',
    '"': '"',
    "'": "'",
    '\\': '\\',
    '?': '?',
    'a': '\a',
    'b': '\b',
    'f': '\f',
    'v': '\v',
}


def strip_comments(text: str) -> str:
    """去掉注释，但**不碰字符串里的内容**。

    不能直接拿正则删 `//`：文案里就有 `http://` / `https://`，
    删了会把「服务地址必须以 http:// 或 https:// 开头」截成半句。
    换行保留，行号才对得上。
    """
    out: list[str] = []
    index = 0
    length = len(text)
    while index < length:
        char = text[index]
        following = text[index + 1] if index + 1 < length else ''
        if char == '/' and following == '/':
            while index < length and text[index] != '\n':
                index += 1
            continue
        if char == '/' and following == '*':
            index += 2
            while index < length and not (text[index] == '*' and index + 1 < length
                                          and text[index + 1] == '/'):
                if text[index] == '\n':
                    out.append('\n')
                index += 1
            index = min(index + 2, length)
            out.append(' ')
            continue
        if char in {'"', "'"}:
            quote = char
            out.append(char)
            index += 1
            while index < length:
                out.append(text[index])
                if text[index] == '\\' and index + 1 < length:
                    out.append(text[index + 1])
                    index += 2
                    continue
                if text[index] == quote:
                    index += 1
                    break
                index += 1
            continue
        out.append(char)
        index += 1
    return ''.join(out)


def unescape_literal_group(group: str) -> str:
    """把 `"上半句" "下半句"` 还原成运行时字符串（含真实换行与转义字符）。"""
    pieces: list[str] = []
    for match in LITERAL_RE.finditer(group):
        body = match.group(0)[1:-1]
        piece: list[str] = []
        index = 0
        while index < len(body):
            char = body[index]
            if char == '\\' and index + 1 < len(body):
                escaped = body[index + 1]
                piece.append(UNESCAPES.get(escaped, escaped))
                index += 2
                continue
            piece.append(char)
            index += 1
        pieces.append(''.join(piece))
    return ''.join(pieces)


def namespace_prefix(text: str) -> str:
    names = [m.group(1) for m in NAMESPACE_RE.finditer(text)]
    if not names:
        return ""
    names.sort(key=len, reverse=True)
    return names[0]


def qualify(prefix: str, name: str) -> str:
    return f"{prefix}::{name}" if prefix else name


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


def line_of(text: str, position: int) -> int:
    return text.count("\n", 0, position) + 1


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
            if (translation.text or "").strip():
                catalog[name].add(source)
    return catalog


def collect_usages(src: Path) -> tuple[dict[tuple[str, str], list[str]], list[str]]:
    usages: dict[tuple[str, str], list[str]] = defaultdict(list)
    unknown: list[str] = []
    for path in sorted(src.rglob("*")):
        if path.suffix not in {".cpp", ".h"}:
            continue
        text = strip_comments(io.open(path, encoding="utf-8").read())
        prefix = namespace_prefix(text)
        explicit = list(I18N_TR_RE.finditer(text)) + list(TRANSLATE_RE.finditer(text)) \
            + list(NOOP_RE.finditer(text))
        for match in explicit:
            usages[(unescape_literal_group(match.group(1)),
                    unescape_literal_group(match.group(2)))].append(
                f"{path}:{line_of(text, match.start())}")
        for match in TR_RE.finditer(text):
            # `i18n::tr("上下文", …)` 里的 `tr(` 也会被 TR_RE 匹配到，
            # 但它的第一个参数是上下文不是文案：交给 I18N_TR_RE。
            if text[max(0, match.start() - 6):match.start()] == "i18n::":
                continue
            location = f"{path}:{line_of(text, match.start())}"
            context = context_at(text, match.start(), prefix)
            if context is None:
                unknown.append(location)
                continue
            usages[(context, unescape_literal_group(match.group(1)))].append(location)
    return usages, unknown


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
    mismatched: list[tuple[str, str, list[str]]] = []
    for (context, source), locations in usages.items():
        if source in catalog.get(context, set()):
            continue
        (mismatched if source in catalog_sources else missing).append(
            (context, source, locations))

    stale = sorted(catalog_sources - {source for _, source in usages})

    covered = len(usages) - len(missing) - len(mismatched)
    print(f"检查 {src} → {catalog_path}")
    print(f"源码里的待翻译文案：{len(usages)} 条（{covered} 条已覆盖）")
    print(f"目录里的词条：{sum(len(sources) for sources in catalog.values())} 条")

    if missing:
        print(f"\n漏翻 {len(missing)} 条：")
        for context, source, locations in sorted(missing):
            print(f"  [{context}] {source}")
            if args.list:
                for location in locations:
                    print(f"      {location}")
    if mismatched:
        print(f"\n上下文对不上 {len(mismatched)} 条（目录里有这条文案，但上下文不同）：")
        for context, source, locations in sorted(mismatched):
            print(f"  [{context}] {source}")
            if args.list:
                for location in locations:
                    print(f"      {location}")
    if unknown:
        print(f"\n推断不出上下文 {len(unknown)} 处（请改成 i18n::tr(\"上下文\", …)）：")
        for location in unknown:
            print(f"  {location}")
    if stale:
        print(f"\n目录里陈旧、源码里已找不到 {len(stale)} 条（不影响运行，建议清理）：")
        for source in stale:
            print(f"  {source}")

    failed = bool(missing or mismatched or unknown)
    print("\n结论：" + ("有问题，见上。" if failed else "全部覆盖。"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
