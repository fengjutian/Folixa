#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成复杂 CJK 排版兼容语料（A12），并提供两个验收脚本共用的 PDFium 探针。

语料覆盖：生僻字与扩展区汉字、CJK 标点、全角字符、基字 + 变音符的组合字符、
中英混排、竖排文字、长段落，以及一页几千字的超大字符集。

PDF 写法复用仓库既有的 scripts\\generate-test-pdfs.py（容器拼装与 TrueType cmap 解析），
不另造一套。字体按 PDF 1.7 的 Type0/Identity-H + CIDFontType2 + CIDToGIDMap + ToUnicode 嵌入：
码就是字体的 GID，ToUnicode 负责把码还原成 Unicode（非 BMP 字用 UTF-16BE 代理对表示）。
CIDToGIDMap 必须显式给出：隐式恒等映射下 PDFium 取不到字形，非 BMP 字会渲染成空白。

探针（probe 子命令）用 ctypes 直连仓库固定的 PDFium（tools\\pdfium\\bin\\pdfium.dll），
两个验收脚本都要用它：
  - verify-optimize-fidelity.ps1 要逐像素比对优化前后的渲染结果；
  - verify-cjk-corpus.ps1 要查页数、抽文字、看渲染是否崩。
仓库本轮只允许新增三个文件，所以探针只此一份实现。

用法：
    python scripts\\gen-cjk-corpus.py gen --out build\\cjk-corpus
    python scripts\\gen-cjk-corpus.py probe --pdf a.pdf --pdf b.pdf --json-out probe.json
