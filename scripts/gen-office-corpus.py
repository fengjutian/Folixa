#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成「双栏 + 表格 + 图片 + CJK」四合一的 PDF 语料（H5 图片导出验收用）。

为什么要有这份语料
------------------
仓库里现成的语料都只覆盖一件事：generate-test-pdfs.py 有表格页与文字页，
gen-compat-corpus.py 有裸 DeviceRGB 的图片，gen-cjk-corpus.py 有 CJK 排版。
**没有一份语料同时有这四样**——而「PDF → Word / PowerPoint」这条导出链
真正难的地方恰恰是四样东西凑在一起时：双栏会让行归并串行、表格会被误判、
图片要单独走资源通道、CJK 要能抽得出来。

所以这里造一份四合一的语料，并且**把期望值写进 .meta.json**：
每一份语料该导出几张图片、几个表格、几页，都是生成时就定死的，
验收脚本拿它当标准答案去对。

四合一怎么排一页
----------------
    ┌──────────────────────────── 页面 ────────────────────────────┐
    │  标题（通栏，跨两栏）                                          │
    ├──────────────────────┬────────────────────────────────────────┤
    │ 左栏正文              │ 右栏正文                                │
    │ （每行足够长，         │（每行足够长，                           │
    │   以免被当成表格单元） │  以免被当成表格单元）                   │
    ├──────────────────────┴────────────────────────────────────────┤
    │  表格（通栏，3 列 × N 行，列位置固定）                          │
    ├───────────────────────────────────────────────────────────────┤
    │  图片（1~2 张）                                                │
    └───────────────────────────────────────────────────────────────┘

参数化维度（每一份语料都不一样）：栏宽、栏距、行数、表格行列与落位、
图片数量与落位、CJK 占比、页数。

CJK 怎么办
----------
中文用 Type0 / Identity-H + CIDFontType2 内嵌字体（写法复用
scripts/gen-cjk-corpus.py），并带 ToUnicode，抽取时能还原回原文。

内嵌的字体是**子集**：只保留语料里真正用到的那些字形，
否则一份 9 MB 的字体 × 50 份 = 450 MB，语料自己就成负担了。
子集的实现在 _subset_ttf() 里。

用法：
    python scripts/gen-office-corpus.py --out build/office-corpus --count 50
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys
import zlib

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))


def _load_module(name, path):
    """按路径加载一个脚本当模块用（这些脚本的文件名带连字符，不能直接 import）。"""
    import importlib.util

    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


gtp = _load_module("generate_test_pdfs", os.path.join(SCRIPT_DIR, "generate-test-pdfs.py"))
cjk = _load_module("gen_cjk_corpus", os.path.join(SCRIPT_DIR, "gen-cjk-corpus.py"))

DEFAULT_OUT = os.path.join(REPO_ROOT, "build", "office-corpus")


# ---------------------------------------------------------------------------
# TrueType 子集：只保留用到的字形
# ---------------------------------------------------------------------------

def _read_tables(data):
    """读 sfnt 的表目录。返回 (tag -> (offset, length))。"""
    if data[:4] == b"ttcf":
        # 字体集合：取第一个字体。
        offset = struct.unpack(">I", data[12:16])[0]
        data = data[offset:]
    num_tables = struct.unpack(">H", data[4:6])[0]
    tables = {}
    for index in range(num_tables):
        base = 12 + index * 16
        tag = data[base:base + 4].decode("latin-1")
        offset, length = struct.unpack(">II", data[base + 8:base + 16])
        tables[tag] = (offset, length)
    return tables


def _table_data(data, tables, tag):
    entry = tables.get(tag)
    if entry is None:
        return b""
    offset, length = entry
    return data[offset:offset + length]


