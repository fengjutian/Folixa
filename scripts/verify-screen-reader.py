#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""独立复核「读屏会念出什么」—— 按 PDF/UA-1 的取文路径读一份带标签 PDF。

为什么要一个**独立实现**
------------------------
本程序自己写结构树、自己读结构树（`pdf::semanticReadingOrder` /
`pdf::semanticReadingText`），这条链路只能证明「自己能读懂自己写的」。
要证明「读屏软件也读得懂」，就得让**另一套实现**读同一份 PDF：
结构树 → 标记内容 → 文字，取文规则按 PDF/UA-1 走。

这个脚本**不引用本仓库的任何 C++ 代码**，只用标准库 + 本仓库已经依赖的 qpdf
（qpdf 负责把对象图与解码后的内容流取出来，取文规则在这里独立实现）。
仓库里已有的 `scripts/verify-office-images.py` / `verify-recordings.py`
是同一个路子：独立写一份解析器，再用它回读产物。

它产出什么
----------
一份「朗读稿」：按结构树顺序，每个元素一行 —— 角色 + 读屏应当念出的文字。
这既是给人工朗读用的对照稿，也是机器可判定的验收依据。

判定规则（有一条不满足就退出码非零）
------------------------------------
  1. 文档必须有 /StructTreeRoot、/MarkInfo /Marked true、/Lang。
  2. 每个**内容元素**（非纯容器）必须既有文字又有意义：文字为空且没有 /Alt
     的元素，读屏只会念出一段空白 —— 这条必须报错。
  3. /ParentTree 必须与页上的标记内容**双射**：每个 MCID 恰好对应一个结构元素，
     每个标记内容也必须能在 /ParentTree 里找到自己的元素。
  4. 朗读顺序必须与页上标记内容出现的顺序一致（同一页内，结构树里的元素顺序
     应当与 MCID 升序一致）。
  5. 表单域（/Form）必须有 /T（字段名）与 /Alt（替代文本），否则读屏念不出字段。
  6. 表格（/Table）里至少要有一个表头格（/TH）。

用法
----
  python scripts/verify-screen-reader.py --pdf <带标签的.pdf>
  python scripts/verify-screen-reader.py --build          # 跑单测产出样例
  python scripts/verify-screen-reader.py --pdf <pdf> --narrate   # 打开 + 打印对照稿
