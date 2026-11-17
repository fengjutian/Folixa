#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 K4 / K5 / K6 用的 PDF 语料。

三类语料：

  1. 畸形 / 损坏语料（K5，默认 1000 份）：成体系地覆盖 10 类真实世界的坏法
     （截断、xref 损坏、对象头损坏、页面树损坏、字典语法错误、流长度不符、
     资源损坏、加密字典残缺、空壳文件、递归页面树）。
     每份旁边写一个同名 .meta.json，记录损坏类别、期望行为与生成参数。
  2. 大文件语料（K4）：`--with-large` 才生成，默认 1 GiB，流式写，内存恒定。
  3. 500 页文档（K6）：每页一行文本 + 一张真图。

只用 Python 标准库，不装第三方包。同一条命令跑两次，产物逐字节一致
（随机种子固定，像素来自 SHAKE-128 伪随机流，文件里不写时间戳）。

用法：
    python scripts/gen-compat-corpus.py --out build/corpus --count 1000
    python scripts/gen-compat-corpus.py --out build/corpus --count 1000 --with-large
"""

import argparse
import hashlib
import json
import os
import random
import re
import sys

DEFAULT_SEED = 20261116
DEFAULT_COUNT = 1000
DEFAULT_PAGES = 500
GIB = 1024 * 1024 * 1024

# 期望行为的三种取值：
#   open   —— 应当仍能打开（PDFium 的容错修复应当生效）
#   reject —— 应当被拒绝，并给出可读的中文原因
#   either —— 两种都算合规；硬要求只有「不崩 + 有可读结果」
EXPECT_OPEN = "open"
EXPECT_REJECT = "reject"
EXPECT_EITHER = "either"

EXPECT_ZH = {
    EXPECT_OPEN: "应当仍能打开",
    EXPECT_REJECT: "应当被拒绝并给出可读原因",
    EXPECT_EITHER: "打开或拒绝都算合规（硬要求：不崩 + 有可读结果）",
}


# ---------------------------------------------------------------------------
# PDF 组装
# ---------------------------------------------------------------------------


class PdfDocument:
    """一份可以反复序列化的 PDF：对象号 -> 对象体。

    语料生成器要先「造结构」再「破坏」，所以把文档建成对象表，
    破坏函数改这张表，最后统一序列化。
    """

    def __init__(self, root_id=1):
        self.objects = {}
        self.root_id = root_id
        self.trailer_extra = b""

    def add(self, obj_id, body):
        self.objects[obj_id] = body
        return obj_id

    def next_id(self):
        return max(self.objects) + 1

    def serialize(self):
        """输出完整 PDF（带 xref 与 trailer）。"""
        out = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
        offsets = {}
        for obj_id in sorted(self.objects):
            offsets[obj_id] = len(out)
            out += b"%d 0 obj\n" % obj_id
            out += self.objects[obj_id]
            out += b"\nendobj\n"

        count = max(offsets) + 1
        xref_offset = len(out)
        out += b"xref\n0 %d\n" % count
        out += b"0000000000 65535 f \n"
        for obj_id in range(1, count):
            if obj_id in offsets:
                out += b"%010d 00000 n \n" % offsets[obj_id]
            else:
                out += b"0000000000 65535 f \n"
        out += b"trailer\n<< /Size %d /Root %d 0 R %s>>\n" % (
            count,
            self.root_id,
            self.trailer_extra,
        )
        out += b"startxref\n%d\n%%%%EOF\n" % xref_offset
        return bytes(out)


def _escape_text(text):
    """把文本里的 ( ) \\ 转义掉，供 PDF 字面量字符串使用。"""
    return text.replace("\\", r"\\").replace("(", r"\(").replace(")", r"\)")


def _base_document(seed, index):
    """构造一份小而有结构的合法 PDF：2~5 页，每页一行文本 + 一张 8x8 的图。

    每份语料的底稿都不一样（页数、文本、图像噪声都跟着 seed + 序号走），
    这样 1000 份语料彼此不同，覆盖到的字节组合也不一样。
    """
    rng = random.Random("base:%d:%d" % (seed, index))
    page_count = rng.choice([2, 3, 4, 5])
    side = 8

    doc = PdfDocument(root_id=1)
    doc.add(1, b"<< /Type /Catalog /Pages 2 0 R >>")

    kids = []
    for i in range(page_count):
        page_id = 3 + 3 * i
        content_id = page_id + 1
        image_id = page_id + 2
        font_id = 3 + 3 * page_count
        kids.append(b"%d 0 R" % page_id)
        doc.add(page_id, (
            b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
            b"/Resources << /Font << /F1 %d 0 R >> /XObject << /Im0 %d 0 R >> >> "
            b"/Contents %d 0 R >>" % (font_id, image_id, content_id)))
        text = "Page %d of %d" % (i + 1, page_count)
        content = (
            b"q 160 0 0 160 60 320 cm /Im0 Do Q\n"
            b"BT /F1 18 Tf 60 260 Td (%s) Tj ET" % _escape_text(text).encode("ascii"))
        doc.add(content_id,
                b"<< /Length %d >>\nstream\n%s\nendstream" % (len(content), content))
        pixels = rng.randbytes(side * side * 3)
        doc.add(image_id, (
            b"<< /Type /XObject /Subtype /Image /Width %d /Height %d "
            b"/ColorSpace /DeviceRGB /BitsPerComponent 8 /Length %d >>\n"
            b"stream\n%s\nendstream" % (side, side, len(pixels), pixels)))

    doc.add(2, b"<< /Type /Pages /Kids [%s] /Count %d >>" % (b" ".join(kids), page_count))
    doc.add(3 + 3 * page_count,
            b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    return doc


# ---------------------------------------------------------------------------
# 破坏手法：每一种对应一类真实世界会遇到的坏文件
# ---------------------------------------------------------------------------


def _truncate(doc, ratio):
    """在指定比例处砍断（连 xref 与 trailer 一起丢）。"""
    data = doc.serialize()
    return data[: max(1, int(len(data) * ratio))]


def _xref_offset_wrong(doc, rng):
    """startxref 指向的偏移改错（差一个固定值）。"""
    data = doc.serialize()
    match = re.search(rb"startxref\n(\d+)\n", data)
    if not match:
        return data
    wrong = int(match.group(1)) + 13
    return data[: match.start(1)] + b"%d" % wrong + data[match.end(1):]


def _xref_missing(doc, rng):
    """整段 startxref 被删掉（老阅读器会直接放弃）。"""
    data = doc.serialize()
    return re.sub(rb"startxref\n\d+\n%%EOF\n", b"%%EOF\n", data, count=1)


def _xref_nonnumeric(doc, rng):
    """startxref 填非数字。"""
    data = doc.serialize()
    return re.sub(rb"startxref\n\d+\n", b"startxref\nnot-a-number\n", data, count=1)


def _xref_entry_wrong(doc, rng):
    """xref 表里某个对象的偏移改错（数字被换掉）。"""
    data = doc.serialize()
    head, sep, tail = data.partition(b"\nxref\n")
    if not sep:
        return data
    lines = tail.split(b"\n")
    # lines[0] = "0 N"，lines[1] = 空闲项，lines[2] 才是 1 号对象的条目。
    if len(lines) < 3:
        return data
    entry = lines[2]
    match = re.match(rb"(\d{10}) (\d{5}) ([nf])", entry)
    if not match:
        return data
    offset = int(match.group(1))
    broken = b"%010d %s %s" % (offset + 7, match.group(2), match.group(3))
    lines[2] = broken + entry[len(match.group(0)):]
    return head + sep + b"\n".join(lines)


def _obj_header_case(doc, rng):
    """对象头大小写被改坏：`N 0 obj` 变成 `N 0 oBj`。"""
    data = doc.serialize()
    return re.sub(rb"(\d+) 0 obj\n", rb"\1 0 oBj\n", data, count=1)


def _obj_no_endobj(doc, rng):
    """少了 endobj。"""
    data = doc.serialize()
    return data.replace(b"\nendobj\n", b"\n", 1)


def _obj_bad_generation(doc, rng):
    """对象号后面跟了一个错误的代数（`3 0 obj` 变成 `3 7 obj`）。"""
    data = doc.serialize()
    return re.sub(rb"(\d+) 0 obj\n", rb"\1 7 obj\n", data, count=1)


def _pagetree_kids_missing(doc, rng):
    """页面树 /Kids 指向不存在的对象号。"""
    body = doc.objects[2]
    kids = re.search(rb"/Kids \[([^\]]*)\]", body)
    doc.objects[2] = body[: kids.start(1)] + b"3 0 R 9999 0 R" + body[kids.end(1):]
    return doc.serialize()


def _pagetree_count_mismatch(doc, rng):
    """页面树 /Count 与实际页数不符。"""
    body = doc.objects[2]
    count = int(re.search(rb"/Count (\d+)", body).group(1))
    doc.objects[2] = body.replace(b"/Count %d" % count, b"/Count %d" % (count + 6))
    return doc.serialize()


def _pagetree_self_cycle(doc, rng):
    """页面树 /Kids 指向自己（自指环）。"""
    body = doc.objects[2]
    doc.objects[2] = re.sub(rb"/Kids \[[^\]]*\]", b"/Kids [2 0 R]", body)
    return doc.serialize()


def _pagetree_nested_cycle(doc, rng):
    """嵌套页面树：两个 Pages 节点互相指向（两节点环）。"""
    extra_id = doc.next_id()
    doc.add(extra_id, b"<< /Type /Pages /Kids [2 0 R] /Count 1 >>")
    body = doc.objects[2]
    doc.objects[2] = re.sub(rb"/Kids \[[^\]]*\]", b"/Kids [%d 0 R]" % extra_id, body)
    return doc.serialize()


def _dict_missing_close(doc, rng):
    """字典少一个 `>>`。"""
    doc.objects[1] = b"<< /Type /Catalog /Pages 2 0 R"
    return doc.serialize()


def _dict_value_not_object(doc, rng):
    """字典的值不是间接对象（写成了裸名字）。"""
    doc.objects[1] = b"<< /Type /Catalog /Pages /NotAnIndirectObject >>"
    return doc.serialize()


def _dict_invalid_key(doc, rng):
    """键名不是合法 token（`%` 不是 PDF 名里允许的字符）。"""
    doc.objects[1] = b"<< /Type /Catalog /Pages 2 0 R /Bad%Key 1 >>"
    return doc.serialize()


def _stream_length_mismatch(doc, rng):
    """流长度 /Length 与实际字节数不符。"""
    for obj_id, body in list(doc.objects.items()):
        match = re.search(rb"/Length (\d+)", body)
        if match:
            doc.objects[obj_id] = (body[: match.start(1)]
                                   + b"%d" % (int(match.group(1)) + 4096)
                                   + body[match.end(1):])
            break
    return doc.serialize()


def _stream_no_endstream(doc, rng):
    """流没有 endstream。"""
    for obj_id, body in list(doc.objects.items()):
        if b"stream\n" in body:
            doc.objects[obj_id] = body.replace(b"\nendstream", b"", 1)
            break
    return doc.serialize()


def _stream_garbage(doc, rng):
    """流内容塞垃圾字节（长度对得上，内容是乱的）。"""
    for obj_id, body in list(doc.objects.items()):
        match = re.search(rb"stream\n(.*)\nendstream", body, re.S)
        if match:
            payload = rng.randbytes(len(match.group(1)))
            doc.objects[obj_id] = body[: match.start(1)] + payload + body[match.end(1):]
            break
    return doc.serialize()


def _resource_font_missing(doc, rng):
    """/Font 指向不存在的字体对象。"""
    for obj_id, body in list(doc.objects.items()):
        if b"/Type /Page " in body:
            doc.objects[obj_id] = re.sub(rb"/F1 (\d+) 0 R", b"/F1 4242 0 R", body, count=1)
            break
    return doc.serialize()


def _image_width_zero(doc, rng):
    """图像 /Width 为 0。"""
    for obj_id, body in list(doc.objects.items()):
        if b"/Subtype /Image" in body:
            doc.objects[obj_id] = re.sub(rb"/Width \d+", b"/Width 0", body, count=1)
            break
    return doc.serialize()


def _image_width_huge(doc, rng):
    """图像 /Width 极大（渲染时会爆内存）。"""
    for obj_id, body in list(doc.objects.items()):
        if b"/Subtype /Image" in body:
            doc.objects[obj_id] = re.sub(rb"/Width \d+", b"/Width 100000", body, count=1)
            break
    return doc.serialize()


def _encrypt_broken(doc, rng):
    """有 /Encrypt，但加密字典残缺（缺 /O、/U）。"""
    extra_id = doc.next_id()
    doc.add(extra_id, b"<< /Filter /Standard /V 2 /R 3 /Length 128 >>")
    doc.trailer_extra = b"/Encrypt %d 0 R " % extra_id
    return doc.serialize()


def _empty_file(doc, rng):
    """0 字节。"""
    return b""


def _whitespace_only(doc, rng):
    """只有空白。"""
    return b"   \n\t\r\n  \n"


def _eof_only(doc, rng):
    """只有 %%EOF。"""
    return b"%%EOF\n"


def _header_only(doc, rng):
    """只有文件头。"""
    return b"%PDF-1.7\n"


def _generate_baseline(out_dir, seed, quiet):
    """写一份**完全合法**的对照样本。

    没有对照样本就说不清「坏文件被拒绝」是应用的容错做得好，
    还是这份文件本来就该被拒。对照样本必须能打开。
    """
    os.makedirs(out_dir, exist_ok=True)
    doc = _base_document(seed, 0)
    data = doc.serialize()
    name = "baseline-valid.pdf"
    with open(os.path.join(out_dir, name), "wb") as handle:
        handle.write(data)
    meta = {
        "name": "baseline-valid",
        "file": name,
        "category": "baseline",
        "category_zh": "对照样本（未损坏）",
        "variant": "baseline",
        "description": "与畸形语料同源的合法 PDF，用来对照：这份必须能打开",
        "expectation": EXPECT_OPEN,
        "expectation_zh": EXPECT_ZH[EXPECT_OPEN],
        "bytes": len(data),
        "sha256": _sha256(data),
        "generator": {"tool": "gen-compat-corpus.py", "seed": seed, "count": 0, "index": 0},
    }
    _write_json(os.path.join(out_dir, name + ".meta.json"), meta)
    if not quiet:
        print("生成对照样本：%s（%d 字节）" % (os.path.join(out_dir, name), len(data)))
    return {"file": name, "bytes": len(data), "sha256": meta["sha256"]}


def _variants():
    """损坏类别清单：10 大类、30 种具体写法。

    期望行为（expect）是「建议」——硬要求对所有样本都一样：
    进程不能崩，打不开时必须给出可读的中文原因。
    """
    return [
        # (1) 截断
        {"name": "truncate-01", "category": "truncate", "category_zh": "截断",
         "expect": EXPECT_REJECT, "describe": "在 1% 处截断，只剩文件头",
         "mutate": lambda doc, rng: _truncate(doc, 0.01)},
        {"name": "truncate-05", "category": "truncate", "category_zh": "截断",
         "expect": EXPECT_EITHER, "describe": "在 5% 处截断",
         "mutate": lambda doc, rng: _truncate(doc, 0.05)},
        {"name": "truncate-20", "category": "truncate", "category_zh": "截断",
         "expect": EXPECT_EITHER, "describe": "在 20% 处截断",
         "mutate": lambda doc, rng: _truncate(doc, 0.20)},
        {"name": "truncate-50", "category": "truncate", "category_zh": "截断",
         "expect": EXPECT_EITHER, "describe": "在 50% 处截断",
         "mutate": lambda doc, rng: _truncate(doc, 0.50)},
        {"name": "truncate-90", "category": "truncate", "category_zh": "截断",
         "expect": EXPECT_OPEN, "describe": "在 90% 处截断，只丢尾部",
         "mutate": lambda doc, rng: _truncate(doc, 0.90)},
        # (2) xref 损坏
        {"name": "xref-offset", "category": "xref", "category_zh": "xref 损坏",
         "expect": EXPECT_EITHER, "describe": "startxref 的偏移量改错",
         "mutate": _xref_offset_wrong},
        {"name": "xref-missing", "category": "xref", "category_zh": "xref 损坏",
         "expect": EXPECT_EITHER, "describe": "整段 startxref 被删掉",
         "mutate": _xref_missing},
        {"name": "xref-nonnumeric", "category": "xref", "category_zh": "xref 损坏",
         "expect": EXPECT_EITHER, "describe": "startxref 填非数字",
         "mutate": _xref_nonnumeric},
        {"name": "xref-entry-wrong", "category": "xref", "category_zh": "xref 损坏",
         "expect": EXPECT_EITHER, "describe": "xref 表里某个对象的偏移改错",
         "mutate": _xref_entry_wrong},
        # (3) 对象头损坏
        {"name": "obj-header-case", "category": "object-header", "category_zh": "对象头损坏",
         "expect": EXPECT_EITHER, "describe": "对象头 `N 0 obj` 变成 `N 0 oBj`",
         "mutate": _obj_header_case},
        {"name": "obj-no-endobj", "category": "object-header", "category_zh": "对象头损坏",
         "expect": EXPECT_EITHER, "describe": "少了 endobj",
         "mutate": _obj_no_endobj},
        {"name": "obj-bad-generation", "category": "object-header", "category_zh": "对象头损坏",
         "expect": EXPECT_EITHER, "describe": "对象号后面跟了错误的代数",
         "mutate": _obj_bad_generation},
        # (4) 页面树损坏
        {"name": "pagetree-kids-missing", "category": "page-tree",
         "category_zh": "页面树损坏",
         "expect": EXPECT_REJECT, "describe": "/Kids 指向不存在的对象号",
         "mutate": _pagetree_kids_missing},
        {"name": "pagetree-count-mismatch", "category": "page-tree",
         "category_zh": "页面树损坏",
         "expect": EXPECT_EITHER, "describe": "/Count 与实际页数不符",
         "mutate": _pagetree_count_mismatch},
        {"name": "pagetree-self-cycle", "category": "page-tree",
         "category_zh": "页面树损坏",
         "expect": EXPECT_REJECT, "describe": "/Kids 指向自己（自指环）",
         "mutate": _pagetree_self_cycle},
        # (10) 嵌套 / 递归结构
        {"name": "pagetree-nested-cycle", "category": "nested-structure",
         "category_zh": "嵌套/递归结构",
         "expect": EXPECT_REJECT, "describe": "两个 Pages 节点互相指向（两节点环）",
         "mutate": _pagetree_nested_cycle},
        # (5) 字典语法错误
        {"name": "dict-missing-close", "category": "dictionary", "category_zh": "字典语法错误",
         "expect": EXPECT_EITHER, "describe": "字典少一个 >>",
         "mutate": _dict_missing_close},
        {"name": "dict-value-not-object", "category": "dictionary", "category_zh": "字典语法错误",
         "expect": EXPECT_EITHER, "describe": "字典的值不是间接对象",
         "mutate": _dict_value_not_object},
        {"name": "dict-invalid-key", "category": "dictionary", "category_zh": "字典语法错误",
         "expect": EXPECT_EITHER, "describe": "键名不是合法 token",
         "mutate": _dict_invalid_key},
        # (6) 流长度不符
        {"name": "stream-length-mismatch", "category": "stream", "category_zh": "流长度不符",
         "expect": EXPECT_EITHER, "describe": "/Length 与实际流长度不符",
         "mutate": _stream_length_mismatch},
        {"name": "stream-no-endstream", "category": "stream", "category_zh": "流长度不符",
         "expect": EXPECT_EITHER, "describe": "流没有 endstream",
         "mutate": _stream_no_endstream},
        {"name": "stream-garbage", "category": "stream", "category_zh": "流长度不符",
         "expect": EXPECT_EITHER, "describe": "流内容是垃圾字节（长度对得上）",
         "mutate": _stream_garbage},
        # (7) 字体 / 图像资源损坏
        {"name": "resource-font-missing", "category": "resource",
         "category_zh": "字体/图像资源损坏",
         "expect": EXPECT_OPEN, "describe": "/Font 指向不存在的字体对象",
         "mutate": _resource_font_missing},
        {"name": "image-width-zero", "category": "resource",
         "category_zh": "字体/图像资源损坏",
         "expect": EXPECT_EITHER, "describe": "图像 /Width 为 0",
         "mutate": _image_width_zero},
        {"name": "image-width-huge", "category": "resource",
         "category_zh": "字体/图像资源损坏",
         "expect": EXPECT_EITHER, "describe": "图像 /Width 极大（100000）",
         "mutate": _image_width_huge},
        # (8) 加密相关损坏
        {"name": "encrypt-broken", "category": "encrypt", "category_zh": "加密相关损坏",
         "expect": EXPECT_REJECT, "describe": "有 /Encrypt，但加密字典残缺（缺 /O、/U）",
         "mutate": _encrypt_broken},
        # (9) 空文件族
        {"name": "empty-file", "category": "empty", "category_zh": "空壳文件",
         "expect": EXPECT_REJECT, "describe": "0 字节",
         "mutate": _empty_file},
        {"name": "whitespace-only", "category": "empty", "category_zh": "空壳文件",
         "expect": EXPECT_REJECT, "describe": "只有空白字符",
         "mutate": _whitespace_only},
        {"name": "eof-only", "category": "empty", "category_zh": "空壳文件",
         "expect": EXPECT_REJECT, "describe": "只有 %%EOF",
         "mutate": _eof_only},
        {"name": "header-only", "category": "empty", "category_zh": "空壳文件",
         "expect": EXPECT_REJECT, "describe": "只有文件头",
         "mutate": _header_only},
    ]


# ---------------------------------------------------------------------------
# 落盘
# ---------------------------------------------------------------------------


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def _write_json(path, payload):
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")


def _generate_malformed(out_dir, count, seed, quiet):
    """生成 count 份畸形语料 + 同名 .meta.json，返回清单。"""
    os.makedirs(out_dir, exist_ok=True)
    variants = _variants()
    mutate_rng = random.Random("mutate:%d" % seed)
    manifest = []

    for index in range(count):
        variant = variants[index % len(variants)]
        doc = _base_document(seed, index)
        data = variant["mutate"](doc, mutate_rng)
        name = "m%04d-%s" % (index + 1, variant["name"])
        pdf_path = os.path.join(out_dir, name + ".pdf")
        with open(pdf_path, "wb") as handle:
            handle.write(data)

        meta = {
            "name": name,
            "file": name + ".pdf",
            "category": variant["category"],
            "category_zh": variant["category_zh"],
            "variant": variant["name"],
            "description": variant["describe"],
            "expectation": variant["expect"],
            "expectation_zh": EXPECT_ZH[variant["expect"]],
            "bytes": len(data),
            "sha256": _sha256(data),
            "generator": {
                "tool": "gen-compat-corpus.py",
                "seed": seed,
                "count": count,
                "index": index,
            },
        }
        _write_json(os.path.join(out_dir, name + ".meta.json"), meta)
        manifest.append({
            "file": name + ".pdf",
            "category": variant["category"],
            "variant": variant["name"],
            "bytes": len(data),
            "sha256": meta["sha256"],
        })

        if not quiet and (index + 1) % 100 == 0:
            print("  malformed %d/%d" % (index + 1, count))

    return manifest


class _KeyStream:
    """确定性伪随机字节流：SHAKE-128 扩展成任意长度。

    比 random 快一个量级，而且跨进程、跨机器完全一致。
    """

    def __init__(self, seed_bytes):
        self._shake = hashlib.shake_128(seed_bytes)

    def read(self, size):
        return self._shake.digest(size)


class _StreamPdfWriter:
    """流式 PDF 写入器：边写边记偏移，最后补 xref。

    大文件（1 GiB）不能先拼成 bytes 再落盘——内存会爆。
    """

    def __init__(self, handle):
        self._handle = handle
        self._offset = 0
        self._offsets = {}
        self._write(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")

    def _write(self, data):
        self._handle.write(data)
        self._offset += len(data)

    def write_object(self, obj_id, body):
        self._offsets[obj_id] = self._offset
        self._write(b"%d 0 obj\n" % obj_id)
        self._write(body)
        self._write(b"\nendobj\n")

    def write_stream_object(self, obj_id, header, chunks):
        """写一个流对象：header 之后依次写 chunks，最后补 endstream。"""
        self._offsets[obj_id] = self._offset
        self._write(b"%d 0 obj\n" % obj_id)
        self._write(header)
        for chunk in chunks:
            self._write(chunk)
        self._write(b"\nendstream\nendobj\n")

    def finish(self, root_id):
        count = max(self._offsets) + 1
        xref_offset = self._offset
        self._write(b"xref\n0 %d\n" % count)
        self._write(b"0000000000 65535 f \n")
        for obj_id in range(1, count):
            offset = self._offsets.get(obj_id)
            if offset is None:
                self._write(b"0000000000 65535 f \n")
            else:
                self._write(b"%010d 00000 n \n" % offset)
        self._write(b"trailer\n<< /Size %d /Root %d 0 R >>\n" % (count, root_id))
        self._write(b"startxref\n%d\n%%%%EOF\n" % xref_offset)
        return self._offset


def _image_chunks(stream, total_bytes, chunk_size=1 << 20):
    """把像素数据按块吐出来：常驻内存只有 chunk_size。"""
    remaining = total_bytes
    while remaining > 0:
        size = min(chunk_size, remaining)
        yield stream.read(size)
        remaining -= size


def _write_paged_document(path, page_count, image_side, seed, label, quiet=False):
    """流式写一份多页 PDF：每页一行文本 + 一张真图。

    图像像素来自 SHAKE-128 伪随机流：内容不可压缩，文件大小可预测，
    生成过程常驻内存恒定（1 MiB 一块）。
    """
    stream = _KeyStream(b"corpus:%s:%d:%d" % (label.encode("ascii"), seed, image_side))
    pixel_bytes = image_side * image_side * 3
    total_image_bytes = pixel_bytes * page_count

    with open(path, "wb") as handle:
        writer = _StreamPdfWriter(handle)
        font_id = 3
        kids = []
        for index in range(page_count):
            page_id = 4 + 3 * index
            content_id = page_id + 1
            image_id = page_id + 2
            kids.append(b"%d 0 R" % page_id)

            text = "Page %d of %d" % (index + 1, page_count)
            content = (
                b"q 200 0 0 200 60 400 cm /Im0 Do Q\n"
                b"BT /F1 16 Tf 60 300 Td (%s) Tj ET" % _escape_text(text).encode("ascii"))
            writer.write_object(page_id, (
                b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                b"/Resources << /Font << /F1 %d 0 R >> /XObject << /Im0 %d 0 R >> >> "
                b"/Contents %d 0 R >>" % (font_id, image_id, content_id)))
            writer.write_object(content_id,
                                b"<< /Length %d >>\nstream\n%s\nendstream" % (len(content), content))
            header = (
                b"<< /Type /XObject /Subtype /Image /Width %d /Height %d "
                b"/ColorSpace /DeviceRGB /BitsPerComponent 8 /Length %d >>\nstream\n"
                % (image_side, image_side, pixel_bytes))
            writer.write_stream_object(image_id, header,
                                       _image_chunks(stream, pixel_bytes))

            if not quiet and (index + 1) % 50 == 0:
                print("  %s page %d/%d" % (label, index + 1, page_count))

        writer.write_object(1, b"<< /Type /Catalog /Pages 2 0 R >>")
        writer.write_object(2, b"<< /Type /Pages /Kids [%s] /Count %d >>"
                            % (b" ".join(kids), page_count))
        writer.write_object(font_id,
                            b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
        size = writer.finish(1)

    return size, total_image_bytes


def _size_slug(gigabytes):
    """1.0 -> '1gb'，0.25 -> '0.25gb'。"""
    text = ("%.2f" % gigabytes).rstrip("0").rstrip(".")
    return "%sgb" % text


def main():
    parser = argparse.ArgumentParser(
        description="生成性能与兼容验收用的 PDF 语料（K4/K5/K6）。")
    parser.add_argument("--out", default=os.path.join("build", "corpus"),
                        help="输出目录（默认 build/corpus）")
    parser.add_argument("--count", type=int, default=DEFAULT_COUNT,
                        help="畸形语料份数（默认 %d）" % DEFAULT_COUNT)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED,
                        help="随机种子（默认 %d）；同一种子产物逐字节一致" % DEFAULT_SEED)
    parser.add_argument("--with-large", action="store_true",
                        help="同时生成大文件语料（K4）。默认不生成：1 GiB 写入很慢。")
    parser.add_argument("--large-gb", type=float, default=1.0,
                        help="大文件目标大小，单位 GiB（默认 1.0）")
    parser.add_argument("--pages", type=int, default=DEFAULT_PAGES,
                        help="K6 内存基准文档页数（默认 %d）" % DEFAULT_PAGES)
    parser.add_argument("--page-image-size", type=int, default=256,
                        help="K6 文档每页图片的边长像素（默认 256）")
    parser.add_argument("--quiet", action="store_true", help="少打印点东西")
    args = parser.parse_args()

    if hasattr(sys.stdout, "reconfigure"):
        # 控制台是 GBK 时，别让一个生僻字把整个脚本掀翻。
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    out_dir = os.path.abspath(args.out)
    malformed_dir = os.path.join(out_dir, "malformed")
    large_dir = os.path.join(out_dir, "large")
    pages_dir = os.path.join(out_dir, "pages")
    os.makedirs(out_dir, exist_ok=True)

    if not args.quiet:
        print("生成畸形语料：%s（%d 份，seed=%d）" % (malformed_dir, args.count, args.seed))
    manifest = _generate_malformed(malformed_dir, args.count, args.seed, args.quiet)
    baseline_dir = os.path.join(out_dir, "baseline")
    baseline = _generate_baseline(baseline_dir, args.seed, args.quiet)

    summary = {
        "tool": "gen-compat-corpus.py",
        "seed": args.seed,
        "count": args.count,
        "malformed_dir": "malformed",
        "malformed": manifest,
        "baseline_dir": "baseline",
        "baseline": baseline,
        "large_dir": "large",
        "large": None,
        "pages_dir": "pages",
        "pages": None,
    }

    if args.with_large:
        side = 1024
        pixel_bytes = side * side * 3
        target = int(args.large_gb * GIB)
        page_count = max(1, -(-target // pixel_bytes))
        name = "large-%s.pdf" % _size_slug(args.large_gb)
        os.makedirs(large_dir, exist_ok=True)
        path = os.path.join(large_dir, name)
        if not args.quiet:
            print("生成大文件语料：%s（%d 页 x %dx%d，约 %.2f GiB）"
                  % (path, page_count, side, side, args.large_gb))
        size, image_bytes = _write_paged_document(path, page_count, side, args.seed,
                                                   "large", args.quiet)
        summary["large"] = {"file": name, "bytes": size,
                            "pages": page_count, "image_side": side}
        if not args.quiet:
            print("  写入 %d 字节（%.3f GiB）" % (size, size / float(GIB)))

    os.makedirs(pages_dir, exist_ok=True)
    pages_name = "pages-%d.pdf" % args.pages
    pages_path = os.path.join(pages_dir, pages_name)
    if not args.quiet:
        print("生成 K6 内存基准文档：%s（%d 页）" % (pages_path, args.pages))
    pages_size, pages_image_bytes = _write_paged_document(
        pages_path, args.pages, args.page_image_size, args.seed, "pages", args.quiet)
    summary["pages"] = {"file": pages_name, "bytes": pages_size,
                         "pages": args.pages, "image_side": args.page_image_size}
    if not args.quiet:
        print("  写入 %d 字节（%.1f MiB）" % (pages_size, pages_size / 1048576.0))

    _write_json(os.path.join(out_dir, "manifest.json"), summary)
    if not args.quiet:
        print("完成。清单：%s" % os.path.join(out_dir, "manifest.json"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