def _cmap_unicode_to_gid(data, tables):
    """从 cmap 里找 Unicode -> GID 的映射（优先 3/1 的 format 4）。"""
    cmap = _table_data(data, tables, "cmap")
    if len(cmap) < 4:
        return {}
    count = struct.unpack(">H", cmap[2:4])[0]
    best = None
    for index in range(count):
        base = 4 + index * 8
        platform, encoding, offset = struct.unpack(">HHI", cmap[base:base + 8])
        score = 0
        if platform == 3 and encoding == 10:
            score = 3
        elif platform == 3 and encoding == 1:
            score = 2
        elif platform == 0:
            score = 1
        if score and (best is None or score > best[0]):
            best = (score, offset)
    if best is None:
        return {}
    subtable = cmap[best[1]:]
    fmt = struct.unpack(">H", subtable[0:2])[0]
    if fmt != 4:
        return {}
    seg_count = struct.unpack(">H", subtable[6:8])[0] // 2
    ends = struct.unpack(">%dH" % seg_count, subtable[14:14 + seg_count * 2])
    starts_at = 14 + seg_count * 2 + 2
    starts = struct.unpack(">%dH" % seg_count, subtable[starts_at:starts_at + seg_count * 2])
    deltas_at = starts_at + seg_count * 2
    deltas = struct.unpack(">%dh" % seg_count, subtable[deltas_at:deltas_at + seg_count * 2])
    ranges_at = deltas_at + seg_count * 2
    ranges = struct.unpack(">%dH" % seg_count, subtable[ranges_at:ranges_at + seg_count * 2])
    mapping = {}
    for index in range(seg_count):
        start, end = starts[index], ends[index]
        if start == 0xFFFF:
            continue
        for code in range(start, end + 1):
            if ranges[index] == 0:
                gid = (code + deltas[index]) & 0xFFFF
            else:
                offset_at = ranges_at + index * 2 + ranges[index] + (code - start) * 2
                if offset_at + 2 > len(subtable):
                    continue
                gid = struct.unpack(">H", subtable[offset_at:offset_at + 2])[0]
                if gid:
                    gid = (gid + deltas[index]) & 0xFFFF
            if gid:
                mapping[code] = gid
    return mapping


def _glyph_offsets(data, tables, num_glyphs):
    """loca 表 -> 每个字形的 (起, 止) 偏移。"""
    loca = _table_data(data, tables, "loca")
    head = _table_data(data, tables, "head")
    long_format = struct.unpack(">h", head[50:52])[0] != 0
    offsets = []
    if long_format:
        for index in range(num_glyphs + 1):
            offsets.append(struct.unpack(">I", loca[index * 4:index * 4 + 4])[0])
    else:
        for index in range(num_glyphs + 1):
            offsets.append(struct.unpack(">H", loca[index * 2:index * 2 + 2])[0] * 2)
    return offsets


def _composite_components(glyph_data):
    """复合字形引用的其它字形 GID。"""
    if len(glyph_data) < 10:
        return []
    number_of_contours = struct.unpack(">h", glyph_data[0:2])[0]
    if number_of_contours >= 0:
        return []
    components = []
    offset = 10
    while offset + 4 <= len(glyph_data):
        flags, glyph_index = struct.unpack(">HH", glyph_data[offset:offset + 4])
        components.append(glyph_index)
        offset += 4
        offset += 4  # ARG_1_AND_2_ARE_WORDS
        if flags & 0x0008:  # WE_HAVE_A_SCALE
            offset += 2
        elif flags & 0x0040:  # X_AND_Y_SCALE
            offset += 4
        elif flags & 0x0080:  # TWO_BY_TWO
            offset += 8
        if not flags & 0x0020:  # MORE_COMPONENTS
            break
    return components


def _build_cmap4(mapping):
    """按 format 4 造一张 cmap：每个码位一个段（段多但只有几十个）。"""
    codes = sorted(mapping)
    segments = []
    for code in codes:
        gid = mapping[code]
        segments.append((code, code, (gid - code) & 0xFFFF))
    segments.append((0xFFFF, 0xFFFF, 1))
    seg_count = len(segments)
    search_range = 2
    entry_selector = 0
    while search_range * 2 <= seg_count * 2:
        search_range *= 2
        entry_selector += 1
    range_shift = seg_count * 2 - search_range

    subtable = struct.pack(">HHHHHHH", 4, 0, 0, seg_count * 2, search_range, entry_selector,
                           range_shift)
    subtable += struct.pack(">%dH" % seg_count, *[segment[1] for segment in segments])
    subtable += struct.pack(">H", 0)
    subtable += struct.pack(">%dH" % seg_count, *[segment[0] for segment in segments])
    subtable += struct.pack(">%dh" % seg_count, *[segment[2] - 0x10000 if segment[2] > 0x7FFF
                                                   else segment[2] for segment in segments])
    subtable += struct.pack(">%dH" % seg_count, *([0] * seg_count))

    header = struct.pack(">HH", 0, 1)
    header += struct.pack(">HHI", 3, 1, 12)
    return header + subtable


