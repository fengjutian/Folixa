#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""负例对照：证明 `scripts/verify-screen-reader.py` 真的会拦人，不是永远放行。

做法：手工造三份「坏掉的带标签 PDF」，每一份只破坏一处，
门禁必须当场变红；然后拿一份完好的 PDF，门禁必须放行。

三份坏件分别破坏的是三条判定：
  A. 元素的 /K 指向一个页上不存在的 MCID  → 「页上没有这段标记内容」
  B. /ParentTree 少映射一个元素           → 「在 /ParentTree 里找不到自己」
  C. 结构树里两个元素顺序对调             → 「朗读顺序与标记内容顺序不一致」

为什么用手工造而不是改产物：产物的对象号与流都是 qpdf 写的，
按字节改很容易改坏别的地方；手工造能把「只破坏这一条」看得很清楚。
这与仓库里 tests/SemanticTagWriterTests.cpp 的黄金夹具是同一个路子。

用法：python scripts/test-screen-reader-negative.py
"""
from __future__ import annotations

import io
import os
import subprocess
import sys

# CI 的 Windows 控制台默认是 GBK，而判定原因里会出现 U+FFFD 之类的字符；
# 不显式换成 UTF-8 会抛 UnicodeEncodeError，把「门禁变红」伪装成脚本崩溃。
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERIFIER = os.path.join(ROOT, "scripts", "verify-screen-reader.py")
WORK = os.path.join(ROOT, "build", "a11y-negative")

TEXT_TITLE = "Chapter One"
TEXT_BODY = "First paragraph line."


def build_pdf(objects, root_id):
    out = bytearray(b"%PDF-1.7\n")
    offsets = {}
    for num in sorted(objects):
        offsets[num] = len(out)
        out.extend(("%d 0 obj\n" % num).encode("ascii"))
        body = objects[num]
        out.extend(body if isinstance(body, bytes) else body.encode("ascii"))
        out.extend(b"\nendobj\n")
    max_id = max(offsets)
    xref = len(out)
    out.extend(("xref\n0 %d\n" % (max_id + 1)).encode("ascii"))
    out.extend(b"0000000000 65535 f \n")
    for num in range(1, max_id + 1):
        if num in offsets:
            out.extend(("%010d 00000 n \n" % offsets[num]).encode("ascii"))
        else:
            out.extend(b"0000000000 65535 f \n")
    out.extend(("trailer\n<< /Size %d /Root %d 0 R >>\nstartxref\n%d\n%%%%EOF\n"
                % (max_id + 1, root_id, xref)).encode("ascii"))
    return bytes(out)


def make_case(kind: str) -> bytes:
    """造一份带标签 PDF。kind 决定破坏哪一处。"""
    xmp = ("<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
           "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
           "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
           "<rdf:Description xmlns:pdfuaid=\"http://www.aiim.org/pdfua/ns/id/\" "
           "pdfuaid:part=\"1\"/>\n</rdf:RDF>\n</x:xmpmeta>\n<?xpacket end=\"w\"?>")
    content = ("q /P <</MCID 0>> BDC BT /F1 20 Tf 72 700 Td (Chapter One) Tj ET EMC Q\n"
               "q /P <</MCID 1>> BDC BT /F1 11 Tf 72 670 Td (First paragraph line.) "
               "Tj ET EMC Q\n")
    stream = ("<< /Length %d >>\nstream\n" % len(content)).encode("ascii") + \
        content.encode("ascii") + b"\nendstream"

    # 结构树：Document → Part → Sect → H1(mcid 0) / P(mcid 1)。
    # kind=C 时把 H1 与 P 对调（朗读顺序与页上顺序不一致）。
    first, second = (10, 11)
    if kind == "c":
        first, second = 11, 10

    # kind=A 时第二个元素指向一个页上不存在的 MCID 9。
    second_k = "9" if kind == "a" else "1"

    objects = {
        1: "<< /Type /Catalog /Pages 2 0 R /StructTreeRoot 4 0 R "
           "/MarkInfo << /Marked true >> /Lang (zh-CN) /Metadata 20 0 R >>",
        2: "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        3: "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Tabs /S "
           "/StructParents 0 /Resources << /Font << /F1 5 0 R >> >> /Contents 6 0 R >>",
        4: "<< /Type /StructTreeRoot /K 7 0 R /ParentTree 8 0 R >>",
        5: "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        7: "<< /Type /StructElem /S /Document /P 4 0 R /K 8 0 R >>",
        8: "<< /Type /StructElem /S /Part /Pg 3 0 R /P 7 0 R /K 9 0 R >>",
        9: "<< /Type /StructElem /S /Sect /P 8 0 R /K [%d 0 R %d 0 R] >>" % (first, second),
        10: "<< /Type /StructElem /S /H1 /P 9 0 R /K 0 >>",
        11: "<< /Type /StructElem /S /P /P 9 0 R /K %s >>" % second_k,
    }
    # kind=B 时 ParentTree 只映射第一个元素，第二个元素找不到自己。
    if kind == "b":
        objects[12] = "<< /Nums [0 [10 0 R]] >>"
    else:
        objects[12] = "<< /Nums [0 [10 0 R 11 0 R]] >>"

    out = bytearray(build_pdf(objects, 1))
    # 内容流与 XMP 要按对象号插进去：重排一次对象表更省事。
    merged = dict(objects)
    merged[6] = stream
    merged[20] = ("<< /Type /Metadata /Subtype /XML /Length %d >>\nstream\n"
                  % len(xmp)).encode("ascii") + xmp.encode("utf-8") + b"\nendstream"
    merged[4] = "<< /Type /StructTreeRoot /K 7 0 R /ParentTree 12 0 R >>"
    return build_pdf(merged, 1)


def run_verifier(path):
    proc = subprocess.run(
        [sys.executable, VERIFIER, "--pdf", path], capture_output=True, encoding="utf-8",
        errors="replace")
    return proc.returncode, (proc.stdout or "") + (proc.stderr or "")


def main():
    os.makedirs(WORK, exist_ok=True)
    failures = []

    # 先确认「完好的那份」是绿的（否则下面的红没有意义）。
    good = make_case("good")
    good_path = os.path.join(WORK, "good.pdf")
    io.open(good_path, "wb").write(good)
    code, out = run_verifier(good_path)
    if code != 0:
        failures.append("完好的那份没有被放行（负例就没有意义了）：\n" + out)
    else:
        print("对照：完好的那份被放行")

    expectations = {
        "a": "页上没有这段标记内容",
        "b": "在 /ParentTree 里找不到自己",
        "c": "朗读顺序与标记内容顺序不一致",
    }
    for kind, expected in expectations.items():
        path = os.path.join(WORK, "broken-%s.pdf" % kind)
        io.open(path, "wb").write(make_case(kind))
        code, out = run_verifier(path)
        if code == 0:
            failures.append("负例 %s：把「%s」破坏掉之后门禁仍然放行" % (kind, expected))
            continue
        hits = [line.strip() for line in out.splitlines() if expected in line]
        if not hits:
            failures.append("负例 %s：门禁变红了，但报的不是「%s」：\n%s" % (kind, expected, out))
            continue
        print("负例 %s 按预期变红：%s" % (kind, hits[0][:120]))

    print()
    if failures:
        print("负例对照失败：")
        for item in failures:
            print("   - %s" % item)
        return 1
    print("负例对照通过：朗读稿门真的会拦人")
    return 0


if __name__ == "__main__":
    sys.exit(main())
