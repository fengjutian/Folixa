#!/usr/bin/env python3
"""生成 PDFium 适配层使用的测试 PDF 集合。

纯标准库实现：手工计算 xref 偏移，并自行实现 PDF 标准安全处理器
（RC4 40 位，R=2）来生成加密样例，不依赖任何第三方库。
"""

import argparse
import hashlib
import os
import struct
import sys
import zlib


def _build_pdf(objects, root_id, extra_trailer=b""):
    """把对象列表拼成带正确 xref 的 PDF。objects: [(id, body_bytes), ...]"""
    out = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
    offsets = {}
    for obj_id, body in objects:
        offsets[obj_id] = len(out)
        out += b"%d 0 obj\n" % obj_id
        out += body
        out += b"\nendobj\n"

    xref_offset = len(out)
    count = max(offsets) + 1
    out += b"xref\n0 %d\n" % count
    out += b"0000000000 65535 f \n"
    for obj_id in range(1, count):
        if obj_id in offsets:
            out += b"%010d 00000 n \n" % offsets[obj_id]
        else:
            out += b"0000000000 65535 f \n"
    out += b"trailer\n<< /Size %d /Root %d 0 R %s>>\n" % (count, root_id, extra_trailer)
    out += b"startxref\n%d\n%%%%EOF\n" % xref_offset
    return bytes(out)


def _content_stream(text, size=28):
    stream = b"BT /F1 %d Tf 24 60 Td (%s) Tj ET" % (size, text.encode("ascii"))
    return b"<< /Length %d >>\nstream\n%s\nendstream" % (len(stream), stream)


def _escape_pdf_text(text):
    """把文本里的 ( ) \\ 转义掉，供 PDF 字面量字符串使用。"""
    return text.replace("\\", r"\\").replace("(", r"\(").replace(")", r"\)")


def _page_content_stream(lines):
    """lines: [(text, size, x, y)]，y 是 PDF user space 的基线位置。"""
    parts = []
    for (text, size, x, y) in lines:
        parts.append(
            b"BT /F1 %d Tf %d %d Td (%s) Tj ET"
            % (size, x, y, _escape_pdf_text(text).encode("latin-1"))
        )
    return b"\n".join(parts)