def _checksum(data):
    padded = data + b"\0" * ((4 - len(data) % 4) % 4)
    total = 0
    for index in range(0, len(padded), 4):
        total = (total + struct.unpack(">I", padded[index:index + 4])[0]) & 0xFFFFFFFF
    return total


def _subset_ttf(data, unicodes):
    """从 TrueType 里挑出用到的字形，重建一个只含这些字形的字体。

    **保留原来的 GID**（不重新编号）：这样 hmtx、复合字形的组件引用
    全都不用改，只换 glyf / loca / cmap 三张表。
    """
    tables = _read_tables(data)
    mapping = _cmap_unicode_to_gid(data, tables)
    if not mapping:
        raise RuntimeError("字体里解析不出 cmap")

    maxp = _table_data(data, tables, "maxp")
    num_glyphs = struct.unpack(">H", maxp[4:6])[0]
    offsets = _glyph_offsets(data, tables, num_glyphs)
    glyf = _table_data(data, tables, "glyf")

    wanted = set()
    for char in unicodes:
        gid = mapping.get(ord(char))
        if gid is None:
            raise RuntimeError("字体里没有字符 U+%04X" % ord(char))
        wanted.add(gid)
    # 复合字形的组件也要留下，否则渲染出来是空的。
    pending = list(wanted)
    while pending:
        gid = pending.pop()
        begin, end = offsets[gid], offsets[gid + 1]
        if end <= begin:
            continue
        for component in _composite_components(glyf[begin:end]):
            if component not in wanted:
                wanted.add(component)
                pending.append(component)

    # 新 glyf：按 GID 顺序排（不用的 GID 与下一个用的 GID 共用偏移）。
    new_glyf = bytearray()
    new_loca = []
    for gid in range(num_glyphs + 1):
        if gid < num_glyphs and gid in wanted:
            begin, end = offsets[gid], offsets[gid + 1]
            new_loca.append(len(new_glyf))
            new_glyf += glyf[begin:end]
            while len(new_glyf) % 4:
                new_glyf += b"\0"
        else:
            new_loca.append(len(new_glyf))
    new_loca = new_loca[:num_glyphs + 1]

    subset_mapping = {}
    for char in unicodes:
        subset_mapping[ord(char)] = mapping[ord(char)]

    pieces = {
        "cmap": _build_cmap4(subset_mapping),
        "glyf": bytes(new_glyf),
        "loca": struct.pack(">%dI" % len(new_loca), *new_loca),
    }
    # head：位置格式改成 long，并清掉 checkSumAdjustment（最后统一算）。
    head = bytearray(_table_data(data, tables, "head"))
    head[50:52] = struct.pack(">h", 1)
    head[8:12] = b"\0\0\0\0"
    pieces["head"] = bytes(head)

    keep = [tag for tag in tables if tag not in ("cmap", "glyf", "loca", "head")]
    for tag in keep:
        pieces[tag] = _table_data(data, tables, tag)

    tags = sorted(pieces)
    num_tables = len(tags)
    search_range = 16
    entry_selector = 0
    while search_range * 2 <= num_tables * 16:
        search_range *= 2
        entry_selector += 1
    range_shift = num_tables * 16 - search_range

    header = struct.pack(">IHHHH", 0x00010000, num_tables, search_range, entry_selector,
                         range_shift)
    directory = b""
    body = b""
    offset = len(header) + num_tables * 16
    body_offset = 0
    for tag in tags:
        payload = pieces[tag]
        padded = payload + b"\0" * ((4 - len(payload) % 4) % 4)
        directory += tag.encode("latin-1").ljust(4, b" ") + struct.pack(">III", _checksum(payload),
                                                                          offset + body_offset,
                                                                          len(payload))
        body += padded
        body_offset += len(padded)

    font = bytearray(header + directory + body)
    # head.checkSumAdjustment = 0xB1B0AFBA - 整个文件的校验和。
    adjustment = (0xB1B0AFBA - _checksum(bytes(font))) & 0xFFFFFFFF
    head_offset = None
    for index, tag in enumerate(tags):
        if tag == "head":
            head_offset = struct.unpack(">I", font[12 + index * 16 + 8:12 + index * 16 + 12])[0]
            break
    if head_offset is not None:
        font[head_offset + 8:head_offset + 12] = struct.pack(">I", adjustment)
    return bytes(font)