"""

import argparse
import ctypes
import importlib.util
import json
import os
import struct
import sys
import zlib

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))


def _load_module(name, path):
    """按路径加载模块：仓库里 generate-test-pdfs.py 的文件名带连字符，不能直接 import。"""
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# 复用仓库既有的 PDF 容器拼装与 TrueType cmap 解析。
gtp = _load_module("generate_test_pdfs", os.path.join(SCRIPT_DIR, "generate-test-pdfs.py"))

DEFAULT_OUT = os.path.join(REPO_ROOT, "build", "cjk-corpus")
DEFAULT_PDFIUM = os.path.join(REPO_ROOT, "tools", "pdfium", "bin", "pdfium.dll")

# 各类字体的候选路径：按顺序取第一个存在的。
# Ext-B（U+20000 起的扩展区）只有 SimSun-ExtB 覆盖得全，
# Ext-G（U+30000 起）由 Simsun-ExtG 覆盖，两者都是系统自带的扩展字体。
FONT_CANDIDATES = {
    "cjk": [
        r"C:\Windows\Fonts\simhei.ttf",
        r"C:\Windows\Fonts\simfang.ttf",
        r"C:\Windows\Fonts\Deng.ttf",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/System/Library/Fonts/PingFang.ttc",
    ],
    "latin": [
        r"C:\Windows\Fonts\NotoSans-Regular.ttf",
        r"C:\Windows\Fonts\arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    ],
    "extb": [
        r"C:\Windows\Fonts\simsunb.ttf",
    ],
    "extg": [
        r"C:\Windows\Fonts\SimsunExtG.ttf",
    ],
}


def find_font(kind):
    """返回该类字体的路径；一个都没有就返回 None（调用方要如实报错，不能偷偷跳过）。"""
    for path in FONT_CANDIDATES.get(kind, []):
        if os.path.exists(path):
            return path
    return None


def parse_units_per_em(data):
    """head 表里的 unitsPerEm：字体单位与 PDF 的 1/1000 em 之间要换算。"""
    if len(data) < 12:
        return 1000
    num_tables = struct.unpack(">H", data[4:6])[0]
    for index in range(num_tables):
        entry = 12 + index * 16
        if entry + 16 > len(data):
            break
        tag = data[entry:entry + 4].decode("latin-1")
        if tag != "head":
            continue
        offset = struct.unpack(">I", data[entry + 8:entry + 12])[0]
        if offset + 20 > len(data):
            break
        units = struct.unpack(">H", data[offset + 18:offset + 20])[0]
        return units if units else 1000
    return 1000


def parse_hmtx(data):
    """读 TrueType 的 hmtx，返回每个字形的 advance 宽度（font units）。

    没有它就只能用 /DW 1000：那样每个拉丁字母都占一个全角宽，
    混排与组合音符的版式会完全走样，语料就失去意义了。
    """
    if len(data) < 12:
        return []
    num_tables = struct.unpack(">H", data[4:6])[0]
    tables = {}
    for index in range(num_tables):
        entry = 12 + index * 16
        if entry + 16 > len(data):
            break
        tag = data[entry:entry + 4].decode("latin-1")
        offset, length = struct.unpack(">II", data[entry + 8:entry + 16])
        tables[tag] = (offset, length)
    if "hhea" not in tables or "hmtx" not in tables or "maxp" not in tables:
        return []
    hhea_offset = tables["hhea"][0]
    metrics_count = struct.unpack(">H", data[hhea_offset + 34:hhea_offset + 36])[0]
    maxp_offset = tables["maxp"][0]
    glyph_count = struct.unpack(">H", data[maxp_offset + 4:maxp_offset + 6])[0]
    hmtx_offset = tables["hmtx"][0]
    widths = []
    for index in range(glyph_count):
        if index < metrics_count:
            position = hmtx_offset + index * 4
            if position + 2 > len(data):
                break
            widths.append(struct.unpack(">H", data[position:position + 2])[0])
        else:
            widths.append(widths[-1] if widths else 0)
    return widths


def parse_vhea_vmtx(data):
    """读 TrueType 的 vhea / vmtx，返回竖排度量（font units）。

    返回 (metrics, reason)：
      metrics = {"ascent":…, "descent":…, "default_advance":…, "advances": {gid: advance}}
      没有 vhea / vmtx 时返回 (None, 原因) —— **如实说没有，不编数**。

    表结构：
      * vhea 与 hhea 一样是 36 字节定长表，ascender 在偏移 4、descender 在 6、
        lineGap 在 8、metricDataFormat 在 32、numberOfVMetrics 在 34；
      * vmtx 的前 numberOfVMetrics 个字形各占 4 字节（advanceHeight + topSideBearing），
        其余字形沿用最后一条记录的 advanceHeight（TrueType 规范如此）。
    """
    if len(data) < 12:
        return None, "字体太小，读不出竖排度量"
    num_tables = struct.unpack(">H", data[4:6])[0]
    tables = {}
    for index in range(num_tables):
        entry = 12 + index * 16
        if entry + 16 > len(data):
            break
        tag = data[entry:entry + 4].decode("latin-1")
        offset, length = struct.unpack(">II", data[entry + 8:entry + 16])
        tables[tag] = (offset, length)
    if "vhea" not in tables or "vmtx" not in tables:
        return None, "字体没有竖排度量（缺 vhea / vmtx 表）"
    vhea_offset, vhea_length = tables["vhea"]
    if vhea_length < 36:
        return None, "vhea 表太短，读不出竖排度量"
    ascent, descent = struct.unpack(">hh", data[vhea_offset + 4:vhea_offset + 8])
    number_of_vmetrics = struct.unpack(">H", data[vhea_offset + 34:vhea_offset + 36])[0]
    if number_of_vmetrics <= 0:
        return None, "vhea 里的 numberOfVMetrics 非法"
    vmtx_offset, vmtx_length = tables["vmtx"]
    if vmtx_length < number_of_vmetrics * 4:
        return None, "vmtx 表太短，放不下竖排度量"
    advances = {}
    last_advance = 0
    for gid in range(number_of_vmetrics):
        last_advance = struct.unpack(">H", data[vmtx_offset + gid * 4:vmtx_offset + gid * 4 + 2])[0]
        advances[gid] = last_advance
    glyph_count = 0
    if "maxp" in tables:
        maxp_offset = tables["maxp"][0]
        glyph_count = struct.unpack(">H", data[maxp_offset + 4:maxp_offset + 6])[0]
    for gid in range(number_of_vmetrics, glyph_count):
        advances[gid] = last_advance
    return {
        "ascent": ascent,
        "descent": descent,
        "default_advance": last_advance,
        "advances": advances,
    }, ""


# ---------------------------------------------------------------------------
# PDF 组装
# ---------------------------------------------------------------------------


class Document:
    """一份 PDF：对象号 -> 对象体。最后交给 generate-test-pdfs 的 _build_pdf 拼容器。"""

    def __init__(self):
        self.objects = {}
        self.next_id = 1

    def add(self, body):
        object_id = self.next_id
        self.next_id += 1
        self.objects[object_id] = body
        return object_id

    def build(self, root_id):
        items = sorted(self.objects.items())
        return gtp._build_pdf(items, root_id)


class EmbeddedFont:
    """内嵌字体：TrueType 原文 + Unicode -> GID 映射。

    语料里的每个字都必须在字体里真的存在（不许拿占位 GID 糊弄），
    缺字时直接报错：那样生成出来的 PDF 渲染出来是空白，兼容性回归就失去意义。
    """

    def __init__(self, kind, resource):
        self.kind = kind
        self.resource = resource
        self.path = find_font(kind)
        if self.path is None:
            raise RuntimeError("找不到 %s 类字体，候选：%s" %
                               (kind, ", ".join(FONT_CANDIDATES.get(kind, []))))
        with open(self.path, "rb") as handle:
            self.data = handle.read()
        self.cmap = gtp._ttf_unicode_to_gid(self.data)
        if not self.cmap:
            raise RuntimeError("字体 %s 解析不出 cmap" % self.path)
        self.widths = parse_hmtx(self.data)
        self.units_per_em = parse_units_per_em(self.data)
        self.base_name = "Folixa" + os.path.splitext(os.path.basename(self.path))[0].replace(" ", "")
        self.used = {}
        # 竖排度量（vhea / vmtx）。没有就是 None —— 如实说没有，不编数。
        self.vertical, self.vertical_reason = parse_vhea_vmtx(self.data)

    def vertical_scale(self):
        """字体单位 -> 1/1000 em。"""
        return 1000.0 / float(self.units_per_em or 1000)

    def vertical_origin_y(self):
        """竖排原点的高度（1/1000 em）。"""
        if not self.vertical:
            return 0
        return int(round(self.vertical["ascent"] * self.vertical_scale()))

    def vertical_default_advance(self):
        """竖排默认宽度：一个字身框有多高（1/1000 em）。"""
        if not self.vertical:
            return 0
        return int(round(self.vertical["default_advance"] * self.vertical_scale()))

    def vertical_advance(self, gid):
        """某个字形的竖排 advance（1/1000 em）。"""
        if not self.vertical:
            return 0
        raw = self.vertical["advances"].get(gid)
        if raw is None:
            raw = self.vertical["default_advance"]
        return int(round(raw * self.vertical_scale()))

    def gid_for(self, char):
        """字 -> GID；字体里没有就返回 None。"""
        code = ord(char)
        gid = self.cmap.get(code)
        if gid is not None:
            self.used[gid] = char
        return gid

    def encode(self, text):
        """文本 -> 十六进制码串（码就是 GID）。缺字直接报错。"""
        gids = []
        for char in text:
            gid = self.gid_for(char)
            if gid is None:
                raise RuntimeError("字体 %s 里没有字符 U+%04X（%s）" %
                                   (os.path.basename(self.path), ord(char), char))
            gids.append(gid)
        return "".join("%04X" % gid for gid in gids)

    def width_array(self):
        """CIDFont 的 /W 数组：把用到的字形宽度写清楚（单位 1/1000 em）。"""
        if not self.widths:
            return "[]"
        scale = 1000.0 / float(self.units_per_em or 1000)
        gids = sorted(self.used)
        chunks = []
        run_start = gids[0]
        run_widths = []
        for gid in gids:
            if gid == run_start + len(run_widths):
                run_widths.append(int(round(self.widths[gid] * scale)))
                continue
            chunks.append((run_start, run_widths))
            run_start = gid
            run_widths = [int(round(self.widths[gid] * scale))]
        chunks.append((run_start, run_widths))
        parts = []
        for start, widths in chunks:
            # PDF 1.7 表 116：c [w1 w2 ... wn] 表示从 CID c 开始的连续 n 个宽度。
            # 后面不能再跟一个「结束 CID」——那是 cfirst clast w 那种写法才有的，
            # 多写会让解析器把后面的条目当成新的 CID，字体字典直接被读歪。
            parts.append("%d [%s]" % (start, " ".join(str(w) for w in widths)))
        return "[%s]" % " ".join(parts)

    def vertical_width_array(self):
        """竖排的 /W2 数组：c [w1y v_x v_y]。

        三个数的顺序按 ISO 32000-1 §9.7.4.3：
          * w1y 是竖排位移（沿书写轴的推进量），**向下为负**（PDF user space 的 y 向上，
            竖排往下走，所以是负数；一个全角字是 -1000）；
          * v_x、v_y 是「从字形的横排原点到竖排原点」的向量分量
            （v_x = 500 让字身框居中于列心，v_y = 字体的竖排 ascent）。

        只给一个数会把 /W2 读歪：PDFium 按每组三个数取，缺一个就串位。
        CID 是**绝对编号**（Identity 映射下就是 GID），连续且度量相同的会并成一条。
        """
        if not self.vertical:
            return None
        origin_y = self.vertical_origin_y()
        entries = []
        for gid in sorted(self.used):
            values = [-self.vertical_advance(gid), 500, origin_y]
            entries.append((gid, values))
        parts = []
        index = 0
        while index < len(entries):
            gid, values = entries[index]
            group = list(values)
            nxt = index + 1
            while (nxt < len(entries) and entries[nxt][0] == entries[nxt - 1][0] + 1
                   and entries[nxt][1] == entries[nxt - 1][1]):
                group.extend(entries[nxt][1])
                nxt += 1
            parts.append("%d [%s]" % (gid, " ".join(str(value) for value in group)))
            index = nxt
        return "[%s]" % " ".join(parts)

    def vertical_font_entries(self):
        """CID 字体字典里的竖排条目：/WMode 1 + /DW2（+ /W2）。

        没有竖排度量时只给 /WMode 1 与规范默认的 /DW2 [880 -1000]，
        **不给 /W2**（不编数）。
        """
        if not self.vertical:
            return "/WMode 1 /DW2 [880 -1000]"
        entries = "/WMode 1 /DW2 [%d %d]" % (self.vertical_origin_y(),
                                              -self.vertical_default_advance())
        widths = self.vertical_width_array()
        if widths:
            entries += " /W2 %s" % widths
        return entries


def to_unicode_cmap(entries):
    """ToUnicode CMap：GID -> Unicode。非 BMP 字的目标写成 UTF-16BE 代理对。"""
    lines = [
        "/CIDInit /ProcSet findresource begin",
        "12 dict begin",
        "begincmap",
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> def",
        "/CMapName /Adobe-Identity-UCS def",
        "/CMapType 2 def",
        "1 begincodespacerange",
        "<0000> <FFFF>",
        "endcodespacerange",
    ]
    for start in range(0, len(entries), 100):
        chunk = entries[start:start + 100]
        lines.append("%d beginbfchar" % len(chunk))
        for gid, char in chunk:
            lines.append("<%04X> <%s>" % (gid, char.encode("utf-16-be").hex().upper()))
        lines.append("endbfchar")
    lines += ["endcmap", "CMapName currentdict /CMap defineresource pop", "end", "end"]
    return "\n".join(lines) + "\n"


class Page:
    """一页：内容流 + 字体资源。"""

    def __init__(self, width=612, height=792):
        self.width = width
        self.height = height
        self.parts = []
        self.fonts = []

    def use_font(self, font):
        if font not in self.fonts:
            self.fonts.append(font)
        return font

    def text(self, font, text, size, x, y):
        """横排一行：基线在 (x, y)，y 是 PDF user space 坐标（左下原点）。"""
        self.use_font(font)
        codes = font.encode(text)
        self.parts.append("BT /%s %d Tf %d %d Td <%s> Tj ET" %
                          (font.resource, size, x, y, codes))

    def vertical(self, font, text, size, x, top, leading):
        """竖排一列：逐字下移。PDF 基础内容流没有竖排模式，只能一个字一个字定位。"""
        for index, char in enumerate(text):
            self.text(font, char, size, x, top - index * leading)

    def vertical_wmode(self, font, text, size, x, top):
        """竖排一列（真竖排，WMode 1）：内容流按阅读顺序写。

        与上面那个「模拟竖排」的区别：
          * 模拟竖排是**一个字一个字定位**（每个字一个 Td），字体并不知道自己在竖排；
          * 这里只摆一次列的起点（Tm），字与字之间的推进交给字体的竖排量
            （/W2、/DW2），这才是 PDF 规范里的竖排书写模式。

        内容流的顺序就是阅读顺序（列内自上而下），所以抽取出来的顺序与渲染一致。
        """
        self.use_font(font)
        codes = font.encode(text)
        parts = ["BT /%s %d Tf 1 0 0 1 %d %d Tm" % (font.resource, size, x, top)]
        for index in range(0, len(codes), 4):
            parts.append("<%s> Tj" % codes[index:index + 4])
        parts.append("ET")
        self.parts.append(" ".join(parts))

    def content(self):
        return "\n".join(self.parts).encode("ascii")


class DocumentBuilder:
    """把页面与字体组装成 PDF。"""

    def __init__(self):
        self.document = Document()
        # 1 号留给 Catalog，2 号留给 Pages；页面与内容流从 3 号开始排，
        # 否则第一个页面的对象会被后面的 Catalog/Pages 覆盖掉（首页会变空白）。
        self.document.next_id = 3
        self.pages = []
        self.fonts = {}

    def add_page(self, page):
        self.pages.append(page)
        return page

    def font_object(self, font):
        """生成 Type0 字体相关的全部对象，返回 Type0 字体对象号。"""
        if font.resource in self.fonts:
            return self.fonts[font.resource]
        entries = sorted(font.used.items())
        if not entries:
            raise RuntimeError("字体 %s 没有任何字符被使用" % font.path)

        # CIDToGIDMap：恒等映射（CID 就是 GID）。必须显式给出，
        # 否则 PDFium 走不到字形，非 BMP 字渲染成空白。
        max_cid = max(gid for gid, _ in entries)
        identity = b"".join(struct.pack(">H", index) for index in range(max_cid + 1))
        compressed_identity = zlib.compress(identity, 6)
        map_id = self.document.add(
            b"<< /Length %d /Filter /FlateDecode >>\nstream\n%s\nendstream" %
            (len(compressed_identity), compressed_identity))

        font_data = zlib.compress(font.data, 6)
        file_id = self.document.add(
            b"<< /Length %d /Filter /FlateDecode >>\nstream\n%s\nendstream" %
            (len(font_data), font_data))

        descriptor_id = self.document.add(
            ("<< /Type /FontDescriptor /FontName /%s /Flags 4 "
             "/FontBBox [-500 -300 1500 1200] /ItalicAngle 0 /Ascent 880 /Descent -120 "
             "/CapHeight 880 /StemV 80 /FontFile2 %d 0 R >>" %
             (font.base_name, file_id)).encode("ascii"))

        # 竖排字体（真竖排）：CID 字体字典上写 /WMode 1 + /DW2（+ /W2）。
        #
        # 另外 Type0 的 /Encoding 要用 Identity-V：PDF 规范里竖排由 CID 字体的
        # /WMode 1 声明，但 **PDFium 判竖排看的是 Encoding CMap**
        # （CPDF_CIDFont::IsVertWriting 走的是 cmap_->IsVertWriting()），
        # 两处都写，PDFium 才会真的按竖排排、也才会按竖排推进。
        vertical_extra = font.vertical_font_entries() if font.vertical else ""
        encoding = "Identity-V" if font.vertical else "Identity-H"

        cid_font_id = self.document.add(
            ("<< /Type /Font /Subtype /CIDFontType2 /BaseFont /%s "
             "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
             "/FontDescriptor %d 0 R /DW 1000 /W %s%s /CIDToGIDMap %d 0 R >>" %
             (font.base_name, descriptor_id, font.width_array(),
              (" " + vertical_extra) if vertical_extra else "",
              map_id)).encode("ascii"))

        cmap = to_unicode_cmap(entries)
        cmap_bytes = cmap.encode("ascii")
        to_unicode_id = self.document.add(
            b"<< /Length %d >>\nstream\n%s\nendstream" % (len(cmap_bytes), cmap_bytes))

        type0_id = self.document.add(
            ("<< /Type /Font /Subtype /Type0 /BaseFont /%s /Encoding /%s "
             "/DescendantFonts [%d 0 R] /ToUnicode %d 0 R >>" %
             (font.base_name, encoding, cid_font_id, to_unicode_id)).encode("ascii"))

        self.fonts[font.resource] = type0_id
        return type0_id

    def build(self):
        page_ids = []
        for page in self.pages:
            content_id = self.document.add(
                b"<< /Length %d >>\nstream\n%s\nendstream" %
                (len(page.content()), page.content()))
            resources = []
            for font in page.fonts:
                font_object_id = self.font_object(font)
                resources.append("/%s %d 0 R" % (font.resource, font_object_id))
            page_id = self.document.add(
                ("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] "
                 "/Resources << /Font << %s >> >> /Contents %d 0 R >>" %
                 (page.width, page.height, " ".join(resources), content_id)).encode("ascii"))
            page_ids.append(page_id)

        pages_id = 2
        self.document.objects[pages_id] = (
            "<< /Type /Pages /Kids [%s] /Count %d >>" %
            (" ".join("%d 0 R" % pid for pid in page_ids), len(page_ids))).encode("ascii")
        self.document.objects[1] = b"<< /Type /Catalog /Pages 2 0 R >>"
        return self.document.build(1)


# ---------------------------------------------------------------------------
# 语料
# ---------------------------------------------------------------------------

LONG_PARAGRAPH = (
    "复杂的中文排版并不只是把字摆上去那么简单：字体要内嵌，编码要能让抽取工具还原回原文，"
    "标点要占对位置，行距行距和断行都要看起来正常。真实世界里排出来的 PDF 还会遇到各种"
    "边角情况，比如全角字母和半角数字混在一起、竖排的稿子、从繁体转简体的文档、"
    "带组合音符的外文名字，以及一页塞满几千字的海报。这些文档在阅读器里既要看得懂，"
    "也要能被搜索和复制，因此每一类都需要有对应的回归语料。"
)


def _meta(name, feature, pages, anchors, notes, ink_pages=None, fonts=None,
          exact_pages=None, wmode=0):
    meta = {
        "name": name,
        "file": "%s.pdf" % name,
        "feature": feature,
        "pages": pages,
        "ink_pages": list(range(pages)) if ink_pages is None else list(ink_pages),
        "anchors": list(anchors),
        "fonts": fonts or [],
        "notes": notes,
    }
    # 逐页期望的抽取串：**顺序敏感**，全等才算过（与「锚点子串命中」是两回事）。
    if exact_pages:
        meta["exact_pages"] = list(exact_pages)
    # 字体字典里应当带 /WMode 1 时填 1（验收脚本会真的去查字体字典）。
    if wmode:
        meta["wmode"] = wmode
    return meta


def fixture_rare_glyphs():
    """生僻字与扩展区汉字：BMP 稀字 + 扩展 A 区 + 扩展 B 区 + 扩展 G 区。"""
    builder = DocumentBuilder()
    cjk = EmbeddedFont("cjk", "F1")
    extb = EmbeddedFont("extb", "F2")
    extg = EmbeddedFont("extg", "F3")

    page_one = builder.add_page(Page())
    page_one.text(cjk, "生僻字与扩展区汉字（BMP 与扩展 A 区）", 20, 56, 720)
    page_one.text(cjk, "龘 齉 龗 鵿 嚤 㐀 㘀 䶮", 28, 56, 660)
    page_one.text(cjk, "扩展 A 区起于 U+3400，扩展 B 区起于 U+20000，扩展 G 区起于 U+30000。", 14, 56, 620)

    page_two = builder.add_page(Page())
    page_two.text(cjk, "扩展 B 区与扩展 G 区（非 BMP，UTF-16 代理对）", 20, 56, 720)
    page_two.text(extb, "𠀀 𠀁 𪜀 𪝀", 28, 56, 660)
    page_two.text(extg, "\U00030EDD \U00030EDE \U0002EBF3", 28, 56, 610)
    page_two.text(cjk, "非 BMP 字在 ToUnicode 里写成代理对，抽取时要能还原成一个字符。", 14, 56, 570)

    meta = _meta(
        "rare-glyphs", "生僻字与扩展区汉字", 2,
        ["龘", "齉", "鵿", "㐀", "\U00020000", "\U0002A700", "\U00030EDD"],
        "第 1 页是 BMP 稀字与扩展 A 区（SimHei），第 2 页是扩展 B 区（SimSun-ExtB）与"
        "扩展 G 区（Simsun-ExtG）。非 BMP 字必须靠 ToUnicode 的代理对还原。",
        fonts=[os.path.basename(cjk.path), os.path.basename(extb.path),
               os.path.basename(extg.path)])
    return builder.build(), meta


def fixture_cjk_punctuation():
    """CJK 标点：成对符号、省略号、句读、书名号。"""
    builder = DocumentBuilder()
    cjk = EmbeddedFont("cjk", "F1")

    page = builder.add_page(Page())
    page.text(cjk, "中文标点兼容性测试", 20, 56, 720)
    page.text(cjk, "「引号」『嵌套引号』《书名号》〈单书名号〉", 16, 56, 670)
    page.text(cjk, "【方括号】〔鱼尾括号〕（圆括号）", 16, 56, 640)
    page.text(cjk, "句号。逗号，顿号、分号；冒号：问号？叹号！", 16, 56, 610)
    page.text(cjk, "省略号……两点省略号‥破折号——", 16, 56, 580)
    page.text(cjk, "叠字符号々圈号〇角号ヶ波浪线～", 16, 56, 550)

    meta = _meta("cjk-punctuation", "CJK 标点", 1,
                 ["「引号」", "《书名号》", "【方括号】", "句号。逗号", "省略号……", "叠字符号々"],
                 "标点是抽取与渲染最容易出错的地方：全角标点要与相邻汉字同宽，"
                 "省略号是单个 U+2026 而不是三个点。",
                 fonts=[os.path.basename(cjk.path)])
    return builder.build(), meta


def fixture_fullwidth():
    """全角字符：全角字母、全角数字与全角符号。"""
    builder = DocumentBuilder()
    cjk = EmbeddedFont("cjk", "F1")

    page = builder.add_page(Page())
    page.text(cjk, "全角字符兼容性测试", 20, 56, 720)
    page.text(cjk, "ＡＢＣ ａｂｃ １２３ ４５６ ７８９ ０", 18, 56, 670)
    page.text(cjk, "！＠＃＄％＾＆＊（）［］｛｝", 18, 56, 635)
    page.text(cjk, "－＿＝＋｜＼；：，．", 18, 56, 600)
    page.text(cjk, "全角与半角 A1 对照：Ａ１ 与 A1", 14, 56, 560)

    meta = _meta("fullwidth", "全角字符", 1,
                 ["ＡＢＣ", "ａｂｃ", "１２３", "！＠＃", "全角与半角"],
                 "全角字母数字落在 U+FF01..U+FF5E，与半角形态不同，"
                 "抽取时不能被归一化成半角。",
                 fonts=[os.path.basename(cjk.path)])
    return builder.build(), meta


def fixture_combining():
    """基字 + 变音符的组合字符：分解形式（NFC 之前）与预组合形式并存。"""
    builder = DocumentBuilder()
    latin = EmbeddedFont("latin", "F1")
    cjk = EmbeddedFont("cjk", "F2")

    # 分解形式必须显式写成 基字 + 组合记号，源码里不能直接放预组合字符，
    # 否则「有没有真的带上变音符」这件事在代码里看不出来。
    e_acute = "e\u0301"
    a_macron = "a\u0304"
    o_circumflex = "o\u0302"
    u_diaeresis_acute = "u\u0308\u0301"
    n_tilde = "n\u0303"

    page = builder.add_page(Page())
    page.text(latin, "Combining marks", 20, 56, 720)
    page.text(latin, "e+U+0301 = %s   a+U+0304 = %s   o+U+0302 = %s" %
              (e_acute, a_macron, o_circumflex), 18, 56, 670)
    page.text(latin, "u+U+0308+U+0301 = %s   n+U+0303 = %s" %
              (u_diaeresis_acute, n_tilde), 18, 56, 635)
    page.text(cjk, "预组合形式对照：", 18, 56, 600)
    page.text(latin, "\u00e9 \u00e0 \u014d \u01d6 \u00f1", 18, 190, 600)
    page.text(cjk, "中文里的组合符号：㐀 䶮 与全角 ＡＢＣ", 16, 56, 560)

    meta = _meta("combining", "基字 + 变音符的组合字符", 1,
                 [e_acute, a_macron, u_diaeresis_acute, n_tilde, "中文里的组合符号"],
                 "分解形式（基字 + U+0301 等）必须原样抽出，不能被吞掉或被归一化；"
                 "这一条用拉丁字体覆盖，因为组合变音符在中文宋体类字体里基本没有字形。",
                 fonts=[os.path.basename(latin.path), os.path.basename(cjk.path)])
    return builder.build(), meta


def fixture_mixed_script():
    """中英混排：中文里夹英文单词、版本号、日期与西文标点。"""
    builder = DocumentBuilder()
    cjk = EmbeddedFont("cjk", "F1")

    page_one = builder.add_page(Page())
    page_one.text(cjk, "中英混排兼容性测试", 20, 56, 720)
    page_one.text(cjk, "Folixa 编辑器支持中英混排：Version 0.15.0（2026-11-17）", 16, 56, 670)
    page_one.text(cjk, "搜索关键词 PDFium、qpdf、Ghostscript 都要能命中。", 16, 56, 640)
    page_one.text(cjk, "标点混排：中文逗号，English comma; 括号（parentheses）与 (brackets)。", 16, 56, 610)

    page_two = builder.add_page(Page())
    page_two.text(cjk, "第二页继续混排", 20, 56, 720)
    page_two.text(cjk, "混排测试：PDF/A-2b、ISO 32000-2、qpdf 12.4.2 与 Ghostscript 优化。", 16, 56, 670)
    page_two.text(cjk, "比例 1:2、3.14%、100%、2026-11-17T08:30:00+08:00", 16, 56, 640)

    meta = _meta("mixed-script", "中英混排", 2,
                 ["中英混排", "Version 0.15.0", "qpdf 12.4.2", "PDFium、qpdf、Ghostscript", "第二页继续混排"],
                 "中英混排要注意半角与全角标点混在一行时的字距，以及英文单词不能被拆断。",
                 fonts=[os.path.basename(cjk.path)])
    return builder.build(), meta


def fixture_vertical():
    """竖排文字：逐字定位的竖列，列序从右到左。"""
    builder = DocumentBuilder()
    cjk = EmbeddedFont("cjk", "F1")

    page_one = builder.add_page(Page())
    page_one.text(cjk, "竖排文字（右起第一列）", 18, 56, 740)
    page_one.vertical(cjk, "竖排文字测试第一列", 22, 300, 700, 26)
    page_one.vertical(cjk, "第二列继续往下排", 22, 260, 700, 26)
    page_one.vertical(cjk, "第三列收尾", 22, 220, 700, 26)

    page_two = builder.add_page(Page())
    page_two.text(cjk, "竖排混排", 18, 56, 740)
    page_two.vertical(cjk, "竖排里也有English和123", 22, 300, 700, 26)

    meta = _meta("vertical", "竖排文字", 2,
                 ["竖排文字测试第一列", "第二列继续往下排", "第三列收尾", "竖排里也有English和123"],
                 "PDF 基础内容流没有竖排模式，竖排只能逐字定位；"
                 "这一页检验抽取时的阅读顺序是否仍然是自上而下的列内顺序。",
                 fonts=[os.path.basename(cjk.path)])
    return builder.build(), meta


def fixture_long_paragraph():
    """长段落：整段中文正文自动折行。"""
    builder = DocumentBuilder()
    cjk = EmbeddedFont("cjk", "F1")

    page = builder.add_page(Page(width=595, height=842))
    page.text(cjk, "长段落兼容性测试", 18, 50, 790)
    body = LONG_PARAGRAPH
    per_line = 26
    line_height = 22
    y = 752
    for start in range(0, len(body), per_line):
        chunk = body[start:start + per_line]
        page.text(cjk, chunk, 12, 50, y)
        y -= line_height
        if y < 60:
            break

    meta = _meta("long-paragraph", "长段落", 1,
                 [body[:10], body[-12:], "长段落兼容性测试"],
                 "整段正文按固定宽度折行，检验抽取时不能丢字、不能把行尾字串到一起。",
                 fonts=[os.path.basename(cjk.path)])
    return builder.build(), meta


def fixture_huge_charset():
    """超大字符集：一页几千个互不相同的汉字。"""
    builder = DocumentBuilder()
    cjk = EmbeddedFont("cjk", "F1")

    # 从字体 cmap 里取连续的汉字：必须是字体真有的字形，否则渲染出来是空白。
    codes = []
    code = 0x4E00
    while len(codes) < 3000 and code < 0x9FFF:
        if code in cjk.cmap:
            codes.append(code)
        code += 1
    if len(codes) < 3000:
        raise RuntimeError("字体 %s 的汉字覆盖不足，凑不出 3000 个字" % cjk.path)

    columns = 40
    size = 8
    leading = 9.6
    left = 30
    top = 780
    page = builder.add_page(Page(width=595, height=842))
    page.text(cjk, "超大字符集：单页 3000 个互不相同的汉字", 14, 40, 810)
    for index, codepoint in enumerate(codes):
        column = index % columns
        row = index // columns
        x = left + column * 13.5
        y = top - row * leading
        if y < 40:
            break
        page.text(cjk, chr(codepoint), size, x, y)

    first = chr(codes[0])
    last = chr(codes[-1])
    meta = _meta("huge-charset", "超大字符集（单页 3000 字）", 1,
                 [first, last, "超大字符集"],
                 "整页 3000 个互不相同的汉字，ToUnicode 里有 3000 条映射，"
                 "用来压抽取与渲染路径。",
                 fonts=[os.path.basename(cjk.path)])
    return builder.build(), meta


def fixture_vertical_wmode1():
    """竖排文字（真的 WMode 1）：字体字典写 /WMode 1，内容流按阅读顺序写。

    这是「模拟竖排」那份 vertical 夹具的对照：
      * vertical：逐字定位（每个字一个 Td），字体并不知道自己在竖排 —— 模拟；
      * vertical-wmode1：CID 字体字典带 /WMode 1 + /DW2 + /W2，
        内容流按阅读顺序（列右→左、列内上→下）写 —— 真正的竖排书写模式。

    两份样本放在一起，才能证明「模拟竖排」与「真竖排」在 PDF 里不是一回事。
    """
    builder = DocumentBuilder()
    cjk = EmbeddedFont("cjk", "F1")
    if not cjk.vertical:
        # 没有竖排度量就造不出真竖排：老实报错，不拿横排宽度凑数。
        raise RuntimeError("字体 %s 没有竖排度量（%s），真竖排夹具造不出来" %
                            (cjk.path, cjk.vertical_reason or "未知原因"))

    size = 22
    columns = [
        ("竖排第一列自上而下", 300),
        ("第二列继续往下排", 270),
        ("第三列收尾", 240),
    ]

    page_one = builder.add_page(Page())
    for text, x in columns:
        page_one.vertical_wmode(cjk, text, size, x, 700)

    page_two = builder.add_page(Page())
    page_two.vertical_wmode(cjk, "竖排里也有English和123", size, 300, 700)
    page_two.vertical_wmode(cjk, "标点，。！？；", size, 270, 700)

    page_one_text = "竖排第一列自上而下" + "第二列继续往下排" + "第三列收尾"
    page_two_text = "竖排里也有English和123" + "标点，。！？；"
    meta = _meta("vertical-wmode1", "竖排文字（WMode 1 真竖排）", 2,
                 ["竖排第一列自上而下", "第二列继续往下排", "第三列收尾",
                  "竖排里也有English和123", "标点，。！？；"],
                 "字体字典带 /WMode 1 + /DW2 + /W2（竖排度量取自字体的 vhea / vmtx），"
                 "内容流按阅读顺序写（列右→左、列内上→下），"
                 "所以抽取顺序与渲染顺序一致；这份与 vertical 那份（逐字定位的模拟竖排）"
                 "是对照组。",
                 exact_pages=[page_one_text, page_two_text],
                 wmode=1,
                 fonts=[os.path.basename(cjk.path)])
    return builder.build(), meta


FIXTURES = [
    ("rare-glyphs", fixture_rare_glyphs),
    ("cjk-punctuation", fixture_cjk_punctuation),
    ("fullwidth", fixture_fullwidth),
    ("combining", fixture_combining),
    ("mixed-script", fixture_mixed_script),
    ("vertical", fixture_vertical),
    ("vertical-wmode1", fixture_vertical_wmode1),
    ("long-paragraph", fixture_long_paragraph),
    ("huge-charset", fixture_huge_charset),
]

# 夹具的静态说明：生成失败时也要写进清单，
# 否则「少生成了一份」会变成静默的覆盖缺口，验收那边根本看不出来。
FIXTURE_INFO = {
    "rare-glyphs": "生僻字与扩展区汉字",
    "cjk-punctuation": "CJK 标点",
    "fullwidth": "全角字符",
    "combining": "基字 + 变音符的组合字符",
    "mixed-script": "中英混排",
    "vertical": "竖排文字（模拟竖排：逐字定位）",
    "vertical-wmode1": "竖排文字（WMode 1 真竖排）",
    "long-paragraph": "长段落",
    "huge-charset": "超大字符集（单页 3000 字）",
}


# ---------------------------------------------------------------------------
# PDFium 探针
# ---------------------------------------------------------------------------


class PdfiumError(RuntimeError):
    pass


class Pdfium:
    """ctypes 直连仓库固定的 PDFium DLL。

    只用到公开 API：打开、取页数、抽文字、渲染到位图。
    不自己解析 PDF，也不引入第三方 Python 包。
    """

    def __init__(self, dll_path):
        if not os.path.exists(dll_path):
            raise PdfiumError("找不到 PDFium：%s（先跑 scripts\\bootstrap-pdfium.ps1）" % dll_path)
        directory = os.path.dirname(os.path.abspath(dll_path))
        if hasattr(os, "add_dll_directory"):
            os.add_dll_directory(directory)
        self.dll = ctypes.CDLL(dll_path)
        self.dll_path = dll_path

        dll = self.dll
        dll.FPDF_InitLibrary.restype = None
        dll.FPDF_InitLibrary.argtypes = []
        dll.FPDF_DestroyLibrary.restype = None
        dll.FPDF_DestroyLibrary.argtypes = []
        dll.FPDF_LoadDocument.restype = ctypes.c_void_p
        dll.FPDF_LoadDocument.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
        dll.FPDF_CloseDocument.restype = None
        dll.FPDF_CloseDocument.argtypes = [ctypes.c_void_p]
        dll.FPDF_GetPageCount.restype = ctypes.c_int
        dll.FPDF_GetPageCount.argtypes = [ctypes.c_void_p]
        dll.FPDF_LoadPage.restype = ctypes.c_void_p
        dll.FPDF_LoadPage.argtypes = [ctypes.c_void_p, ctypes.c_int]
        dll.FPDF_ClosePage.restype = None
        dll.FPDF_ClosePage.argtypes = [ctypes.c_void_p]
        dll.FPDF_GetPageWidthF.restype = ctypes.c_float
        dll.FPDF_GetPageWidthF.argtypes = [ctypes.c_void_p]
        dll.FPDF_GetPageHeightF.restype = ctypes.c_float
        dll.FPDF_GetPageHeightF.argtypes = [ctypes.c_void_p]
        dll.FPDFBitmap_Create.restype = ctypes.c_void_p
        dll.FPDFBitmap_Create.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
        dll.FPDFBitmap_FillRect.restype = ctypes.c_int
        dll.FPDFBitmap_FillRect.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                                           ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
        dll.FPDFBitmap_GetBuffer.restype = ctypes.c_void_p
        dll.FPDFBitmap_GetBuffer.argtypes = [ctypes.c_void_p]
        dll.FPDFBitmap_GetStride.restype = ctypes.c_int
        dll.FPDFBitmap_GetStride.argtypes = [ctypes.c_void_p]
        dll.FPDFBitmap_Destroy.restype = None
        dll.FPDFBitmap_Destroy.argtypes = [ctypes.c_void_p]
        dll.FPDF_RenderPageBitmap.restype = None
        dll.FPDF_RenderPageBitmap.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int,
                                             ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                             ctypes.c_int, ctypes.c_int]
        dll.FPDFText_LoadPage.restype = ctypes.c_void_p
        dll.FPDFText_LoadPage.argtypes = [ctypes.c_void_p]
        dll.FPDFText_ClosePage.restype = None
        dll.FPDFText_ClosePage.argtypes = [ctypes.c_void_p]
        dll.FPDFText_CountChars.restype = ctypes.c_int
        dll.FPDFText_CountChars.argtypes = [ctypes.c_void_p]
        dll.FPDFText_GetText.restype = ctypes.c_int
        dll.FPDFText_GetText.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                                         ctypes.POINTER(ctypes.c_ushort)]
        dll.FPDF_GetLastError.restype = ctypes.c_ulong
        dll.FPDF_GetLastError.argtypes = []
        dll.FPDF_InitLibrary()

    def last_error(self):
        return int(self.dll.FPDF_GetLastError())

    def open(self, path):
        handle = self.dll.FPDF_LoadDocument(path.encode("utf-8"), None)
        if not handle:
            raise PdfiumError("FPDF_LoadDocument 失败（错误码 %d）：%s" %
                              (self.last_error(), path))
        return handle

    def page_count(self, document):
        return int(self.dll.FPDF_GetPageCount(document))

    def page_text(self, document, index):
        page = self.dll.FPDF_LoadPage(document, index)
        if not page:
            raise PdfiumError("FPDF_LoadPage 失败（第 %d 页）" % index)
        try:
            text_page = self.dll.FPDFText_LoadPage(page)
            if not text_page:
                raise PdfiumError("FPDFText_LoadPage 失败（第 %d 页）" % index)
            try:
                count = int(self.dll.FPDFText_CountChars(text_page))
                if count <= 0:
                    return ""
                buffer = (ctypes.c_ushort * (count + 1))()
                written = int(self.dll.FPDFText_GetText(text_page, 0, count, buffer))
                if written <= 0:
                    return ""
                raw = ctypes.string_at(ctypes.addressof(buffer), (written - 1) * 2)
                return raw.decode("utf-16-le", "replace")
            finally:
                self.dll.FPDFText_ClosePage(text_page)
        finally:
            self.dll.FPDF_ClosePage(page)

    def render(self, document, index, width):
        """把一页渲染成 BGRA 位图，返回原始像素。"""
        page = self.dll.FPDF_LoadPage(document, index)
        if not page:
            raise PdfiumError("FPDF_LoadPage 失败（第 %d 页）" % index)
        try:
            pdf_width = float(self.dll.FPDF_GetPageWidthF(page))
            pdf_height = float(self.dll.FPDF_GetPageHeightF(page))
            if pdf_width <= 0 or pdf_height <= 0:
                raise PdfiumError("第 %d 页尺寸非法" % index)
            height = max(1, int(round(width * pdf_height / pdf_width)))
            bitmap = self.dll.FPDFBitmap_Create(width, height, 0)
            if not bitmap:
                raise PdfiumError("FPDFBitmap_Create 失败（第 %d 页）" % index)
            try:
                self.dll.FPDFBitmap_FillRect(bitmap, 0, 0, width, height, 0xFFFFFFFF)
                self.dll.FPDF_RenderPageBitmap(bitmap, page, 0, 0, width, height, 0, 0x01)
                buffer = self.dll.FPDFBitmap_GetBuffer(bitmap)
                stride = int(self.dll.FPDFBitmap_GetStride(bitmap))
                pixels = ctypes.string_at(buffer, stride * height)
                return {"width": width, "height": height, "stride": stride, "pixels": pixels}
            finally:
                self.dll.FPDFBitmap_Destroy(bitmap)
        finally:
            self.dll.FPDF_ClosePage(page)

    def close(self, document):
        self.dll.FPDF_CloseDocument(document)

    def destroy(self):
        self.dll.FPDF_DestroyLibrary()


def compare_images(left, right):
    """逐像素比对两张位图，返回差异像素比例。

    尺寸不同直接算 100% 差异：页尺寸变了就已经不是同一份渲染结果。
    """
    if left["width"] != right["width"] or left["height"] != right["height"]:
        return 1.0
    total = left["width"] * left["height"]
    if total <= 0:
        return 1.0
    differing = 0
    for row in range(left["height"]):
        left_row = left["pixels"][row * left["stride"]:row * left["stride"] + left["width"] * 4]
        right_row = right["pixels"][row * right["stride"]:row * right["stride"] + right["width"] * 4]
        differing += _row_pixel_diff(left_row, right_row)
    return float(differing) / float(total)


def _row_pixel_diff(left_row, right_row):
    """一行里有多少个像素不同（BGRA 四通道）。整行先异或，再按字节位或统计。"""
    left_value = int.from_bytes(left_row, "big")
    right_value = int.from_bytes(right_row, "big")
    diff = (left_value ^ right_value).to_bytes(len(left_row), "big")
    count = len(diff) // 4
    if count == 0:
        return 0
    mask = (int.from_bytes(diff[0::4], "big") |
            int.from_bytes(diff[1::4], "big") |
            int.from_bytes(diff[2::4], "big") |
            int.from_bytes(diff[3::4], "big"))
    mask_bytes = mask.to_bytes(count, "big")
    return count - mask_bytes.count(0)


def probe(pdf_paths, dll_path=DEFAULT_PDFIUM, render_width=612, max_pages=0, do_render=True):
    """探测若干 PDF：页数、每页文字、渲染结果；给两份时再算渲染差异。"""
    result = {"engine": "pdfium", "dll": dll_path, "documents": [], "diff": None}
    pdfium = Pdfium(dll_path)
    try:
        rendered = []
        for path in pdf_paths:
            entry = {"path": path, "opened": False, "error": "", "pages": 0,
                     "page_texts": [], "text_chars": 0, "render": None}
            document = None
            try:
                document = pdfium.open(path)
                entry["opened"] = True
                count = pdfium.page_count(document)
                entry["pages"] = count
                limit = count if max_pages <= 0 else min(count, max_pages)
                texts = []
                for index in range(limit):
                    texts.append(pdfium.page_text(document, index))
                entry["page_texts"] = texts
                entry["text_chars"] = sum(len(text) for text in texts)
                if do_render:
                    images = []
                    ink_pages = []
                    blank_pages = []
                    for index in range(limit):
                        image = pdfium.render(document, index, render_width)
                        images.append(image)
                        non_white = sum(1 for byte in image["pixels"] if byte != 255)
                        if non_white > 0:
                            ink_pages.append(index)
                        else:
                            blank_pages.append(index)
                    entry["render"] = {"ok": True, "pages": limit,
                                       "ink_pages": ink_pages, "blank_pages": blank_pages,
                                       "width": render_width,
                                       "heights": [image["height"] for image in images]}
                    rendered.append(images)
                else:
                    rendered.append(None)
            except Exception as error:  # noqa: BLE001 - 探针要把失败原因如实报回去
                entry["error"] = str(error)
                rendered.append(None)
            finally:
                if document is not None:
                    pdfium.close(document)
            result["documents"].append(entry)

        if len(pdf_paths) >= 2 and rendered[0] and rendered[1]:
            left = rendered[0]
            right = rendered[1]
            ratios = []
            differing = []
            for index in range(min(len(left), len(right))):
                ratio = compare_images(left[index], right[index])
                ratios.append(ratio)
                if ratio > 0:
                    differing.append(index)
            page_count_mismatch = len(left) != len(right)
            result["diff"] = {
                "compared_pages": min(len(left), len(right)),
                "page_diff_ratio": ratios,
                "differing_pages": differing,
                "max_diff_ratio": max(ratios) if ratios else (1.0 if page_count_mismatch else 0.0),
                "page_count_mismatch": page_count_mismatch,
            }
    finally:
        pdfium.destroy()
    return result


# ---------------------------------------------------------------------------
# 命令行
# ---------------------------------------------------------------------------


def write_json(path, payload):
    directory = os.path.dirname(os.path.abspath(path))
    if directory and not os.path.isdir(directory):
        os.makedirs(directory, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")


def command_gen(args):
    out_dir = os.path.abspath(args.out)
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir, exist_ok=True)

    # 清单里不写路径：同一份语料换个目录生成，字节必须还是一样。
    manifest = {"tool": "gen-cjk-corpus.py", "fixtures": []}
    failures = 0
    for name, factory in FIXTURES:
        try:
            data, meta = factory()
        except Exception as error:  # noqa: BLE001 - 一个夹具失败不影响其余夹具
            failures += 1
            print("FAIL  %-18s %s" % (name, error), file=sys.stderr)
            meta = {
                "name": name,
                "file": "%s.pdf" % name,
                "feature": FIXTURE_INFO.get(name, name),
                "pages": 0,
                "ink_pages": [],
                "anchors": [],
                "fonts": [],
                "notes": "",
                "error": str(error),
            }
            manifest["fixtures"].append(meta)
            continue
        path = os.path.join(out_dir, meta["file"])
        with open(path, "wb") as handle:
            handle.write(data)
        write_json(os.path.join(out_dir, name + ".meta.json"), meta)
        manifest["fixtures"].append(meta)
        print("wrote %-18s %9d bytes  %s" % (meta["file"], len(data), meta["feature"]))

    write_json(os.path.join(out_dir, "manifest.json"), manifest)
    print("清单：%s" % os.path.join(out_dir, "manifest.json"))
    if failures:
        print("%d 份夹具生成失败（清单里已标出 error，验收会判红）" % failures, file=sys.stderr)
        return 1
    return 0


def command_probe(args):
    if not args.pdf:
        print("probe 至少要给一个 --pdf", file=sys.stderr)
        return 2
    result = probe(args.pdf, dll_path=args.pdfium, render_width=args.render_width,
                   max_pages=args.max_pages, do_render=not args.no_render)
    if args.json_out:
        write_json(args.json_out, result)
        print("探针结果：%s" % args.json_out)
    else:
        if hasattr(sys.stdout, "reconfigure"):
            sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        json.dump(result, sys.stdout, ensure_ascii=False, indent=2, sort_keys=True)
        sys.stdout.write("\n")
    return 0


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    parser = argparse.ArgumentParser(
        description="生成复杂 CJK 兼容语料，并提供共用的 PDFium 探针。")
    subparsers = parser.add_subparsers(dest="command")

    gen_parser = subparsers.add_parser("gen", help="生成 CJK 语料")
    gen_parser.add_argument("--out", default=DEFAULT_OUT, help="输出目录（默认 build/cjk-corpus）")
    gen_parser.set_defaults(func=command_gen)

    probe_parser = subparsers.add_parser("probe", help="用 PDFium 探测 PDF（页数/文字/渲染/差异）")
    probe_parser.add_argument("--pdf", action="append", default=[], help="PDF 路径，可重复给两份做比对")
    probe_parser.add_argument("--pdfium", default=DEFAULT_PDFIUM, help="pdfium.dll 路径")
    probe_parser.add_argument("--render-width", type=int, default=612, help="渲染宽度（像素）")
    probe_parser.add_argument("--max-pages", type=int, default=0, help="最多处理多少页（0 = 全部）")
    probe_parser.add_argument("--no-render", action="store_true", help="只抽文字，不渲染")
    probe_parser.add_argument("--json-out", default="", help="把结果写成 JSON 文件")
    probe_parser.set_defaults(func=command_probe)

    # 不带子命令（或只带参数）时默认生成语料。
    if not argv or argv[0] not in ("gen", "probe"):
        argv = ["gen"] + argv
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