def text_pdf():
    """带文本层的多页文档：用于选择、复制与全文搜索验收。

    第 1 页用大号字写 "Folixa"（拖选好命中），第 2 页再出现一次
    （搜索结果跨页，可以验证跳转）。
    """
    page_one = (
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 400] "
        b"/Resources << /Font << /F1 5 0 R >> >> /Contents 6 0 R >>"
    )
    page_two = (
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 400] "
        b"/Resources << /Font << /F1 5 0 R >> >> /Contents 7 0 R >>"
    )
    objects = [
        (1, b"<< /Type /Catalog /Pages 2 0 R >>"),
        (2, b"<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>"),
        (3, page_one),
        (4, page_two),
        (5, b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"),
    ]

    first = _page_content_stream([
        ("Folixa", 32, 60, 320),
        ("Full text search and selection demo document.", 14, 60, 270),
        ("The quick brown fox jumps over the lazy dog.", 14, 60, 240),
    ])
    second = _page_content_stream([
        ("Folixa appears on the second page too.", 18, 60, 320),
        ("Search hits are listed with a page snippet.", 14, 60, 280),
    ])
    objects.append((6, b"<< /Length %d >>\nstream\n%s\nendstream" % (len(first), first)))
    objects.append((7, b"<< /Length %d >>\nstream\n%s\nendstream" % (len(second), second)))
    return _build_pdf(objects, 1)


def normal_pdf():
    page = (
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] "
        b"/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>"
    )
    objects = [
        (1, b"<< /Type /Catalog /Pages 2 0 R >>"),
        (2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>"),
        (3, page),
        (4, b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"),
        (5, _content_stream("Hello PDFium")),
    ]
    return _build_pdf(objects, 1)


def empty_pdf():
    objects = [
        (1, b"<< /Type /Catalog /Pages 2 0 R >>"),
        (2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>"),
        (3, b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 120 160] "
            b"/Resources << >> >>"),
    ]
    return _build_pdf(objects, 1)


def rotated_pdf():
    page = (
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] /Rotate 90 "
        b"/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>"
    )
    objects = [
        (1, b"<< /Type /Catalog /Pages 2 0 R >>"),
        (2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>"),
        (3, page),
        (4, b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"),
        (5, _content_stream("Rotated")),
    ]
    return _build_pdf(objects, 1)


def multipage_pdf(page_count=12, width=300, height=400):
    """多页文档：每页有页码，用来验证连续滚动。"""
    font_id = 4
    first_page_id = 3
    kids = []
    objects = []

    for index in range(page_count):
        page_id = first_page_id + index * 2
        content_id = page_id + 1
        kids.append(b"%d 0 R" % page_id)
        objects.append((
            page_id,
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] "
            b"/Resources << /Font << /F1 %d 0 R >> >> /Contents %d 0 R >>"
            % (width, height, font_id, content_id),
        ))
        objects.append((
            content_id,
            _content_stream("Page %d" % (index + 1), size=32),
        ))

    objects.insert(0, (1, b"<< /Type /Catalog /Pages 2 0 R >>"))
    objects.insert(1, (
        2,
        b"<< /Type /Pages /Kids [%s] /Count %d >>" % (b" ".join(kids), page_count),
    ))
    objects.append((font_id, b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"))
    return _build_pdf(objects, 1)


def long_pdf(page_count=500, width=300, height=400):
    """长文档：用于压力测试连续浏览与缓存上限。"""
    return multipage_pdf(page_count=page_count, width=width, height=height)


def dense_pdf(page_count=600, width=612, height=792, lines_per_page=60, chars_per_line=100):
    """文字密集文档：让全文搜索跑得足够久。

    扫描成本按**字形数量**走，不是按页数走：3000 个只有几个字的小页，
    全文搜索只要 0.5 秒就扫完了，验收脚本根本来不及在搜索进行中点「停止」。
    换成每页几千字符的密集页面，几百页就能把搜索拉到好几秒。

    每页只有第一行含 "Page"：命中数与页数相同（1:1），
    结果列表才不会因为几十万个命中把界面压垮。
    """
    font_id = 4
    first_page_id = 3
    kids = []
    objects = []
    filler = "The quick brown fox jumps over the lazy dog and keeps running along. "

    for index in range(page_count):
        page_id = first_page_id + index * 2
        content_id = page_id + 1
        kids.append(b"%d 0 R" % page_id)
        objects.append((
            page_id,
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] "
            b"/Resources << /Font << /F1 %d 0 R >> >> /Contents %d 0 R >>"
            % (width, height, font_id, content_id),
        ))

        lines = []
        for line in range(lines_per_page):
            if line == 0:
                text = ("Page %d of %d: " % (index + 1, page_count)).ljust(chars_per_line)
            else:
                text = (filler * 3)[:chars_per_line]
            lines.append((text, 8, 40, height - 50 - line * 12))
        stream = _page_content_stream(lines)
        objects.append((
            content_id,
            b"<< /Length %d >>\nstream\n%s\nendstream" % (len(stream), stream),
        ))

    objects.insert(0, (1, b"<< /Type /Catalog /Pages 2 0 R >>"))
    objects.insert(1, (
        2,
        b"<< /Type /Pages /Kids [%s] /Count %d >>" % (b" ".join(kids), page_count),
    ))
    objects.append((font_id, b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"))
    return _build_pdf(objects, 1)


# --------------------------------------------------------------------------
# 中文夹具：Type0 / Identity-H 编码 + 内嵌 TrueType + ToUnicode CMap。
# --------------------------------------------------------------------------

_CJK_FONT_CANDIDATES = [
    r"C:\Windows\Fonts\simhei.ttf",
    r"C:\Windows\Fonts\Deng.ttf",
    r"C:\Windows\Fonts\simsun.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/System/Library/Fonts/PingFang.ttc",
]


def _find_cjk_font():
    for path in _CJK_FONT_CANDIDATES:
        if os.path.exists(path):
            return path
    return None


def _parse_cmap_format4(data, offset):
    """TrueType cmap 格式 4（BMP）：返回 {Unicode: GID}。"""
    seg_count = struct.unpack(">H", data[offset + 6:offset + 8])[0] // 2
    ends_at = offset + 14
    starts_at = ends_at + seg_count * 2 + 2
    deltas_at = starts_at + seg_count * 2
    ranges_at = deltas_at + seg_count * 2
    ends = struct.unpack(">%dH" % seg_count, data[ends_at:ends_at + seg_count * 2])
    starts = struct.unpack(">%dH" % seg_count, data[starts_at:starts_at + seg_count * 2])
    deltas = struct.unpack(">%dh" % seg_count, data[deltas_at:deltas_at + seg_count * 2])
    range_offsets = struct.unpack(">%dH" % seg_count, data[ranges_at:ranges_at + seg_count * 2])

    mapping = {}
    for index in range(seg_count):
        start, end = starts[index], ends[index]
        if start > end or start == 0xFFFF:
            continue
        for code in range(start, end + 1):
            if range_offsets[index] == 0:
                gid = (code + deltas[index]) & 0xFFFF
            else:
                position = ranges_at + index * 2 + range_offsets[index] + (code - start) * 2
                if position + 2 > len(data):
                    continue
                gid = struct.unpack(">H", data[position:position + 2])[0]
                if gid:
                    gid = (gid + deltas[index]) & 0xFFFF
            if gid:
                mapping[code] = gid
    return mapping


def _parse_cmap_format12(data, offset):
    """TrueType cmap 格式 12（完整 Unicode）：返回 {Unicode: GID}。"""
    groups = struct.unpack(">I", data[offset + 12:offset + 16])[0]
    mapping = {}
    position = offset + 16
    for _ in range(groups):
        start, end, gid = struct.unpack(">III", data[position:position + 12])
        position += 12
        if end - start > 0x10000:
            end = start + 0x10000
        for code in range(start, end + 1):
            mapping[code] = gid + (code - start)
    return mapping


def _ttf_unicode_to_gid(data):
    """解析 TrueType 的 cmap 表，返回 {Unicode 码点: 字形编号}。"""
    if len(data) < 12:
        return {}
    num_tables = struct.unpack(">H", data[4:6])[0]
    tables = {}
    for index in range(num_tables):
        entry = 12 + index * 16
        if entry + 16 > len(data):
            break
        tag = data[entry:entry + 4].decode("latin-1")
        table_offset, table_length = struct.unpack(">II", data[entry + 8:entry + 16])
        tables[tag] = (table_offset, table_length)
    if "cmap" not in tables:
        return {}

    cmap_offset = tables["cmap"][0]
    num_subtables = struct.unpack(">H", data[cmap_offset + 2:cmap_offset + 4])[0]
    best = None
    for index in range(num_subtables):
        record = cmap_offset + 4 + index * 8
        platform, encoding, sub_offset = struct.unpack(">HHI", data[record:record + 8])
        sub = cmap_offset + sub_offset
        if sub + 4 > len(data):
            continue
        fmt = struct.unpack(">H", data[sub:sub + 2])[0]
        score = {(3, 10): 5, (0, 4): 5, (0, 6): 5, (3, 1): 4, (0, 3): 3}.get((platform, encoding), 0)
        if score and (best is None or score > best[0]):
            best = (score, sub, fmt)
    if best is None:
        return {}
    _, sub, fmt = best
    if fmt == 4:
        return _parse_cmap_format4(data, sub)
    if fmt == 12:
        return _parse_cmap_format12(data, sub)
    return {}


def _cjk_content_stream(text, size, x, y, gids):
    codes = "".join("%04X" % gid for gid in gids)
    return ("BT /F1 %d Tf %d %d Td <%s> Tj ET" % (size, x, y, codes)).encode("ascii")


def cjk_pdf(font_path=None):
    """中文文档：Type0/Identity-H + 内嵌 TrueType + ToUnicode。

    没有中文字体时退化成「不内嵌字体」的版本：文本提取照样正确
    （提取只看 ToUnicode），只是页面渲染不出汉字。
    """
    if font_path is None:
        font_path = _find_cjk_font()
    font_data = None
    if font_path:
        with open(font_path, "rb") as handle:
            font_data = handle.read()

    lines_one = [
        ("中文选择测试", 28, 60, 320),
        ("全文搜索可以找到中文关键词", 16, 60, 270),
    ]
    lines_two = [
        ("第二页也包含中文内容", 20, 60, 320),
        ("关键词：中文", 16, 60, 270),
    ]

    gid_map = {}
    if font_data:
        gid_map = _ttf_unicode_to_gid(font_data)

    def codes_for(text):
        result = []
        for char in text:
            code = ord(char)
            gid = gid_map.get(code)
            if gid is None:
                # 字体里没有这个字：退回一个不会撞车的占位编号。
                gid = 0xF000 + (code & 0xFF)
            result.append(gid)
        return result

    all_codes = []
    for (text, _, _, _) in lines_one + lines_two:
        all_codes.extend(codes_for(text))

    # ToUnicode：GID → Unicode（同一个字只写一次）。
    bf_chars = []
    seen = set()
    for (text, _, _, _) in lines_one + lines_two:
        gids = codes_for(text)
        for index, char in enumerate(text):
            gid = gids[index]
            if gid in seen:
                continue
            seen.add(gid)
            bf_chars.append("<%04X> <%04X>" % (gid, ord(char)))
    bf_chars.sort()

    cmap = (
        "/CIDInit /ProcSet findresource begin\n"
        "12 dict begin\n"
        "begincmap\n"
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> def\n"
        "/CMapName /Adobe-Identity-UCS def\n"
        "/CMapType 2 def\n"
        "1 begincodespacerange\n"
        "<0000> <FFFF>\n"
        "endcodespacerange\n"
        "%d beginbfchar\n"
        "%s\n"
        "endbfchar\n"
        "endcmap\n"
        "CMapName currentdict /CMap defineresource pop\n"
        "end\n"
        "end\n"
    ) % (len(bf_chars), "\n".join(bf_chars))

    content_one = b"\n".join(
        _cjk_content_stream(text, size, x, y, codes_for(text))
        for (text, size, x, y) in lines_one
    )
    content_two = b"\n".join(
        _cjk_content_stream(text, size, x, y, codes_for(text))
        for (text, size, x, y) in lines_two
    )

    objects = [
        (1, b"<< /Type /Catalog /Pages 2 0 R >>"),
        (2, b"<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>"),
        (3, b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 400] "
           b"/Resources << /Font << /F1 5 0 R >> >> /Contents 7 0 R >>"),
        (4, b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 400] "
           b"/Resources << /Font << /F1 5 0 R >> >> /Contents 8 0 R >>"),
        (5, b"<< /Type /Font /Subtype /Type0 /BaseFont /SimHei /Encoding /Identity-H "
           b"/DescendantFonts [6 0 R] /ToUnicode 9 0 R >>"),
        (6, b"<< /Type /Font /Subtype /CIDFontType2 /BaseFont /SimHei "
           b"/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
           b"/FontDescriptor 10 0 R /DW 1000 >>"),
        (7, b"<< /Length %d >>\nstream\n%s\nendstream" % (len(content_one), content_one)),
        (8, b"<< /Length %d >>\nstream\n%s\nendstream" % (len(content_two), content_two)),
        (9, b"<< /Length %d >>\nstream\n%s\nendstream" % (len(cmap), cmap.encode("ascii"))),
        (10, b"<< /Type /FontDescriptor /FontName /SimHei /Flags 4 "
            b"/FontBBox [-200 -300 1200 900] /ItalicAngle 0 /Ascent 880 "
            b"/Descent -120 /CapHeight 880 /StemV 80 >>"),
    ]
    if font_data:
        compressed = zlib.compress(font_data, 6)
        objects.append(
            (11, b"<< /Length %d /Filter /FlateDecode >>\nstream\n%s\nendstream"
                % (len(compressed), compressed))
        )
        # 内嵌字体后，CIDFont 要指向 FontFile2。
        objects[5] = (
            6, b"<< /Type /Font /Subtype /CIDFontType2 /BaseFont /SimHei "
               b"/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
               b"/FontDescriptor 10 0 R /DW 1000 /FontFile2 11 0 R >>"
        )
    return _build_pdf(objects, 1)


def corrupt_pdf():
    """结构被破坏的 PDF：文件头正常，但对象内容是垃圾。"""
    body = b"%PDF-1.7\n" + os.urandom(0) + b"this is not a pdf body at all\n"
    return body + b"%%EOF\n"


def truncated_pdf():
    """合法 PDF 的前半段：xref 与 trailer 都被截断。"""
    return normal_pdf()[:120]


# --------------------------------------------------------------------------
# PDF 标准安全处理器（RC4，R=2，40 位），用于生成需要密码的样例。
# 参考 PDF 1.7 规范 Algorithm 1/2/3/4/5/6。
# --------------------------------------------------------------------------

_PAD = bytes(
    [
        0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41,
        0x64, 0x00, 0x4E, 0x56, 0xFF, 0xFA, 0x01, 0x08,
        0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68, 0x3E, 0x80,
        0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A,
    ]
)


def _rc4(key, data):
    state = list(range(256))
    j = 0
    for i in range(256):
        j = (j + state[i] + key[i % len(key)]) & 0xFF
        state[i], state[j] = state[j], state[i]
    out = bytearray()
    i = j = 0
    for byte in data:
        i = (i + 1) & 0xFF
        j = (j + state[i]) & 0xFF
        state[i], state[j] = state[j], state[i]
        out.append(byte ^ state[(state[i] + state[j]) & 0xFF])
    return bytes(out)


def _pad_password(password):
    raw = password.encode("latin-1")[:32]
    return raw + _PAD[: 32 - len(raw)]


def _literal_string(data):
    escaped = data.replace(b"\\", b"\\\\").replace(b"(", b"\\(").replace(b")", b"\\)")
    return b"(" + escaped + b")"


def encrypted_pdf(user_password="secret", owner_password="owner", file_id=b"0123456789ABCDEF"):
    """生成需要用户密码才能打开的 PDF。"""
    permissions = -1
    padded_user = _pad_password(user_password)
    padded_owner = _pad_password(owner_password)

    # O 条目：用所有者密码的 MD5 前 5 字节做密钥，RC4 加密用户口令。
    owner_key = hashlib.md5(padded_owner).digest()[:5]
    owner_entry = _rc4(owner_key, padded_user)

    # 加密密钥：MD5(填充用户口令 + O + P + 文件 ID) 的前 5 字节。
    digest = hashlib.md5()
    digest.update(padded_user)
    digest.update(owner_entry)
    digest.update(struct.pack("<i", permissions))
    digest.update(file_id)
    key = digest.digest()[:5]

    # U 条目：RC4 加密标准填充串。
    user_entry = _rc4(key, _PAD)

    def object_key(obj_id, gen_id):
        extra = struct.pack("<I", obj_id)[:3] + struct.pack("<I", gen_id)[:2]
        return hashlib.md5(key + extra).digest()[: min(len(key) + 5, 16)]

    content = b"BT /F1 24 Tf 24 60 Td (Encrypted) Tj ET"
    encrypted_content = _rc4(object_key(5, 0), content)

    objects = [
        (1, b"<< /Type /Catalog /Pages 2 0 R >>"),
        (2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>"),
        (3, b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] "
            b"/Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>"),
        (4, b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"),
        (5, b"<< /Length %d >>\nstream\n%s\nendstream" % (len(encrypted_content), encrypted_content)),
        (6, b"<< /Filter /Standard /V 1 /R 2 /O %s /U %s /P %d >>"
            % (_literal_string(owner_entry), _literal_string(user_entry), permissions)),
    ]

    id_hex = file_id.hex().encode("ascii")
    extra_trailer = b"/Encrypt 6 0 R /ID [<%s> <%s>] " % (id_hex, id_hex)
    return _build_pdf(objects, 1, extra_trailer)


def _utf16_hex(text):
    """中文标题用 UTF-16BE 十六进制串（带 BOM）。

    PDF 字面量字符串默认是 PDFDocEncoding，直接写 UTF-8 字节会被
    当成乱码；带 BOM 的 UTF-16BE 十六进制串才是中文的正确写法。
    """
    return b"<" + (b"\xfe\xff" + text.encode("utf-16-be")).hex().upper().encode("ascii") + b">"


def navigation_pdf():
    """带大纲与链接注释的文档：用于目录、书签与链接跳转验收。

    结构刻意覆盖几种真实写法：

      - 三个顶层/子级大纲条目：顶层用 /Dest 指向目标页，
        子级用 /A 动作（GoTo）——两种写法都要能解析出目标页。
      - 第 1 页：一个内部链接（GoTo 到第 2 页）。
      - 第 2 页：一个外部链接（URI）。
      - 第 3 页：两个**完全重叠**的链接，后枚举的那个（URI）必须胜出
        （PDFium 按 Z 序枚举，越靠后越在上层）。

    标题用中文（UTF-16BE），顺带验证解码路径。
    """
    page_one = (
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 400] "
        b"/Resources << /Font << /F1 6 0 R >> >> /Contents 7 0 R /Annots [10 0 R] >>"
    )
    page_two = (
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 400] "
        b"/Resources << /Font << /F1 6 0 R >> >> /Contents 8 0 R /Annots [11 0 R] >>"
    )
    page_three = (
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 400] "
        b"/Resources << /Font << /F1 6 0 R >> >> /Contents 9 0 R /Annots [12 0 R 13 0 R] >>"
    )

    objects = [
        (1, b"<< /Type /Catalog /Pages 2 0 R /Outlines 14 0 R >>"),
        (2, b"<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 >>"),
        (3, page_one),
        (4, page_two),
        (5, page_three),
        (6, b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"),
    ]

    first = _page_content_stream([
        ("Navigation demo", 28, 60, 330),
        ("The blue link below jumps to page two.", 14, 60, 260),
        ("Jump to page two", 16, 60, 212),
    ])
    second = _page_content_stream([
        ("Page two", 28, 60, 330),
        ("The link below opens a website.", 14, 60, 260),
        ("External link target", 16, 60, 160),
    ])
    third = _page_content_stream([
        ("Page three", 28, 60, 330),
        ("Two links overlap here, the topmost one wins.", 14, 60, 260),
        ("Overlapping links", 16, 60, 112),
    ])
    objects.append((7, b"<< /Length %d >>\nstream\n%s\nendstream" % (len(first), first)))
    objects.append((8, b"<< /Length %d >>\nstream\n%s\nendstream" % (len(second), second)))
    objects.append((9, b"<< /Length %d >>\nstream\n%s\nendstream" % (len(third), third)))

    # 链接注释：/Rect 是 PDF user space（左下原点）。
    objects.append((10, b"<< /Type /Annot /Subtype /Link /Rect [60 200 220 235] "
                      b"/Border [0 0 0] /A << /S /GoTo /D [4 0 R /XYZ null null null] >> >>"))
    objects.append((11, b"<< /Type /Annot /Subtype /Link /Rect [60 150 300 180] "
                      b"/Border [0 0 0] /A << /S /URI /URI (https://example.com/folixa) >> >>"))
    objects.append((12, b"<< /Type /Annot /Subtype /Link /Rect [60 100 300 140] "
                      b"/Border [0 0 0] /A << /S /GoTo /D [3 0 R /XYZ null null null] >> >>"))
    objects.append((13, b"<< /Type /Annot /Subtype /Link /Rect [60 100 300 140] "
                      b"/Border [0 0 0] /A << /S /URI /URI (https://example.com/overlay) >> >>"))

    # 大纲：两个顶层条目，其中一个带两个子条目。
    objects.append((14, b"<< /Type /Outlines /First 15 0 R /Last 18 0 R /Count 3 >>"))
    objects.append((15, b"<< /Title " + _utf16_hex("第一章 概述") +
                      b" /Parent 14 0 R /Next 18 0 R /First 16 0 R /Last 17 0 R /Count 2 "
                      b"/Dest [3 0 R /XYZ null null null] >>"))
    objects.append((16, b"<< /Title " + _utf16_hex("第一节 背景") +
                      b" /Parent 15 0 R /Next 17 0 R "
                      b"/A << /S /GoTo /D [4 0 R /XYZ null null null] >> >>"))
    objects.append((17, b"<< /Title " + _utf16_hex("第二节 方法") +
                      b" /Parent 15 0 R /Prev 16 0 R "
                      b"/A << /S /GoTo /D [5 0 R /XYZ null null null] >> >>"))
    objects.append((18, b"<< /Title " + _utf16_hex("第二章 结果") +
                      b" /Parent 14 0 R /Prev 15 0 R "
                      b"/A << /S /GoTo /D [5 0 R /XYZ null null null] >> >>"))
    return _build_pdf(objects, 1)


FIXTURES = {
    "normal.pdf": normal_pdf,
    "empty.pdf": empty_pdf,
    "rotated.pdf": rotated_pdf,
    "corrupt.pdf": corrupt_pdf,
    "truncated.pdf": truncated_pdf,
    "encrypted.pdf": encrypted_pdf,
    "multipage.pdf": multipage_pdf,
    "long.pdf": long_pdf,
    "text.pdf": text_pdf,
    "cjk.pdf": cjk_pdf,
    "navigation.pdf": navigation_pdf,
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", help="输出目录")
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--dense-pages", type=int, default=0,
                        help="额外生成 dense.pdf（页数）。默认不生成：别的验收脚本"
                             "用不上，生成它纯属浪费磁盘与时间。")
    parser.add_argument("--dense-lines", type=int, default=120,
                        help="dense.pdf 每页的行数（行数越多，搜索越慢）。")
    args = parser.parse_args()

    os.makedirs(args.output, exist_ok=True)
    for name, factory in FIXTURES.items():
        data = factory()
        path = os.path.join(args.output, name)
        with open(path, "wb") as handle:
            handle.write(data)
        if not args.quiet:
            print("wrote %-16s %6d bytes" % (name, len(data)))

    if args.dense_pages > 0:
        data = dense_pdf(page_count=args.dense_pages, lines_per_page=args.dense_lines)
        path = os.path.join(args.output, "dense.pdf")
        with open(path, "wb") as handle:
            handle.write(data)
        if not args.quiet:
            print("wrote %-16s %6d bytes" % ("dense.pdf", len(data)))

    return 0


if __name__ == "__main__":
    sys.exit(main())