# ---------------------------------------------------------------------------
# 页面零件
# ---------------------------------------------------------------------------

# 表格里的内容（短词：单元必须短，否则会被当成正文而不是表格单元）。
TABLE_HEADERS = ["Item", "Qty", "Price"]
TABLE_ROWS = [
    ["Apple", "12", "3.50"],
    ["Pear", "7", "5.25"],
    ["Plum", "31", "1.75"],
    ["Fig", "9", "8.00"],
]

# 两栏的正文（每行都要足够长：以免被当成表格单元）。
#
# 行**不能太长**：左栏的行尾必须停在右栏起点之前，否则两栏之间那条
# 竖直空白带就没了，双栏识别会整个失效（整页被当成一栏）。
# 宽度由 measure_text() 精确算（Helvetica 的 AFM 宽度表），生成时断言。
LEFT_LINES = [
    "Left column prose line one.",
    "Left column prose line two.",
    "Left column prose line three.",
    "Left column prose line four.",
    "Left column prose line five.",
    "Left column prose line six.",
    "Left column prose line seven.",
    "Left column prose line eight.",
]

RIGHT_LINES = [
    "Right column prose line one.",
    "Right column prose line two.",
    "Right column prose line three.",
    "Right column prose line four.",
    "Right column prose line five.",
    "Right column prose line six.",
    "Right column prose line seven.",
    "Right column prose line eight.",
]

# 中文的正文（同样要长：汉字一个字就是一个字宽）。
CJK_LEFT_LINES = [
    "左栏第一行正文，用来验证双栏。",
    "左栏第二行正文，逐行保留版式。",
    "左栏第三行正文，说明栏宽参数。",
    "左栏第四行正文，提醒表格另认。",
    "左栏第五行正文，足够高才认得出。",
    "左栏第六行正文，右栏与之并列。",
]

CJK_RIGHT_LINES = [
    "右栏第一行正文，与左栏并列。",
    "右栏第二行正文，导出后对应。",
    "右栏第三行正文，混排字宽算对。",
    "右栏第四行正文，图片另走通道。",
    "右栏第五行正文，双栏到此讲完。",
    "右栏第六行正文，下面是表与图。",
]

MIXED_LEFT_LINES = [
    "左栏第一行混排 English 与中文。",
    "左栏第二行混排 English 与中文。",
    "左栏第三行混排 English 与中文。",
    "左栏第四行混排 English 与中文。",
    "左栏第五行混排 English 与中文。",
    "左栏第六行混排 English 与中文。",
]


# ---------------------------------------------------------------------------
# 行宽测量
# ---------------------------------------------------------------------------

# Helvetica（标准 14 号字体）的 AFM 宽度表，单位 1/1000 em。
#
# 为什么要有它：两栏的版式是「算出来」的，不是「看着差不多」。
# 左栏的行尾必须落在右栏起点之前，还要留出一条够宽的空白带
# （导出端的空白带阈值是「一个字高」），这两件事都得用真实步进宽度算，
# 靠眼睛估会差几个点，差几个点就足以让双栏识别失效。
HELVETICA_WIDTHS = {
    " ": 278, "!": 278, '"': 355, "#": 556, "$": 556, "%": 889, "&": 667,
    "'": 191, "(": 333, ")": 333, "*": 389, "+": 584, ",": 278, "-": 333,
    ".": 278, "/": 278, ":": 278, ";": 278, "<": 584, "=": 584, ">": 584,
    "?": 556, "@": 1015, "[": 278, "\\": 278, "]": 278, "^": 469, "_": 556,
    "`": 333, "{": 334, "|": 260, "}": 334, "~": 584,
    "A": 667, "B": 667, "C": 722, "D": 722, "E": 667, "F": 611, "G": 778,
    "H": 722, "I": 278, "J": 500, "K": 667, "L": 556, "M": 833, "N": 722,
    "O": 778, "P": 667, "Q": 778, "R": 722, "S": 667, "T": 611, "U": 722,
    "V": 667, "W": 944, "X": 667, "Y": 667, "Z": 611,
    "a": 556, "b": 556, "c": 500, "d": 556, "e": 556, "f": 278, "g": 556,
    "h": 556, "i": 222, "j": 222, "k": 500, "l": 222, "m": 833, "n": 556,
    "o": 556, "p": 556, "q": 556, "r": 333, "s": 500, "t": 278, "u": 556,
    "v": 500, "w": 722, "x": 500, "y": 500, "z": 500,
}
for _digit in "0123456789":
    HELVETICA_WIDTHS[_digit] = 556


