#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""独立校验生成的 Office 包（.pptx / .xlsx）。

为什么要有这道检查：包是本仓库自己写的（自己的 zip 写入器 + 自己拼的 XML），
**自己写、自己读不算验证** —— 写错了自己也读不出来。
所以这里用**另一套解析器**重新解一遍：

* zipfile（Python 标准库）：解包 + 校验部件齐全 + 每个 XML 都能解析；
* python-pptx / openpyxl（装了才用）：让真正的 Office 实现打开一次。

没有装 python-pptx / openpyxl 时不算失败：标准库那几项已经能抓住
「zip 结构不对」「XML 语法错」「部件缺失」这三类问题。

用法：
    python scripts/verify-office-packages.py --dir <样例目录>
"""
from __future__ import annotations

import argparse
import io
import os
import sys
import xml.etree.ElementTree as ET
import zipfile

problems = []


def check(condition, message):
    if not condition:
        problems.append(message)
    return condition


def xml_texts(data: bytes):
    """取一份 XML 里所有元素的文本（不区分命名空间前缀）。"""
    root = ET.fromstring(data)
    out = []
    for element in root.iter():
        tag = element.tag.rsplit("}", 1)[-1]
        if tag in ("t", "tab") and element.text:
            out.append(element.text)
    return out


def verify_pptx(path):
    print("校验 PPTX：%s" % path)
    with zipfile.ZipFile(path) as archive:
        names = set(archive.namelist())
        required = [
            "[Content_Types].xml",
            "_rels/.rels",
            "ppt/presentation.xml",
            "ppt/_rels/presentation.xml.rels",
            "ppt/theme/theme1.xml",
            "ppt/slideMasters/slideMaster1.xml",
            "ppt/slideLayouts/slideLayout1.xml",
            "ppt/notesMasters/notesMaster1.xml",
            "ppt/slides/slide1.xml",
            "ppt/slides/slide2.xml",
            "ppt/notesSlides/notesSlide1.xml",
        ]
        for name in required:
            check(name in names, "PPTX 缺部件：%s" % name)

        # 每个 XML 部件都要能被**别的解析器**读一遍。
        for name in sorted(names):
            if not (name.endswith(".xml") or name.endswith(".rels")):
                continue
            try:
                ET.fromstring(archive.read(name))
            except ET.ParseError as error:
                problems.append("PPTX 的 %s 不是合法 XML：%s" % (name, error))

        slide1 = xml_texts(archive.read("ppt/slides/slide1.xml"))
        for expected in ("第一季度", "营收 120 万", "成本 80 万", "毛利率 33%", "营收与成本"):
            check(expected in slide1, "slide1 里没有「%s」" % expected)
        slide2 = xml_texts(archive.read("ppt/slides/slide2.xml"))
        check("第二季度" in slide2, "slide2 里没有「第二季度」")
        check("营收 120 万" not in slide2, "slide2 混进了第一页的要点")
        notes = xml_texts(archive.read("ppt/notesSlides/notesSlide1.xml"))
        check(any("第四季度口径有调整" in text for text in notes),
              "备注页里没有演讲者备注")

    # 真 Office 实现能打开吗（装了才查）。
    try:
        from pptx import Presentation  # type: ignore
    except Exception:
        print("  （未装 python-pptx，跳过「真实现打开」这一项）")
        return
    deck = Presentation(path)
    check(len(deck.slides) == 2, "python-pptx 读到 %d 页，应为 2 页" % len(deck.slides))
    first = [shape.text_frame.text for shape in deck.slides[0].shapes
             if shape.has_text_frame]
    joined = "\n".join(first)
    for expected in ("第一季度", "营收 120 万", "成本 80 万"):
        check(expected in joined, "python-pptx 读到的第一页里没有「%s」" % expected)
    print("  python-pptx 打开成功：%d 页" % len(deck.slides))


def verify_xlsx(path):
    print("校验 XLSX：%s" % path)
    with zipfile.ZipFile(path) as archive:
        names = set(archive.namelist())
        required = [
            "[Content_Types].xml",
            "_rels/.rels",
            "xl/workbook.xml",
            "xl/_rels/workbook.xml.rels",
            "xl/styles.xml",
            "xl/worksheets/sheet1.xml",
        ]
        for name in required:
            check(name in names, "XLSX 缺部件：%s" % name)
        for name in sorted(names):
            if not (name.endswith(".xml") or name.endswith(".rels")):
                continue
            try:
                ET.fromstring(archive.read(name))
            except ET.ParseError as error:
                problems.append("XLSX 的 %s 不是合法 XML：%s" % (name, error))

        sheet = xml_texts(archive.read("xl/worksheets/sheet1.xml"))
        for expected in ("name", "value", "姓名", "张三", "备注"):
            check(expected in sheet, "工作表里没有「%s」" % expected)
        check("示例 & 有限公司 <测试>" in sheet, "工作表里的特殊字符没被正确转义")

    try:
        import openpyxl  # type: ignore
    except Exception:
        print("  （未装 openpyxl，跳过「真实现打开」这一项）")
        return
    book = openpyxl.load_workbook(path)
    sheet = book.active
    check(sheet.max_row == 4, "openpyxl 读到 %d 行，应为 4 行" % sheet.max_row)
    check(sheet["A2"].value == "姓名", "openpyxl 读到的 A2 不是「姓名」")
    check(sheet["B2"].value == "张三", "openpyxl 读到的 B2 不是「张三」")
    check(sheet["B3"].value == "示例 & 有限公司 <测试>",
          "openpyxl 读到的 B3 与写入值不一致")
    print("  openpyxl 打开成功：工作表「%s」，%d 行" % (sheet.title, sheet.max_row))


def main() -> int:
    parser = argparse.ArgumentParser(description="独立校验生成的 Office 包")
    parser.add_argument("--dir", required=True, help="样例所在目录")
    args = parser.parse_args()

    pptx = os.path.join(args.dir, "sample.pptx")
    xlsx = os.path.join(args.dir, "sample.xlsx")
    for path in (pptx, xlsx):
        if not os.path.exists(path):
            print("!! 找不到样例：%s" % path)
            return 1

    verify_pptx(pptx)
    verify_xlsx(xlsx)

    print()
    if problems:
        print("!! 独立校验未通过：")
        for item in problems:
            print("   - " + item)
        return 1
    print("独立校验通过：两个包都能被别的解析器读开")
    return 0


if __name__ == "__main__":
    sys.exit(main())
