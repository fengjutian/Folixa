#!/usr/bin/env python3
"""生成 PDFium 适配层使用的测试 PDF 集合。

纯标准库实现，手工计算 xref 偏移，保证文件结构合法。
加密样例依赖 pypdf（见 --encrypted），缺失时会跳过并说明原因。
"""

import argparse
import os
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


def corrupt_pdf():
    """结构被破坏的 PDF：文件头正常，但对象内容是垃圾。"""
    body = b"%PDF-1.7\n" + os.urandom(0) + b"this is not a pdf body at all\n"
    return body + b"%%EOF\n"


def truncated_pdf():
    """合法 PDF 的前半段：xref 与 trailer 都被截断。"""
    return normal_pdf()[:120]


def encrypted_pdf():
    """带用户密码的加密 PDF（需要 pypdf）。"""
    try:
        from pypdf import PdfWriter
    except ImportError:
        return None
    writer = PdfWriter()
    writer.add_blank_page(width=200, height=100)
    writer.encrypt("secret", algorithm="AES-256")
    import io

    buffer = io.BytesIO()
    writer.write(buffer)
    return buffer.getvalue()


FIXTURES = {
    "normal.pdf": normal_pdf,
    "empty.pdf": empty_pdf,
    "rotated.pdf": rotated_pdf,
    "corrupt.pdf": corrupt_pdf,
    "truncated.pdf": truncated_pdf,
    "encrypted.pdf": encrypted_pdf,
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", help="输出目录")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    os.makedirs(args.output, exist_ok=True)
    missing = []
    for name, factory in FIXTURES.items():
        data = factory()
        if data is None:
            missing.append(name)
            continue
        path = os.path.join(args.output, name)
        with open(path, "wb") as handle:
            handle.write(data)
        if not args.quiet:
            print("wrote %-16s %6d bytes" % (name, len(data)))

    if missing:
        print("skipped (needs pypdf): %s" % ", ".join(missing), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