def measure_text(text, size, font=None):
    """量一行文字的宽度（点）。

    ASCII 用 Helvetica 的 AFM 宽度；中文用内嵌字体的真实步进宽度
    （SimHei 的 unitsPerEm 是 256，一个汉字正好 256 单位 = 1 em）。
    """
    total = 0.0
    for char in text:
        gid = font.cmap.get(ord(char)) if font is not None else None
        if gid is not None and gid < len(font.widths) and font.widths[gid]:
            total += font.widths[gid] / float(font.units_per_em) * size
            continue
        total += HELVETICA_WIDTHS.get(char, 556) / 1000.0 * size
    return total


def latin_text(s):
    """PDF 字面串里的转义。"""
    return s.replace("\\", r"\\").replace("(", r"\(").replace(")", r"\)")


class Page:
    """一页：内容流 + 资源。"""

    def __init__(self, width=595.0, height=842.0):
        self.width = width
        self.height = height
        self.parts = []
        self.fonts = []
        self.xobjects = {}

    def use_font(self, font):
        if font not in self.fonts:
            self.fonts.append(font)
        return font

    def latin(self, text, size, x, y):
        """西文一行（标准 14 字体 Helvetica）。"""
        self.parts.append("BT /F1 %d Tf %d %d Td (%s) Tj ET" %
                          (size, round(x), round(y), latin_text(text)))

    def cjk(self, font, text, size, x, y):
        """中文一行（内嵌 Type0 字体）。"""
        self.use_font(font)
        codes = font.encode(text)
        self.parts.append("BT /%s %d Tf %d %d Td <%s> Tj ET" %
                          (font.resource, size, round(x), round(y), codes))

    def image(self, name, width, height, x, y):
        self.parts.append("q %d 0 0 %d %d %d cm /%s Do Q" %
                          (round(width), round(height), round(x), round(y), name))

    def content(self):
        return "\n".join(self.parts).encode("ascii")


