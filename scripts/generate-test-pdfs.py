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


FIXTURES = {
    "normal.pdf": normal_pdf,
    "empty.pdf": empty_pdf,
    "rotated.pdf": rotated_pdf,
    "corrupt.pdf": corrupt_pdf,
    "truncated.pdf": truncated_pdf,
    "encrypted.pdf": encrypted_pdf,
    "multipage.pdf": multipage_pdf,
    "long.pdf": long_pdf,
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", help="输出目录")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    os.makedirs(args.output, exist_ok=True)
    for name, factory in FIXTURES.items():
        data = factory()
        path = os.path.join(args.output, name)
        with open(path, "wb") as handle:
            handle.write(data)
        if not args.quiet:
            print("wrote %-16s %6d bytes" % (name, len(data)))

    return 0


if __name__ == "__main__":
    sys.exit(main())
