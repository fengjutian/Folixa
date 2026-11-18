#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""独立校验 PDF → Excel / PowerPoint / Word 的产物。

为什么要有这道检查
------------------
包是本仓库自己写的（自己的 zip 写入器 + 自己拼的 XML），
**自己写、自己读不算验证** —— 写错了自己也读不出来。
所以这里用**另一套实现**重新读一遍：

* openpyxl：第三方 Excel 实现，读工作表与单元格的值；
* python-pptx：第三方 PowerPoint 实现，读幻灯片数与文字；
* LibreOffice（headless）：把 docx 转成 txt —— 转得出来就说明它认这个包。

没有装 openpyxl / python-pptx 时不算失败：Python 标准库 zipfile +
ElementTree 那几项已经能抓住「zip 结构不对」「XML 语法错」「部件缺失」。

用法：
    python scripts/verify-pdf-office-packages.py --dir <样例目录>
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

SOFFICE_CANDIDATES = [
    Path(r"C:\Program Files\LibreOffice\program\soffice.exe"),
    Path(r"C:\Program Files (x86)\LibreOffice\program\soffice.exe"),
]

problems: list[str] = []


def check(condition: bool, message: str) -> bool:
    if not condition:
        problems.append(message)
    return condition


def xml_texts(data: bytes) -> list[str]:
    """取一份 XML 里所有元素的文本（不区分命名空间前缀）。"""
    root = ET.fromstring(data)
    out = []
    for element in root.iter():
        tag = element.tag.rsplit("}", 1)[-1]
        if tag in ("t", "tab") and element.text:
            out.append(element.text)
    return out


def check_xml_parts(archive: zipfile.ZipFile, label: str) -> None:
    """每个 XML 部件都要能被**别的解析器**读一遍。"""
    for name in sorted(archive.namelist()):
        if not (name.endswith(".xml") or name.endswith(".rels")):
            continue
        try:
            ET.fromstring(archive.read(name))
        except ET.ParseError as error:
            problems.append("%s 的 %s 不是合法 XML：%s" % (label, name, error))


def verify_xlsx(path: Path) -> None:
    print("校验 XLSX：%s" % path)
    with zipfile.ZipFile(path) as archive:
        names = set(archive.namelist())
        for name in ("[Content_Types].xml", "_rels/.rels", "xl/workbook.xml",
                     "xl/_rels/workbook.xml.rels", "xl/styles.xml",
                     "xl/worksheets/sheet1.xml", "xl/worksheets/sheet2.xml"):
            check(name in names, "XLSX 缺部件：%s" % name)
        check_xml_parts(archive, "XLSX")
        sheet = xml_texts(archive.read("xl/worksheets/sheet1.xml"))
        for expected in ("Item", "Qty", "Price", "Apple", "Pear", "5.25"):
            check(expected in sheet, "表格工作表里没有「%s」" % expected)
        text_sheet = xml_texts(archive.read("xl/worksheets/sheet2.xml"))
        check("Quarterly report" in text_sheet,
              "文字工作表里没有表格外的标题")

    try:
        import openpyxl  # type: ignore
    except Exception:
        print("  （未装 openpyxl，跳过「真实现打开」这一项）")
        return
    book = openpyxl.load_workbook(path)
    names = book.sheetnames
    check(len(names) >= 2, "openpyxl 读到 %d 张表，应至少 2 张" % len(names))
    rows = [[cell.value for cell in row] for row in book[names[0]].iter_rows()]
    check(rows and rows[0][:3] == ["Item", "Qty", "Price"],
          "openpyxl 读到的表头不对：%s" % (rows[0] if rows else None))
    check(len(rows) >= 3 and rows[1][:3] == ["Apple", 12, 3.5],
          "openpyxl 读到的第二行不对：%s" % (rows[1] if len(rows) > 1 else None))
    check(len(rows) >= 3 and rows[2][:3] == ["Pear", 7, 5.25],
          "openpyxl 读到的第三行不对：%s" % (rows[2] if len(rows) > 2 else None))
    print("  openpyxl 打开成功：工作表 %s" % names)