class CorpusBuilder:
    """把页面、图片与字体组装成 PDF。"""

    def __init__(self):
        self.builder = cjk.DocumentBuilder()
        self.document = self.builder.document
        self.pages = []
        self.images = []
        # 标准 14 号字体（不内嵌，PDFium 自带字形）。
        self.helvetica_id = self.document.add(
            b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")

    def add_image(self, pixels, width, height):
        """加一张裸 DeviceRGB 的 XObject（写法同 gen-compat-corpus.py）。"""
        object_id = self.document.add(
            b"<< /Type /XObject /Subtype /Image /Width %d /Height %d "
            b"/ColorSpace /DeviceRGB /BitsPerComponent 8 /Length %d >>\nstream\n%s\nendstream" %
            (width, height, len(pixels), pixels))
        self.images.append(object_id)
        return object_id

    def add_page(self, page):
        self.pages.append(page)
        return page

    def build(self):
        page_ids = []
        for page in self.pages:
            content_id = self.document.add(
                b"<< /Length %d >>\nstream\n%s\nendstream" %
                (len(page.content()), page.content()))
            resources = []
            fonts = ["/F1 %d 0 R" % self.helvetica_id]
            for font in page.fonts:
                fonts.append("/%s %d 0 R" % (font.resource, self.builder.font_object(font)))
            xobjects = []
            for name, object_id in page.xobjects.items():
                xobjects.append("/%s %d 0 R" % (name, object_id))
            resources.append("/Font << %s >>" % " ".join(fonts))
            if xobjects:
                resources.append("/XObject << %s >>" % " ".join(xobjects))
            page_id = self.document.add(
                ("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] "
                 "/Resources << %s >> /Contents %d 0 R >>" %
                 (round(page.width), round(page.height), " ".join(resources),
                  content_id)).encode("ascii"))
            page_ids.append(page_id)

        self.document.objects[2] = (
            "<< /Type /Pages /Kids [%s] /Count %d >>" %
            (" ".join("%d 0 R" % pid for pid in page_ids), len(page_ids))).encode("ascii")
        self.document.objects[1] = b"<< /Type /Catalog /Pages 2 0 R >>"
        return self.document.build(1)


def make_pixels(width, height, seed):
    """一张确定性的测试图（渐变 + 棋盘），像素数据随 seed 变。"""
    pixels = bytearray()
    for y in range(height):
        for x in range(width):
            checker = ((x // 4) + (y // 4) + seed) % 2
            red = (x * 255) // max(1, width - 1)
            green = (y * 255) // max(1, height - 1)
            blue = 255 if checker else 40
            pixels += bytes((red, green, blue))
    return bytes(pixels)


def build_document(index, params, font):
    """造一份语料，返回 (pdf 字节, 期望值)。"""
    builder = CorpusBuilder()

    page_count = params["pages"]
    image_count = 0
    table_count = 0

    # 版式自检：左栏的行尾必须停在右栏起点之前，还要留出一条够宽的空白带。
    #
    # 为什么要在这里断言：语料要是排错了（比如左栏的文字压到右栏头上），
    # 双栏识别就整个失效——那时候失败的是「导出」，但根子在「语料」，
    # 排查起来最费时间。所以生成时就当场拒绝。
    left_x = 50.0
    right_x = left_x + params["column_width"] + params["gutter"]
    min_gap = 14.0
    for line in LEFT_LINES + RIGHT_LINES + CJK_LEFT_LINES + CJK_RIGHT_LINES + MIXED_LEFT_LINES:
        width = measure_text(line, 11.0, font)
        if left_x + width > right_x - min_gap:
            raise RuntimeError(
                "左栏正文太宽，会压到右栏上（行宽 %.1f 点，右栏起点 %.1f 点）：%s"
                % (width, right_x, line))
    for line in RIGHT_LINES + CJK_RIGHT_LINES:
        width = measure_text(line, 10.0, font)
        if right_x + width > 595.0 - 12.0:
            raise RuntimeError("右栏正文超出页面右边（行宽 %.1f 点）：%s" % (width, line))

    for page_index in range(page_count):
        page = builder.add_page(Page())
        top = page.height - 60.0

        # 标题（通栏）。
        page.latin("Office export corpus document %03d page %d" % (index, page_index + 1),
                   17, 50, top)

        # 双栏正文。
        line_gap = params["line_gap"]
        column_top = top - 40.0
        for line_index in range(params["column_lines"]):
            y = column_top - line_index * line_gap
            ratio = params["cjk_ratio"]
            if ratio >= 0.99:
                left_text = CJK_LEFT_LINES[line_index % len(CJK_LEFT_LINES)]
                right_text = CJK_RIGHT_LINES[line_index % len(CJK_RIGHT_LINES)]
                page.cjk(font, left_text, 11, left_x, y)
                page.cjk(font, right_text, 11, right_x, y)
            elif ratio > 0.0:
                left_text = MIXED_LEFT_LINES[line_index % len(MIXED_LEFT_LINES)]
                right_text = RIGHT_LINES[line_index % len(RIGHT_LINES)]
                page.cjk(font, left_text, 11, left_x, y)
                page.latin(right_text, 10, right_x, y)
            else:
                page.latin(LEFT_LINES[line_index % len(LEFT_LINES)], 11, left_x, y)
                page.latin(RIGHT_LINES[line_index % len(RIGHT_LINES)], 11, right_x, y)

        # 表格（通栏，列位置固定 —— 表格识别靠的就是这个）。
        table_top = column_top - params["column_lines"] * line_gap - 34.0
        columns = params["table_columns"]
        rows = params["table_rows"]
        column_x = [50.0 + index * params["table_column_gap"] for index in range(columns)]
        for row_index in range(rows):
            y = table_top - row_index * params["table_row_gap"]
            values = TABLE_HEADERS if row_index == 0 else TABLE_ROWS[(row_index - 1) %
                                                                   len(TABLE_ROWS)]
            for column in range(columns):
                page.latin(values[column % len(values)], 12, column_x[column], y)
        table_count += 1

        # 图片。
        for image_index in range(params["images_per_page"]):
            name = "Im%d" % (image_index + 1)
            width = params["image_width"]
            height = params["image_height"]
            x = 50.0 + image_index * (width + 30.0)
            y = params["image_y"]
            pixels = make_pixels(24, 18, index + image_index)
            object_id = builder.add_image(pixels, 24, 18)
            page.xobjects[name] = object_id
            page.image(name, width, height, x, y)
            image_count += 1

    return builder.build(), {
        "pages": page_count,
        "images": image_count,
        "tables": table_count,
        "table_rows": params["table_rows"],
        "table_columns": params["table_columns"],
        "images_per_page": params["images_per_page"],
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description="生成 PDF → Office 的四合一语料")
    parser.add_argument("--out", default=DEFAULT_OUT, help="输出目录")
    parser.add_argument("--count", type=int, default=50, help="造多少份（默认 50）")
    parser.add_argument("--quiet", action="store_true", help="不打印进度")
    args = parser.parse_args(argv)

    os.makedirs(args.out, exist_ok=True)

    # 中文字体：内嵌 Type0/Identity-H。字体文件子集化后再用。
    font_path = cjk.find_font("cjk")
    if font_path is None:
        print("!! 找不到中文字体（候选：%s）" % ", ".join(cjk.FONT_CANDIDATES["cjk"]))
        print("   这份语料需要中文字体才能抽出中文；请先装一个。")
        return 2
    with open(font_path, "rb") as handle:
        raw_font = handle.read()

    # 语料里会出现的全部汉字（子集时要用）。
    used = set()
    for group in (CJK_LEFT_LINES, CJK_RIGHT_LINES, MIXED_LEFT_LINES):
        for line in group:
            used.update(line)
    subset = _subset_ttf(raw_font, used)
    if not args.quiet:
        print("字体子集：%.1f MB → %.1f KB（%d 个字形）" %
              (len(raw_font) / 1048576.0, len(subset) / 1024.0, len(used)))

    font = cjk.EmbeddedFont("cjk", "F2")
    # 用子集替掉字体内容（EmbeddedFont 读的是完整文件，这里直接换掉 bytes）。
    # GID 保持不变，所以 cmap 查出来的 GID 在子集里依然是对的。
    font.data = subset

    documents = []
    for index in range(args.count):
        params = {
            "pages": 1 + (index % 3),
            "column_width": 210.0 + (index % 5) * 12.0,
            "gutter": 20.0 + (index % 4) * 6.0,
            "column_lines": 6 + (index % 3),
            "line_gap": 16.0 + (index % 3) * 2.0,
            "cjk_ratio": [0.0, 0.5, 1.0][index % 3],
            "table_columns": 3,
            "table_rows": 3 + (index % 2),
            "table_column_gap": 120.0 + (index % 4) * 15.0,
            "table_row_gap": 26.0,
            "images_per_page": 1 + (index % 2),
            "image_width": 120.0 + (index % 3) * 20.0,
            "image_height": 90.0,
            "image_y": 120.0 + (index % 4) * 30.0,
        }
        pdf, expected = build_document(index, params, font)
        name = "office-%03d.pdf" % (index + 1)
        with open(os.path.join(args.out, name), "wb") as handle:
            handle.write(pdf)
        expected = dict(expected)
        expected["name"] = name
        expected["params"] = params
        documents.append(expected)
        if not args.quiet and (index + 1) % 10 == 0:
            print("  %s %d/%d" % (name, index + 1, args.count))

    meta = {
        "generator": "scripts/gen-office-corpus.py",
        "count": len(documents),
        "note": "每份语料的 pages / images / tables 是生成时定死的期望值，"
                "验收脚本拿它当标准答案。",
        "documents": documents,
    }
    meta_path = os.path.join(args.out, "corpus.meta.json")
    with open(meta_path, "w", encoding="utf-8") as handle:
        json.dump(meta, handle, ensure_ascii=False, indent=2)
    if not args.quiet:
        print("语料 %d 份 → %s（期望值在 %s）" % (len(documents), args.out, meta_path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