"""
from __future__ import annotations

import argparse
import base64
import binascii
import json
import os
import re
import subprocess
import sys
import zlib

# 同 test-screen-reader-negative.py：控制台编码不能决定这道门是否崩。
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QPDF = os.path.join(REPO_ROOT, "tools", "qpdf", "bin", "qpdf.exe")
BUILD_BIN = os.path.join(REPO_ROOT, "build", "windows-debug", "bin")
SAMPLE_DIR = os.path.join(REPO_ROOT, "build", "a11y-sample")

# 容器类元素：自己不该有文字，文字在子元素里。
CONTAINER_ROLES = {"Document", "Part", "Sect", "Div", "Art", "Aside",
                   "BlockQuote", "Caption", "L", "LI", "LBody", "Lbl", "LItem"}
# 需要有文字才有意义的内容元素（读屏念出来是内容，不是空）。
CONTENT_ROLES = {"H", "H1", "H2", "H3", "H4", "H5", "H6", "P", "TD", "TH",
                 "THead", "TBody", "TFoot", "Title", "FENote", "Note", "Quote"}


# ============================================================ qpdf 取对象图 ===

class QpdfDocument:
    """用 qpdf 把 PDF 读成对象表（标准库解析 JSON，不引第三方库）。"""

    def __init__(self, path: str):
        self.path = path
        if not os.path.exists(QPDF):
            raise RuntimeError("找不到 qpdf：%s（先跑 scripts/bootstrap-qpdf.ps1）" % QPDF)
        proc = subprocess.run(
            [QPDF, "--json-output=latest", path], capture_output=True
        )
        if proc.returncode != 0:
            raise RuntimeError("qpdf 读不了 %s：%s" % (path, proc.stderr.decode("utf-8", "replace")[:400]))
        data = json.loads(proc.stdout.decode("utf-8"))
        objects = {}
        for key, value in data["qpdf"][1].items():
            if not key.startswith("obj:"):
                continue
            objects[key[4:]] = value  # "1 0 R" -> {"value": ...}
        self.objects = objects

    def resolve(self, ref):
        """引用（"12 0 R"）→ 对象体；不是引用就原样返回。"""
        if isinstance(ref, str) and re.fullmatch(r"\d+ \d+ R", ref):
            return self.objects.get(ref, {})
        return {"value": ref}

    def value(self, ref):
        entry = self.resolve(ref)
        return entry.get("value")

    def raw(self, ref):
        return self.resolve(ref)

    def stream_bytes(self, ref) -> bytes:
        """取流对象的**解码后**字节（自己按 /Filter 解 Flate）。"""
        entry = self.resolve(ref)
        stream = entry.get("stream")
        if not stream:
            return b""
        data = stream.get("data", "")
        try:
            raw = base64.b64decode(data) if data else b""
        except (binascii.Error, ValueError):
            raw = b""
        filters = self.value(stream.get("dict", {}).get("/Filter"))
        if isinstance(filters, str):
            filters = [filters]
        if not filters:
            return raw
        for flt in filters:
            if flt != "/FlateDecode":
                raise RuntimeError("不认识的过滤器：%s" % flt)
            try:
                raw = zlib.decompress(raw)
            except zlib.error:
                raw = zlib.decompressobj().decompress(raw)
        return raw


# ========================================================== 内容流取文字 ======

class Token:
    __slots__ = ("kind", "value")

    def __init__(self, kind, value):
        self.kind = kind
        self.value = value


def tokenize(data: bytes):
    """把内容流切成记号：name / number / string / dict / array / operator。"""
    tokens = []
    i = 0
    n = len(data)
    while i < n:
        ch = data[i:i + 1]
        if ch in b" \t\r\n\f\x00":
            i += 1
            continue
        if ch == b"%":
            while i < n and data[i:i + 1] not in b"\r\n":
                i += 1
            continue
        if ch == b"(":
            depth = 1
            i += 1
            buf = bytearray()
            while i < n and depth:
                c = data[i:i + 1]
                if c == b"\\":
                    nxt = data[i + 1:i + 2]
                    mapping = {b"n": b"\n", b"r": b"\r", b"t": b"\t", b"b": b"\b",
                               b"f": b"\f", b"(": b"(", b")": b")", b"\\": b"\\"}
                    if nxt in mapping:
                        buf += mapping[nxt]
                        i += 2
                    elif nxt.isdigit():
                        digits = b""
                        j = i + 1
                        while j < n and len(digits) < 3 and data[j:j + 1].isdigit():
                            digits += data[j:j + 1]
                            j += 1
                        buf.append(int(digits, 8) & 0xFF)
                        i = j
                    elif nxt in (b"\n", b"\r"):
                        i += 2
                    else:
                        buf += nxt
                        i += 2
                    continue
                if c == b"(":
                    depth += 1
                elif c == b")":
                    depth -= 1
                    if depth == 0:
                        i += 1
                        break
                buf += c
                i += 1
            tokens.append(Token("string", bytes(buf)))
            continue
        if ch == b"<" and data[i + 1:i + 2] != b"<":
            end = data.find(b">", i + 1)
            if end < 0:
                break
            hexdigits = re.sub(rb"[^0-9A-Fa-f]", b"", data[i + 1:end])
            if len(hexdigits) % 2:
                hexdigits += b"0"
            tokens.append(Token("string", binascii.unhexlify(hexdigits)))
            i = end + 1
            continue
        if data[i:i + 2] == b"<<":
            tokens.append(Token("dict_start", None))
            i += 2
            continue
        if data[i:i + 2] == b">>":
            tokens.append(Token("dict_end", None))
            i += 2
            continue
        if ch == b"[":
            tokens.append(Token("array_start", None))
            i += 1
            continue
        if ch == b"]":
            tokens.append(Token("array_end", None))
            i += 1
            continue
        if ch == b"/":
            j = i + 1
            while j < n and data[j:j + 1] not in b" \t\r\n\f\x00/[]<>(){}%":
                j += 1
            tokens.append(Token("name", "/" + data[i + 1:j].decode("latin-1")))
            i = j
            continue
        if ch.isdigit() or ch in b"+-.":
            j = i
            while j < n and data[j:j + 1] in b"0123456789+-.eE":
                j += 1
            try:
                tokens.append(Token("number", float(data[i:j])))
            except ValueError:
                pass
            i = j
            continue
        j = i
        while j < n and data[j:j + 1] not in b" \t\r\n\f\x00/[]<>(){}%":
            j += 1
        if j == i:
            i += 1
            continue
        tokens.append(Token("op", data[i:j].decode("latin-1")))
        i = j
    return tokens


def decode_pdf_text(raw: bytes) -> str:
    """PDF 字符串 → 文本：带 BOM 的按 UTF-16BE，其余按 PDFDocEncoding 的 ASCII 部分。"""
    if raw.startswith(b"\xfe\xff"):
        return raw[2:].decode("utf-16-be", "replace")
    return raw.decode("latin-1")


def mcid_text_by_page(doc: QpdfDocument, page_refs):
    """每页：MCID → 该标记内容里的文字。

    page_refs 是页面引用列表（按页序）。返回 {page_ref: {mcid: text}}。
    """
    result = {}
    for page_ref in page_refs:
        page = doc.value(page_ref) or {}
        contents = page.get("/Contents")
        refs = []
        if isinstance(contents, str):
            refs = [contents]
        elif isinstance(contents, list):
            refs = [c for c in contents if isinstance(c, str)]
        data = b"".join(doc.stream_bytes(r) for r in refs)
        result[page_ref] = extract_mcid_text(data)
    return result


def extract_mcid_text(content: bytes):
    """扫内容流：BDC/EMC 之间、按 /MCID 归集文字。

    /MCID 出现在 BDC 的属性字典里（`/P <</MCID 0>> BDC`），
    所以词法阶段要留着名字，靠词法下标往后看一个数字。
    """
    tokens = tokenize(content)
    stack = []          # 标记内容栈：每层记 {"mcid": 编号或 None, "text": [文字片段]}
    collected = {}      # mcid -> list[str]
    pending_mcid = None

    for index, tok in enumerate(tokens):
        if tok.kind == "name":
            if tok.value == "/MCID":
                nxt = tokens[index + 1] if index + 1 < len(tokens) else None
                if nxt is not None and nxt.kind == "number":
                    pending_mcid = int(nxt.value)
            continue
        if tok.kind in ("dict_start", "dict_end", "array_start", "array_end"):
            continue
        if tok.kind == "number":
            continue
        if tok.kind == "string":
            # 文字归到**当前最内层**的标记内容上。
            #
            # 为什么不在 ET 上清空缓冲：BDC/EMC 与 BT/ET 的嵌套有两种合法写法
            # （`BT … BDC … EMC ET` 与 `BT … ET EMC`），EMC 在 ET 之后时，
            # 在 ET 上清空会把这一段文字整个丢掉。
            if stack:
                stack[-1]["text"].append(decode_pdf_text(tok.value))
            continue
        op = tok.value
        if op == "BDC":
            stack.append({"mcid": pending_mcid, "text": []})
            pending_mcid = None
        elif op == "EMC":
            if stack:
                frame = stack.pop()
                if frame["mcid"] is not None and frame["text"]:
                    collected.setdefault(frame["mcid"], []).append("".join(frame["text"]))
    return collected


# ================================================================ 结构树 ======

class StructElement:
    __slots__ = ("ref", "role", "alt", "actual", "title", "lang", "parent",
                 "kids", "page", "mcids", "widget", "is_root")

    def __init__(self):
        self.ref = ""
        self.role = ""
        self.alt = ""
        self.actual = ""
        self.title = ""
        self.lang = ""
        self.parent = ""
        self.kids = []      # 子结构元素
        self.page = ""      # /Pg
        self.mcids = []     # /K 里的全部 MCID（一个段落可能有多段标记内容）
        self.widget = ""    # /K 指向 widget 注解时
        self.is_root = False


def strip_u(text):
    """qpdf 的 JSON 里 unicode 字符串带 "u:" 前缀。"""
    if isinstance(text, str) and text.startswith("u:"):
        return text[2:]
    return text


def normalize_role(role):
    """结构元素的 /S 是名字（"/H1"），去掉斜杠好比较。"""
    if not role:
        return ""
    return role[1:] if role.startswith("/") else role


def read_structure(doc: QpdfDocument):
    """读出结构树元素列表（按文档顺序）与角色映射。"""
    catalog = None
    for ref, entry in doc.objects.items():
        value = entry.get("value")
        if isinstance(value, dict) and value.get("/Type") == "/Catalog":
            catalog = value
            break
    if catalog is None:
        raise RuntimeError("没有 Catalog")
    root_ref = catalog.get("/StructTreeRoot")
    if not root_ref:
        raise RuntimeError("没有 /StructTreeRoot")
    root = doc.value(root_ref) or {}
    role_map = {}
    for name, target in (root.get("/RoleMap") or {}).items():
        role_map[name] = target

    elements = []

    def build(ref, parent_ref, seen, inherited_page=""):
        if ref in seen:
            return None
        seen.add(ref)
        value = doc.value(ref)
        if not isinstance(value, dict):
            return None
        element = StructElement()
        element.ref = ref
        element.role = normalize_role(role_map.get(value.get("/S"), value.get("/S") or ""))
        element.alt = strip_u(value.get("/Alt") or "")
        element.actual = strip_u(value.get("/ActualText") or "")
        element.title = strip_u(value.get("/T") or "")
        element.lang = strip_u(value.get("/Lang") or "")
        element.parent = parent_ref
        # /Pg 是**可继承**的（PDF 32000-1 §14.7.2）：没写就沿用父元素的页。
        element.page = value.get("/Pg") or inherited_page
        content = value.get("/K")
        if isinstance(content, int):
            element.mcids.append(content)
        elif isinstance(content, list):
            # /K 是数组时，三种内容可以混着来（PDF 32000-1 §14.7.2）：
            # MCID 数字、结构元素引用、/OBJR 或 widget 字典。
            for item in content:
                if isinstance(item, int):
                    element.mcids.append(item)
                elif isinstance(item, str) and re.fullmatch(r"\d+ \d+ R", item):
                    # 数组里的引用可能是子结构元素，也可能是 widget 注解
                    # （表单域元素走这条：/K 指向 widget，PDF 32000-1 §14.7.2）。
                    target = doc.value(item) or {}
                    if target.get("/Type") == "/Annot" or "/Subtype" in target:
                        element.widget = item
                        continue
                    child = build(item, ref, seen, element.page)
                    if child is not None:
                        element.kids.append(child)
                elif isinstance(item, dict):
                    # /OBJR 或 widget 字典：不是结构元素，是内容引用。
                    element.widget = element.widget or ""
        elif isinstance(content, str) and re.fullmatch(r"\d+ \d+ R", content):
            target = doc.value(content) or {}
            if target.get("/Type") == "/Annot" or "/Subtype" in target:
                element.widget = content
            else:
                child = build(content, ref, seen, element.page)
                if child is not None:
                    element.kids.append(child)
        elements.append(element)
        return element

    roots = root.get("/K")
    root_refs = roots if isinstance(roots, list) else [roots]
    seen = set()
    top = []
    for ref in root_refs:
        if isinstance(ref, str):
            element = build(ref, root_ref, seen, "")
            if element is not None:
                element.is_root = True
                top.append(element)
    return elements, top, root, catalog


def read_parent_tree(doc: QpdfDocument, root):
    """/ParentTree → {key: [元素引用, ...]}（key 是页的 /StructParents 或注解的 /StructParent）。"""
    tree_ref = root.get("/ParentTree")
    if not tree_ref:
        return {}
    tree = doc.value(tree_ref) or {}
    nums = tree.get("/Nums")
    if not isinstance(nums, list):
        return {}
    out = {}
    for i in range(0, len(nums) - 1, 2):
        key = nums[i]
        value = nums[i + 1]
        items = value if isinstance(value, list) else [value]
        out[key] = [x for x in items if isinstance(x, str)]
    return out


def page_refs_of(doc: QpdfDocument):
    """按页序返回页面引用列表。"""
    catalog = None
    for ref, entry in doc.objects.items():
        value = entry.get("value")
        if isinstance(value, dict) and value.get("/Type") == "/Catalog":
            catalog = value
            break
    pages_ref = catalog.get("/Pages")
    pages_value = doc.value(pages_ref) or {}
    out = []

    def walk(ref):
        value = doc.value(ref) or {}
        if value.get("/Type") == "/Page":
            out.append(ref)
            return
        for kid in value.get("/Kids") or []:
            if isinstance(kid, str):
                walk(kid)

    if pages_ref:
        walk(pages_ref)
    return out


# ============================================================== 朗读稿 ========

def narration_for(doc: QpdfDocument, elements, top, parent_tree, page_texts):
    """按结构树顺序产出朗读稿：[(元素, 角色, 读屏应当念出的文字)]。"""
    rows = []

    def element_text(element):
        if element.role == "Form":
            return element.alt or element.title
        if element.role == "Figure":
            return element.alt
        if element.actual:
            return element.actual
        if element.mcids:
            parts = []
            for mcid in element.mcids:
                page = element.page
                if not page:
                    # 元素没写 /Pg：在所有页里找这个 MCID。
                    for ref, mcid_map in page_texts.items():
                        if mcid in mcid_map:
                            page = ref
                            break
                parts.extend(page_texts.get(page, {}).get(mcid, []))
            return " ".join(p for p in parts if p)
        return ""

    def walk(element):
        rows.append((element, element.role, element_text(element)))
        for kid in element.kids:
            walk(kid)

    for element in top:
        walk(element)
    return rows


def check(doc: QpdfDocument, elements, root, catalog, parent_tree, page_texts, rows):
    """判定：返回 (问题, 提醒)。问题非空 = 不合格。"""
    problems = []
    warnings = []
    if not catalog.get("/MarkInfo", {}).get("/Marked"):
        problems.append("catalog 没有 /MarkInfo /Marked true")
    if not strip_u(catalog.get("/Lang") or ""):
        problems.append("catalog 没有 /Lang（读屏要靠它选语音）")

    # 1) 内容元素必须有文字；容器元素自己不该有文字。
    for element, role, text in rows:
        if role in CONTAINER_ROLES:
            continue
        if role == "Form":
            continue   # 表单域单独查（要 /T 与 /Alt）
        if role == "Figure":
            # 图没有 /Alt 不算失败：本项目的口径是「没有依据的替代文本不写」，
            # 改不成就把原因列进 issues，不编一句 —— 这里是提醒，不是错误。
            if not element.alt:
                warnings.append("/Figure 元素 %s 没有 /Alt（读屏只会念「图」）" % element.ref)
            continue
        if role in CONTENT_ROLES:
            if not text.strip():
                problems.append("%s 元素（%s）读不出文字（MCID=%s）"
                                % (role, element.ref, element.mcids))

    # 2) 标记内容与结构元素必须双射：每个 MCID 恰好一个元素，反之亦然。
    content_mcids = set()
    for mcid_map in page_texts.values():
        content_mcids.update(mcid_map.keys())
    element_mcids = {}
    for element in elements:
        for mcid in element.mcids:
            element_mcids.setdefault(mcid, []).append(element.ref)
    for mcid in sorted(content_mcids - set(element_mcids)):
        problems.append("MCID %s 在页上存在，但结构树里没有对应的元素" % mcid)
    for mcid, refs in sorted(element_mcids.items()):
        if mcid not in content_mcids:
            problems.append("结构元素 %s 指向 MCID %s，但页上没有这段标记内容"
                            % (refs[0], mcid))
        if len(refs) > 1:
            problems.append("MCID %s 被 %d 个结构元素引用（应当恰好一个）：%s"
                            % (mcid, len(refs), "、".join(refs)))

    # 3) 每个结构元素都要能在 /ParentTree 里找到自己。
    mapped = set()
    for refs in parent_tree.values():
        mapped.update(refs)
    for element in elements:
        for mcid in element.mcids:
            if element.ref not in mapped:
                problems.append("结构元素 %s（MCID=%s）在 /ParentTree 里找不到自己"
                                % (element.ref, mcid))

    # 4) 同一页内，结构树顺序必须与 MCID 升序一致。
    by_page = {}
    for element in elements:
        if not element.mcids or not element.page:
            continue
        by_page.setdefault(element.page, []).append(element)
    for page, page_elements in by_page.items():
        mcids = [mcid for e in page_elements for mcid in e.mcids]
        if mcids != sorted(mcids):
            problems.append("页 %s 的朗读顺序与标记内容顺序不一致：%s" % (page, mcids))

    # 5) 表单域必须有 /T 与 /Alt。
    for element in elements:
        if element.role == "Form":
            if not element.title:
                problems.append("表单域元素 %s 没有 /T（字段名）" % element.ref)
            if not element.alt:
                problems.append("表单域元素 %s 没有 /Alt（替代文本）" % element.ref)

    # 6) 表格里至少要有一个表头格。
    for element in elements:
        if element.role != "Table":
            continue
        has_header = any(k.role == "TH" for k in element.kids)
        if not has_header:
            problems.append("表格元素 %s 里没有 /TH 表头格" % element.ref)
    return problems, warnings


# =============================================================== 主流程 =======

def build_samples():
    """跑 UaGeneratorTests，落盘两份带标签样例（本程序真实写入器的产物）。"""
    exe = os.path.join(BUILD_BIN, "UaGeneratorTests.exe")
    if not os.path.exists(exe):
        print("[!] 找不到 %s，先构建测试目标" % exe, file=sys.stderr)
        return 1
    os.makedirs(SAMPLE_DIR, exist_ok=True)
    env = dict(os.environ)
    env["AIPDF_A11Y_DUMP_DIR"] = SAMPLE_DIR
    proc = subprocess.run([exe], capture_output=True, env=env)
    sys.stdout.write(proc.stdout.decode("utf-8", "replace"))
    sys.stderr.write(proc.stderr.decode("utf-8", "replace"))
    if proc.returncode != 0:
        return proc.returncode
    print("[i] 样例已落盘：%s" % SAMPLE_DIR)
    return 0


def verify(pdf_path, show_narration=True):
    doc = QpdfDocument(pdf_path)
    elements, top, root, catalog = read_structure(doc)
    parent_tree = read_parent_tree(doc, root)
    pages = page_refs_of(doc)
    page_texts = mcid_text_by_page(doc, pages)
    rows = narration_for(doc, elements, top, parent_tree, page_texts)
    problems, warnings = check(doc, elements, root, catalog, parent_tree, page_texts, rows)

    if show_narration:
        print("==== 朗读稿（按结构树顺序）====")
        for index, (element, role, text) in enumerate(rows, 1):
            print("%2d. [%-8s] %s" % (index, role, text if text else "(无文字)"))
        print()

    for item in warnings:
        print("[提醒] %s" % item)

    if problems:
        print("!! 不合格：")
        for item in problems:
            print("   - %s" % item)
        return 1
    print("[OK] 朗读稿完整、顺序与标记内容一致、ParentTree 双射（%d 个结构元素）" % len(elements))
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pdf", help="要复核的带标签 PDF")
    parser.add_argument("--build", action="store_true", help="跑单测产出样例")
    parser.add_argument("--all", action="store_true",
                        help="产出样例并逐份复核（CI 用这一条）")
    parser.add_argument("--narrate", action="store_true",
                        help="打印朗读稿后再打开 PDF（供人工朗读对照）")
    args = parser.parse_args()

    if args.build:
        return build_samples()
    if args.all:
        code = build_samples()
        if code != 0:
            return code
        for name in ("ua-text.pdf", "ua-form.pdf"):
            path = os.path.join(SAMPLE_DIR, name)
            if not os.path.exists(path):
                print("[!] 样例没落盘：%s" % path, file=sys.stderr)
                return 1
            print("==== 复核 %s ====" % name)
            code = verify(path)
            if code != 0:
                return code
        return 0
    if not args.pdf:
        parser.error("要给 --pdf、--build 或 --all")

    code = verify(args.pdf)
    if args.narrate and code == 0:
        target = os.path.abspath(args.pdf)
        print("[i] 打开 %s（用读屏软件念一遍，对照上面的朗读稿）" % target)
        try:
            os.startfile(target)  # noqa: S606  Windows 打开默认阅读器
        except Exception as exc:
            print("[!] 打开失败：%s" % exc)
    return code


if __name__ == "__main__":
    sys.exit(main())