def verify_pptx(path: Path) -> None:
    print("校验 PPTX：%s" % path)
    with zipfile.ZipFile(path) as archive:
        names = set(archive.namelist())
        for name in ("[Content_Types].xml", "_rels/.rels", "ppt/presentation.xml",
                     "ppt/_rels/presentation.xml.rels", "ppt/theme/theme1.xml",
                     "ppt/slideMasters/slideMaster1.xml", "ppt/slideLayouts/slideLayout1.xml",
                     "ppt/slides/slide1.xml", "ppt/slides/slide12.xml"):
            check(name in names, "PPTX 缺部件：%s" % name)
        check_xml_parts(archive, "PPTX")
        slide1 = xml_texts(archive.read("ppt/slides/slide1.xml"))
        check("Page 1" in slide1, "第 1 张幻灯片里没有正文")
        check("第 1 页" in slide1, "第 1 张幻灯片的标题不对")

    try:
        from pptx import Presentation  # type: ignore
    except Exception:
        print("  （未装 python-pptx，跳过「真实现打开」这一项）")
        return
    deck = Presentation(str(path))
    check(len(deck.slides) == 12, "python-pptx 读到 %d 页，应为 12 页" % len(deck.slides))
    texts = [shape.text_frame.text for shape in deck.slides[0].shapes if shape.has_text_frame]
    joined = "\n".join(texts)
    check("Page 1" in joined, "python-pptx 读到的第一页里没有「Page 1」")
    print("  python-pptx 打开成功：%d 页" % len(deck.slides))


def verify_docx(path: Path) -> None:
    print("校验 DOCX：%s" % path)
    with zipfile.ZipFile(path) as archive:
        names = set(archive.namelist())
        for name in ("[Content_Types].xml", "_rels/.rels", "word/document.xml"):
            check(name in names, "DOCX 缺部件：%s" % name)
        check_xml_parts(archive, "DOCX")
        document = xml_texts(archive.read("word/document.xml"))
        check("Folixa" in document, "DOCX 里没有第一页的文字")
        check("Search hits" in document, "DOCX 里没有第二页的文字（整篇导出没生效）")
        raw = archive.read("word/document.xml").decode("utf-8", "replace")
        check('w:br w:type="page"' in raw, "DOCX 里没有分页符（页与页没隔开）")

    soffice = next((path for path in SOFFICE_CANDIDATES if path.exists()), None)
    if soffice is None:
        print("  （本机没装 LibreOffice，跳过「真实现打开」这一项）")
        return
    out_dir = path.parent / "lo-convert"
    out_dir.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [str(soffice), "--headless", "--convert-to", "txt:Text", "--outdir", str(out_dir), str(path)],
        capture_output=True,
        text=True,
        timeout=300,
    )
    converted = out_dir / (path.stem + ".txt")
    if not converted.exists():
        problems.append("LibreOffice 转不出 txt：它打不开这个 docx")
        return
    content = converted.read_text(encoding="utf-8", errors="replace")
    check("Folixa" in content, "LibreOffice 转出来的正文里没有第一页的文字")
    check("Search hits" in content, "LibreOffice 转出来的正文里没有第二页的文字")
    print("  LibreOffice 打开成功：转出 %d 个字符" % len(content))


def main() -> int:
    parser = argparse.ArgumentParser(description="独立校验 PDF → Office 的产物")
    parser.add_argument("--dir", required=True, help="样例所在目录")
    args = parser.parse_args()

    directory = Path(args.dir)
    xlsx = directory / "table.xlsx"
    pptx = directory / "multipage.pptx"
    docx = directory / "text.docx"
    for path in (xlsx, pptx, docx):
        if not path.exists():
            print("!! 找不到样例：%s" % path)
            return 1

    verify_xlsx(xlsx)
    verify_pptx(pptx)
    verify_docx(docx)

    shutil.rmtree(directory / "lo-convert", ignore_errors=True)
    print()
    if problems:
        print("!! 独立校验未通过：")
        for item in problems:
            print("   - %s" % item)
        return 1
    print("独立校验通过：三个包都能被别的实现读开")
    return 0


if __name__ == "__main__":
    sys.exit(main())
