#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""独立校验 PDF → Word / PowerPoint / Excel 的产物（图片、表格、页数）。

为什么要有这道门
----------------
包是本仓库自己写的（自己的 zip 写入器 + 自己拼的 XML）。
**自己写、自己读不算验证** —— 写错了自己也读不出来，
所以这里用**别的实现**把产物重新读一遍：

* python-docx：第三方 Word 实现，读段落、内嵌图片、表格；
* python-pptx：第三方 PowerPoint 实现，读幻灯片、图片、表格；
* openpyxl：第三方 Excel 实现，读工作表与单元格的值。

没装这些包时不算失败：Python 标准库 zipfile + ElementTree 那几项
已经能抓住「zip 结构不对」「XML 语法错」「部件缺失」「关系号对不上」。

三种格式各自的判据
------------------
* **DOCX**：包里的 word/media/* 部件数、正文里 <w:drawing> 的个数、
  关系文件里的 rId 与正文里的 r:embed 是否一一对应、
  <w:tbl> 的个数与行列数、显式分页符的个数（页数）。
* **PPTX**：幻灯片里的 <p:pic> 与 <a:tbl> 的个数、表格的行列数、
  幻灯片张数（页数，文字多的页会拆成「（续）」，所以是下限）。
* **XLSX**：工作表张数、表格工作表里的表头与首行数据。

语料与期望值
------------
语料由 scripts/gen-office-corpus.py 生成，同时写出 corpus.meta.json：
每份语料该有几页、几张图、几个表、表格几行几列、左栏与右栏各是哪句话，
都是**生成时定死的标准答案**。这里拿它当标准答案去对。

负例对照
--------
光有「正例通过」不算数：校验器太松也会一直绿。
所以这里每次跑都会**故意弄坏产物**（删一个图片部件、改坏一个关系号、
删掉一张表），确认校验器**当场报出问题**；负例没红就整体判失败。

用法：
    python scripts/verify-office-images.py --corpus build/office-corpus \\
        --dump build/office-export --exporter build/debt-h5/bin/OfficeImagesTests.exe
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# OOXML 命名空间。
NS_W = "{http://schemas.openxmlformats.org/wordprocessingml/2006/main}"
NS_R = "{http://schemas.openxmlformats.org/officeDocument/2006/relationships}"
NS_P = "{http://schemas.openxmlformats.org/presentationml/2006/main}"
NS_A = "{http://schemas.openxmlformats.org/drawingml/2006/main}"


# ---------------------------------------------------------------------------
# 小工具
# ---------------------------------------------------------------------------

def compact(text: str) -> str:
    """去掉所有空白：比对文字时用。

    为什么去空白：导出的文字里有「按词距补出来的空格」（"Right column
    prose line seven ." 这种），那是词距启发式的既有行为，与「内容对不对」
    无关。仓库自己的 CJK 语料验收也是「去空白后子串命中即可」。
    """
    return re.sub(r"\s+", "", text)


class Report:
    """一次校验的结论。"""

    def __init__(self, label: str):
        self.label = label
        self.problems: list[str] = []
        self.notes: list[str] = []

    def check(self, condition: bool, message: str) -> bool:
        if not condition:
            self.problems.append("%s：%s" % (self.label, message))
        return condition

    def note(self, message: str) -> None:
        self.notes.append("%s：%s" % (self.label, message))


def optional_import(name: str):
    """导入一个可选的第三方库；没有就返回 None（并说明降级原因）。"""
    try:
        return __import__(name)
    except Exception as error:  # noqa: BLE001 - 缺包是正常情况
        print("  （未装 %s，跳过「真实现打开」这一项：%s）" % (name, error))
        return None


# ---------------------------------------------------------------------------
# DOCX
# ---------------------------------------------------------------------------

def docx_facts(path: Path) -> dict:
    """从 DOCX 包里读出事实（只用标准库）。"""
    facts = {
        "media": [],
        "names": [],
        "drawings": 0,
        "tables": 0,
        "table_shapes": [],
        "page_breaks": 0,
        "embeds": [],
        "relationships": {},
        "paragraphs": [],
        "text": "",
        "ok_xml": True,
    }
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        facts["names"] = names
        facts["media"] = sorted(n for n in names if n.startswith("word/media/"))
        for name in names:
            if name.endswith(".xml") or name.endswith(".rels"):
                try:
                    ET.fromstring(archive.read(name))
                except ET.ParseError as error:
                    facts["ok_xml"] = False
                    facts.setdefault("bad_xml", []).append("%s：%s" % (name, error))
        document = ""
        if "word/document.xml" in names:
            document = archive.read("word/document.xml").decode("utf-8", "replace")
        facts["drawings"] = document.count("<w:drawing>")
        facts["tables"] = document.count("<w:tbl>")
        facts["page_breaks"] = len(re.findall(r'<w:br[^>]*w:type="page"', document))
        facts["embeds"] = re.findall(r'r:embed="([^"]+)"', document)
        facts["paragraphs"] = re.findall(r"<w:t[^>]*>(.*?)</w:t>", document, re.S)
        # 按段落分组：判「左右两栏是不是被并成一段」必须在段落粒度上看，
        # 在整篇文本上看的话，左右栏各占一段也会被拼在一起。
        facts["blocks"] = ["".join(re.findall(r"<w:t[^>]*>(.*?)</w:t>", block, re.S))
                           for block in re.findall(r"<w:p[ >].*?</w:p>", document, re.S)]
        facts["text"] = "".join(facts["paragraphs"])
        rels_name = "word/_rels/document.xml.rels"
        if rels_name in names:
            rels = archive.read(rels_name).decode("utf-8", "replace")
            for rid, target in re.findall(r'Id="([^"]+)"[^>]*Target="([^"]+)"', rels):
                facts["relationships"][rid] = target
    return facts


def verify_docx(path: Path, expected: dict, report: Report) -> dict:
    """校验一份 DOCX；返回事实（给负例对照用）。"""
    label = "DOCX %s" % path.name
    if not path.exists():
        report.check(False, "%s 不存在" % label)
        return {}
    facts = docx_facts(path)
    report.check(facts.get("ok_xml", True), "包里有 XML 解析不过的部件：%s" %
                 facts.get("bad_xml"))

    # 图片：部件数与正文里的引用数必须一致，且都要等于期望值。
    report.check(len(facts["media"]) == expected["images"],
                 "包里的图片部件 %d 个，应为 %d 个" % (len(facts["media"]), expected["images"]))
    report.check(facts["drawings"] == expected["images"],
                 "正文里的 <w:drawing> %d 个，应为 %d 个" % (facts["drawings"], expected["images"]))
    report.check(len(facts["embeds"]) == expected["images"],
                 "正文里的 r:embed 引用 %d 个，应为 %d 个" % (len(facts["embeds"]), expected["images"]))
    # 关系号必须指向真实存在的部件。
    for rid in facts["embeds"]:
        report.check(rid in facts["relationships"], "正文引用了 %s，关系文件里没有它" % rid)
        target = facts["relationships"].get(rid, "")
        if target:
            part = "word/" + target.lstrip("/")
            report.check(part in facts["names"], "关系 %s 指向的部件 %s 不在包里" % (rid, part))

    # 表格：个数与行列数。
    report.check(facts["tables"] == expected["tables"],
                 "<w:tbl> %d 个，应为 %d 个" % (facts["tables"], expected["tables"]))

    # 页数：页与页之间插的是显式分页符。
    report.check(facts["page_breaks"] + 1 == expected["pages"],
                 "分页符 %d 个（页数 %d），应为 %d 页" %
                 (facts["page_breaks"], facts["page_breaks"] + 1, expected["pages"]))

    # 文字：左栏与右栏的锚点都要在，而且**不能**在同一个段落里
    # （在同一段里就是双栏被串行了）。
    text = compact(facts["text"])
    blocks = [compact(block) for block in facts["blocks"]]
    for side in ("left", "right"):
        anchor = expected.get("anchors", {}).get(side)
        if not anchor:
            continue
        report.check(compact(anchor) in text, "正文里找不到%s的锚点「%s」" % (side, anchor))
        # 锚点每页各出现一次（每页的左栏第一行都是同一句）。
        hits = [block for block in blocks if compact(anchor) in block]
        report.check(len(hits) == expected["pages"],
                     "有 %d 个段落含%s的锚点，应为 %d 个（每页一个）" %
                     (len(hits), side, expected["pages"]))
    if expected.get("anchors", {}).get("left") and expected["anchors"].get("right"):
        left = compact(expected["anchors"]["left"])
        right = compact(expected["anchors"]["right"])
        merged = [block for block in blocks if left in block and right in block]
        report.check(not merged,
                     "左右两栏被并成了一段（同一个段落里同时有「%s」与「%s」）" %
                     (expected["anchors"]["left"], expected["anchors"]["right"]))

    # 表格的行列数（用 python-docx 读，第三方实现）。
    docx = optional_import("docx")
    if docx is None:
        report.note("没有 python-docx，跳过表格行列数与图片形状的核对")
        return facts
    try:
        document = docx.Document(str(path))
    except Exception as error:  # noqa: BLE001 - 第三方实现打不开就是失败
        report.check(False, "python-docx 打不开这个包：%s" % error)
        return facts
    report.check(len(document.tables) == expected["tables"],
                 "python-docx 读到 %d 张表，应为 %d 张" % (len(document.tables), expected["tables"]))
    for index, table in enumerate(document.tables):
        report.check(len(table.rows) == expected["table_rows"],
                     "第 %d 张表 %d 行，应为 %d 行" % (index + 1, len(table.rows),
                                                  expected["table_rows"]))
        report.check(len(table.columns) == expected["table_columns"],
                     "第 %d 张表 %d 列，应为 %d 列" % (index + 1, len(table.columns),
                                                  expected["table_columns"]))
    report.check(len(document.inline_shapes) == expected["images"],
                 "python-docx 读到 %d 张内嵌图片，应为 %d 张" %
                 (len(document.inline_shapes), expected["images"]))
    # 段落级的锚点检查：左栏与右栏的锚点必须在**不同的段落**里。
    paragraphs = [compact(paragraph.text) for paragraph in document.paragraphs]
    for side in ("left", "right"):
        anchor = expected.get("anchors", {}).get(side)
        if not anchor:
            continue
        hits = [index for index, text in enumerate(paragraphs) if compact(anchor) in text]
        report.check(len(hits) == expected["pages"],
                     "python-docx 读到 %d 个段落含%s的锚点，应为 %d 个（每页一个）" %
                     (len(hits), side, expected["pages"]))
    if expected.get("anchors", {}).get("left") and expected["anchors"].get("right"):
        left = expected["anchors"]["left"]
        right = expected["anchors"]["right"]
        merged = [paragraph for paragraph in paragraphs
                  if compact(left) in paragraph and compact(right) in paragraph]
        report.check(not merged, "python-docx 读到左右两栏在同一段里（双栏串行了）")
    return facts


# ---------------------------------------------------------------------------
# PPTX
# ---------------------------------------------------------------------------

def verify_pptx(path: Path, expected: dict, report: Report) -> dict:
    """校验一份 PPTX。"""
    label = "PPTX %s" % path.name
    if not path.exists():
        report.check(False, "%s 不存在" % label)
        return {}
    facts = {"pictures": 0, "tables": 0, "table_shapes": [], "slides": 0, "text": ""}
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        for name in names:
            if name.endswith(".xml") or name.endswith(".rels"):
                try:
                    ET.fromstring(archive.read(name))
                except ET.ParseError as error:
                    report.check(False, "%s 的 %s 不是合法 XML：%s" % (label, name, error))
        slide_names = sorted(n for n in names if re.fullmatch(r"ppt/slides/slide\d+\.xml", n))
        facts["slides"] = len(slide_names)
        for name in slide_names:
            body = archive.read(name).decode("utf-8", "replace")
            facts["pictures"] += body.count("<p:pic>")
            facts["tables"] += body.count("<a:tbl>")
            facts["text"] += "".join(re.findall(r"<a:t>(.*?)</a:t>", body, re.S))
        media = [n for n in names if n.startswith("ppt/media/")]

    report.check(len(media) == expected["images"],
                 "包里的图片部件 %d 个，应为 %d 个" % (len(media), expected["images"]))
    report.check(facts["pictures"] == expected["images"],
                 "幻灯片里的 <p:pic> %d 个，应为 %d 个" % (facts["pictures"], expected["images"]))
    report.check(facts["tables"] == expected["tables"],
                 "幻灯片里的 <a:tbl> %d 个，应为 %d 个" % (facts["tables"], expected["tables"]))
    report.check(facts["slides"] >= expected["pages"],
                 "幻灯片 %d 张，应至少 %d 张（文字多的页会拆成「（续）」）" %
                 (facts["slides"], expected["pages"]))

    pptx = optional_import("pptx")
    if pptx is None:
        report.note("没有 python-pptx，跳过幻灯片图片/表格形状的核对")
        return facts
    from pptx import Presentation  # type: ignore
    from pptx.enum.shapes import MSO_SHAPE_TYPE  # type: ignore

    try:
        deck = Presentation(str(path))
    except Exception as error:  # noqa: BLE001 - 第三方实现打不开就是失败
        report.check(False, "python-pptx 打不开这个包：%s" % error)
        return facts
    report.check(len(deck.slides) == facts["slides"],
                 "python-pptx 读到 %d 张幻灯片，与包里的 %d 张对不上" %
                 (len(deck.slides), facts["slides"]))
    pictures = 0
    tables = 0
    text = ""
    for slide in deck.slides:
        for shape in slide.shapes:
            if shape.shape_type == MSO_SHAPE_TYPE.PICTURE:
                pictures += 1
            if getattr(shape, "has_table", False):
                tables += 1
                table = shape.table
                report.check(len(table.rows) == expected["table_rows"],
                             "幻灯片里的表 %d 行，应为 %d 行" % (len(table.rows),
                                                          expected["table_rows"]))
                report.check(len(table.columns) == expected["table_columns"],
                             "幻灯片里的表 %d 列，应为 %d 列" % (len(table.columns),
                                                          expected["table_columns"]))
            if getattr(shape, "has_text_frame", False):
                text += shape.text_frame.text
    report.check(pictures == expected["images"],
                 "python-pptx 读到 %d 张图片，应为 %d 张" % (pictures, expected["images"]))
    report.check(tables == expected["tables"],
                 "python-pptx 读到 %d 张表，应为 %d 张" % (tables, expected["tables"]))
    compact_text = compact(text)
    for side in ("left", "right"):
        anchor = expected.get("anchors", {}).get(side)
        if not anchor:
            continue
        report.check(compact(anchor) in compact_text,
                     "幻灯片文字里找不到%s的锚点「%s」" % (side, anchor))
    return facts


# ---------------------------------------------------------------------------
# XLSX
# ---------------------------------------------------------------------------

def verify_xlsx(path: Path, expected: dict, report: Report) -> dict:
    """校验一份 XLSX。"""
    label = "XLSX %s" % path.name
    if not path.exists():
        report.check(False, "%s 不存在" % label)
        return {}
    facts = {"sheets": 0, "table_sheets": 0, "text": ""}
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        for name in names:
            if name.endswith(".xml") or name.endswith(".rels"):
                try:
                    ET.fromstring(archive.read(name))
                except ET.ParseError as error:
                    report.check(False, "%s 的 %s 不是合法 XML：%s" % (label, name, error))
        facts["sheets"] = len([n for n in names
                                if re.fullmatch(r"xl/worksheets/sheet\d+\.xml", n)])

    openpyxl = optional_import("openpyxl")
    if openpyxl is None:
        report.note("没有 openpyxl，跳过工作表内容与表头的核对")
        return facts
    try:
        book = openpyxl.load_workbook(path)
    except Exception as error:  # noqa: BLE001 - 第三方实现打不开就是失败
        report.check(False, "openpyxl 打不开这个包：%s" % error)
        return facts
    report.check(len(book.sheetnames) == facts["sheets"],
                 "openpyxl 读到 %d 张表，与包里的 %d 张对不上" % (len(book.sheetnames), facts["sheets"]))
    header_seen = 0
    text = ""
    for name in book.sheetnames:
        sheet = book[name]
        rows = [[cell.value for cell in row] for row in sheet.iter_rows()]
        if not rows:
            continue
        text += " ".join(str(value) for row in rows for value in row if value is not None)
        if rows[0][:3] == ["Item", "Qty", "Price"]:
            header_seen += 1
            report.check(len(rows) >= 2 and rows[1][:3] == ["Apple", "12", "3.50"],
                         "表格工作表「%s」的第一行数据不对：%s" % (name, rows[1] if len(rows) > 1 else None))
            report.check(len(rows) == expected["table_rows"],
                         "表格工作表「%s」有 %d 行，应为 %d 行" % (name, len(rows),
                                                         expected["table_rows"]))
    facts["table_sheets"] = header_seen
    report.check(header_seen == expected["tables"],
                 "openpyxl 读到 %d 张表格工作表，应为 %d 张" % (header_seen, expected["tables"]))
    compact_text = compact(text)
    for side in ("left", "right"):
        anchor = expected.get("anchors", {}).get(side)
        if not anchor:
            continue
        report.check(compact(anchor) in compact_text,
                     "工作表里找不到%s的锚点「%s」" % (side, anchor))
    return facts


# ---------------------------------------------------------------------------
# 负例对照
# ---------------------------------------------------------------------------

def mutate_docx(source: Path, target: Path, kind: str) -> None:
    """把一份好的 DOCX 弄坏：删图片部件 / 改坏关系号 / 删一张表。"""
    with zipfile.ZipFile(source) as archive:
        entries = [(info.filename, archive.read(info.filename))
                   for info in archive.infolist()]
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as out:
        for name, payload in entries:
            if kind == "drop-media" and name.startswith("word/media/"):
                continue
            if kind == "break-rel" and name == "word/_rels/document.xml.rels":
                text = payload.decode("utf-8")
                text = re.sub(r'Id="rId1"', 'Id="rId99"', text, count=1)
                payload = text.encode("utf-8")
            if kind == "drop-table" and name == "word/document.xml":
                text = payload.decode("utf-8")
                text = re.sub(r"<w:tbl>.*?</w:tbl>", "", text, count=1, flags=re.S)
                payload = text.encode("utf-8")
            out.writestr(name, payload)


def negative_controls(sample_docx: Path, sample_pptx: Path, expected: dict) -> list[str]:
    """故意弄坏产物，确认校验器当场报出问题。

    返回「负例没红」的说明列表；空列表表示负例全部变红 ✓。
    """
    failures: list[str] = []
    workdir = Path(tempfile.mkdtemp(prefix="office-negative-"))
    try:
        cases = [
            ("删掉一个图片部件", "drop-media", "docx"),
            ("把一个关系号改坏", "break-rel", "docx"),
            ("删掉一整张表", "drop-table", "docx"),
        ]
        for name, kind, _ in cases:
            broken = workdir / ("broken-%s.docx" % kind)
            mutate_docx(sample_docx, broken, kind)
            report = Report("负例：%s" % name)
            verify_docx(broken, expected, report)
            if not report.problems:
                failures.append("负例「%s」没有让校验器报错：校验太松了" % name)
            else:
                print("  负例「%s」→ 校验器报出 %d 条问题 ✓" % (name, len(report.problems)))

        # 期望值改错：把图片数改成 +1，校验器必须发现对不上。
        wrong = dict(expected)
        wrong["images"] = expected["images"] + 1
        report = Report("负例：期望值被改错")
        verify_docx(sample_docx, wrong, report)
        if not report.problems:
            failures.append("负例「期望值被改错」没有让校验器报错：校验太松了")
        else:
            print("  负例「期望值被改错」→ 校验器报出 %d 条问题 ✓" % len(report.problems))

        # PPTX：删掉一张图片。
        if sample_pptx.exists():
            broken = workdir / "broken.pptx"
            with zipfile.ZipFile(sample_pptx) as archive:
                entries = [(info.filename, archive.read(info.filename))
                           for info in archive.infolist()]
            with zipfile.ZipFile(broken, "w", zipfile.ZIP_DEFLATED) as out:
                for name, payload in entries:
                    if name.startswith("ppt/media/"):
                        continue
                    out.writestr(name, payload)
            report = Report("负例：PPTX 删掉一个图片部件")
            verify_pptx(broken, expected, report)
            if not report.problems:
                failures.append("负例「PPTX 删掉一个图片部件」没有让校验器报错：校验太松了")
            else:
                print("  负例「PPTX 删掉一个图片部件」→ 校验器报出 %d 条问题 ✓" %
                      len(report.problems))
    finally:
        shutil.rmtree(workdir, ignore_errors=True)
    return failures


# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------

def run_exporter(exporter: Path, corpus: Path, dump: Path) -> bool:
    """调用 C++ 侧的批量导出（同一个二进制换个模式跑）。"""
    command = [str(exporter), "--export-corpus", str(corpus), str(dump)]
    print("导出：%s" % " ".join(command))
    result = subprocess.run(command, capture_output=True, text=True, timeout=1800)
    if result.returncode != 0:
        print("导出失败（退出码 %d）：%s" % (result.returncode, result.stderr.strip()))
        return False
    return True


def verify_corpus(corpus: Path, dump: Path) -> tuple[list[str], list[str]]:
    """按 corpus.meta.json 里的期望值逐份校验产物。"""
    problems: list[str] = []
    notes: list[str] = []
    meta_path = corpus / "corpus.meta.json"
    if not meta_path.exists():
        return (["语料目录里没有 corpus.meta.json：%s" % meta_path], notes)
    meta = json.loads(meta_path.read_text(encoding="utf-8"))
    documents = meta.get("documents", [])
    print("语料 %d 份，期望值来自 %s" % (len(documents), meta_path))

    manifest_path = dump / "manifest.json"
    manifest = {}
    if manifest_path.exists():
        manifest = {entry.get("name"): entry
                    for entry in json.loads(manifest_path.read_text(encoding="utf-8")).get(
                        "documents", [])}
    else:
        notes.append("产物目录里没有 manifest.json：导出端的自报计数没法交叉核对")

    for document in documents:
        name = document["name"]
        stem = Path(name).stem
        expected = {
            "pages": document["pages"],
            "images": document["images"],
            "tables": document["tables"],
            "table_rows": document.get("table_rows", 3),
            "table_columns": document.get("table_columns", 3),
            "anchors": document.get("anchors", {}),
        }
        report = Report(name)

        docx_path = dump / ("%s.docx" % stem)
        pptx_path = dump / ("%s.pptx" % stem)
        xlsx_path = dump / ("%s.xlsx" % stem)

        verify_docx(docx_path, expected, report)
        verify_pptx(pptx_path, expected, report)
        verify_xlsx(xlsx_path, expected, report)

        # 与导出端自报的计数交叉核对（导出端的计数器 vs 独立解析器的读数）。
        entry = manifest.get(name)
        if entry:
            for key, field in (("docx", "images"), ("pptx", "images")):
                claimed = entry.get(key, {}).get(field)
                if claimed is not None and claimed != expected["images"]:
                    report.check(False, "导出端自报 %s 图片数 %d，与语料期望值 %d 对不上" %
                                 (key, claimed, expected["images"]))
            claimed_tables = entry.get("docx", {}).get("tables")
            if claimed_tables is not None and claimed_tables != expected["tables"]:
                report.check(False, "导出端自报 docx 表格数 %d，与语料期望值 %d 对不上" %
                             (claimed_tables, expected["tables"]))

        problems.extend(report.problems)
        notes.extend(report.notes)

    return problems, notes


def main() -> int:
    parser = argparse.ArgumentParser(description="独立校验 PDF → Office 的产物")
    parser.add_argument("--corpus", help="语料目录（含 corpus.meta.json）")
    parser.add_argument("--dump", help="产物目录（docx / pptx / xlsx 与 manifest.json）")
    parser.add_argument("--samples", help="单测落盘目录（含 images.docx / images.pptx）")
    parser.add_argument("--exporter", help="C++ 批量导出器（OfficeImagesTests.exe）")
    parser.add_argument("--generate", action="store_true",
                        help="语料不在时先生成（调 scripts/gen-office-corpus.py）")
    args = parser.parse_args()

    problems: list[str] = []
    notes: list[str] = []

    if not args.corpus and not args.samples:
        print("!! 至少要给 --corpus 或 --samples 之一")
        return 1

    corpus = Path(args.corpus) if args.corpus else None
    dump = Path(args.dump) if args.dump else (corpus / "exported" if corpus else None)

    if corpus and not (corpus / "corpus.meta.json").exists():
        if args.generate:
            print("语料不存在先生成：")
            command = [sys.executable, str(REPO_ROOT / "scripts" / "gen-office-corpus.py"),
                       "--out", str(corpus), "--count", "50", "--quiet"]
            result = subprocess.run(command, cwd=str(REPO_ROOT), text=True)
            if result.returncode != 0:
                print("!! 语料生成失败")
                return 1
        else:
            print("!! 语料目录里没有 corpus.meta.json：%s" % corpus)
            return 1

    if corpus and dump:
        if args.exporter and not (dump / "manifest.json").exists():
            if not run_exporter(Path(args.exporter), corpus, dump):
                return 1
        corpus_problems, corpus_notes = verify_corpus(corpus, dump)
        problems.extend(corpus_problems)
        notes.extend(corpus_notes)

    # 单测落盘的样例：证明产物能被第三方实现打开（不依赖语料）。
    sample_docx = None
    sample_pptx = None
    sample_report = Report("单测样例")
    if args.samples:
        samples = Path(args.samples)
        sample_docx = samples / "images.docx"
        sample_pptx = samples / "images.pptx"
        if sample_docx.exists():
            verify_docx(sample_docx, {"pages": 1, "images": 2, "tables": 1,
                                      "table_rows": 3, "table_columns": 3,
                                      "anchors": {}}, sample_report)
        else:
            sample_report.note("样例目录里没有 images.docx，跳过")
        if sample_pptx.exists():
            verify_pptx(sample_pptx, {"pages": 1, "images": 2, "tables": 1,
                                      "table_rows": 3, "table_columns": 3,
                                      "anchors": {}}, sample_report)
        else:
            sample_report.note("样例目录里没有 images.pptx，跳过")
    problems.extend(sample_report.problems)
    notes.extend(sample_report.notes)

    # 负例对照：拿样例（或者语料里的第一份）故意弄坏，确认校验器会红。
    negative_source = sample_docx
    negative_pptx = sample_pptx
    negative_expected = {"pages": 1, "images": 2, "tables": 1,
                         "table_rows": 3, "table_columns": 3, "anchors": {}}
    if (negative_source is None or not negative_source.exists()) and corpus and dump:
        meta = json.loads((corpus / "corpus.meta.json").read_text(encoding="utf-8"))
        first = meta["documents"][0]
        negative_source = dump / ("%s.docx" % Path(first["name"]).stem)
        negative_pptx = dump / ("%s.pptx" % Path(first["name"]).stem)
        negative_expected = {"pages": first["pages"], "images": first["images"],
                             "tables": first["tables"],
                             "table_rows": first.get("table_rows", 3),
                             "table_columns": first.get("table_columns", 3),
                             "anchors": first.get("anchors", {})}
    if negative_source is not None and negative_source.exists():
        print("负例对照：")
        problems.extend(negative_controls(negative_source, negative_pptx,
                                          negative_expected))
    else:
        print("  （没有可用来做负例对照的产物，跳过负例对照）")

    for note in dict.fromkeys(notes):
        print("  说明：%s" % note)

    print()
    if problems:
        print("!! 独立校验未通过：")
        for item in problems:
            print("   - %s" % item)
        return 1
    print("独立校验通过：产物能被第三方实现读开，页数/图片数/表格数与语料期望值一致")
    return 0


if __name__ == "__main__":
    sys.exit(main())
