# -*- coding: utf-8 -*-
"""负例对照：证明 check-partial-boundaries.py 真的会拦人，不是永远放行。

做法与 `test-doc-claims-negative.py` 同一套路：字节级备份 → 故意改坏 →
守卫必须当场变红 → 原样还原 → 必须复绿。

**为什么用字节级备份而不是 git checkout**：这几个文件可能正有未提交的改动，
git checkout 会把那些改动一起冲掉。

四个负例分别打三件事，外加一条「别把守卫写成误伤」：

  1. 去掉某条 partial 行的加粗边界 —— 守卫必须变红
     （这就是旧账「只写了实现、没标边界」的原样复现）。
  2. 删掉一条豁免登记 —— 守卫必须变红
     （证明 R2 真在拦「评分卡已对齐、矩阵还挂着 partial」）。
  3. 把某条豁免对应的矩阵行改成 aligned —— 守卫必须变红
     （证明豁免会过期：降级补上了、矩阵升了 aligned，豁免不能留着当通行证）。
  4. 塞一条合法豁免进去 —— **不该**变红：豁免机制本身要能正常使用。

用法：python scripts/test-partial-boundaries-negative.py
"""
import csv
import io
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MATRIX = os.path.join(ROOT, "docs", "benchmark", "foxit-features.csv")
EXEMPT = os.path.join(ROOT, "docs", "benchmark", "matrix-scorecard-exemptions.csv")
GUARD = os.path.join(ROOT, "scripts", "check-partial-boundaries.py")


def run_guard():
    proc = subprocess.run(
        [sys.executable, GUARD],
        capture_output=True,
        cwd=ROOT,
        encoding="utf-8",
        errors="replace",
    )
    return proc.returncode, (proc.stdout or "") + (proc.stderr or "")


def first_problem(out):
    for line in out.splitlines():
        if line.strip().startswith("- "):
            return line.strip()[2:]
    return out.strip().splitlines()[-1] if out.strip() else "(没有输出)"


def read_rows(path):
    with io.open(path, "r", encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def write_rows(path, rows, fieldnames, bom):
    with io.open(path, "w", encoding="utf-8-sig" if bom else "utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


failures = []
touched = {}


def backup(path):
    if path not in touched:
        with io.open(path, "rb") as fh:
            touched[path] = fh.read()


def restore():
    for path, data in touched.items():
        with io.open(path, "wb") as fh:
            fh.write(data)


def has_bom(path):
    with io.open(path, "rb") as fh:
        return fh.read(3) == b"\xef\xbb\xbf"


try:
    def load():
        """每个负例都从磁盘重读。

        为什么必须重读：负例之间会改坏内存里的行对象（负例 1 去掉了加粗），
        负例 3 如果拿这份被改坏的数据写回文件，守卫报的就不是它该报的那条错——
        负例照样是红的，却测错了地方。负例对照的红必须是**因为要测的那件事**而红。
        """
        return read_rows(MATRIX), read_rows(EXEMPT)

    matrix_rows, exempt_rows = load()
    matrix_fields = list(matrix_rows[0].keys())
    exempt_fields = list(exempt_rows[0].keys())

    # ---- 负例 1：去掉某条 partial 行的加粗边界，守卫必须变红 ----
    victim = None
    for row in matrix_rows:
        if row.get("status", "").strip() == "partial" and "**" in row.get("note", ""):
            victim = row
            break
    if victim is None:
        failures.append("负例 1：矩阵里找不到带加粗边界的 partial 行")
    else:
        backup(MATRIX)
        victim["note"] = victim["note"].replace("**", "")
        write_rows(MATRIX, matrix_rows, matrix_fields, has_bom(MATRIX))
        code, out = run_guard()
        if code == 0:
            failures.append("负例 1：去掉 partial 行的加粗边界后守卫仍然放行（应该变红）")
        else:
            print("负例 1 按预期变红：")
            print("   " + first_problem(out))
        restore()

    # ---- 负例 2：删掉一条豁免登记，守卫必须变红 ----
    matrix_rows, exempt_rows = load()
    if not exempt_rows:
        failures.append("负例 2：豁免表是空的，无从下手")
    else:
        backup(EXEMPT)
        write_rows(EXEMPT, exempt_rows[1:], exempt_fields, has_bom(EXEMPT))
        code, out = run_guard()
        if code == 0:
            failures.append("负例 2：删掉一条豁免登记后守卫仍然放行（应该变红）")
        else:
            print("负例 2 按预期变红：")
            print("   " + first_problem(out))
        restore()

    # ---- 负例 3：豁免过期 —— 把对应矩阵行升成 aligned，守卫必须变红 ----
    matrix_rows, exempt_rows = load()
    if not exempt_rows:
        failures.append("负例 3：豁免表是空的，无从下手")
    else:
        row = exempt_rows[0]
        backup(MATRIX)
        backup(EXEMPT)
        changed = False
        for item in matrix_rows:
            if (item.get("capability", "").strip() == row["capability"].strip()
                    and item.get("tab", "").strip() == row["tab"].strip()
                    and item.get("command", "").strip() == row["command"].strip()):
                item["status"] = "aligned"
                changed = True
                break
        if not changed:
            failures.append("负例 3：在矩阵里找不到豁免登记的那一行（%s / %s / %s）"
                            % (row["capability"], row["tab"], row["command"]))
        else:
            write_rows(MATRIX, matrix_rows, matrix_fields, has_bom(MATRIX))
            code, out = run_guard()
            if code == 0:
                failures.append("负例 3：矩阵升成 aligned 后豁免没过期，守卫仍然放行（应该变红）")
            else:
                print("负例 3 按预期变红：")
                print("   " + first_problem(out))
            restore()

    # ---- 负例 4：塞一条合法豁免进去，不该变红（豁免机制要能正常使用）----
    matrix_rows, exempt_rows = load()
    backup(EXEMPT)
    extra = dict(exempt_rows[0])
    extra["reason"] = "负例对照用的合法豁免：登记了就不该报错"
    write_rows(EXEMPT, exempt_rows + [extra], exempt_fields, has_bom(EXEMPT))
    code, out = run_guard()
    if code != 0:
        failures.append("负例 4：登记一条合法豁免后守卫就变红了——豁免机制不可用：\n" + out)
    else:
        print("负例 4 按预期放行：豁免机制能正常使用")
    restore()
finally:
    restore()

code, out = run_guard()
if code != 0:
    failures.append("最终复检没过：\n" + out)
else:
    print("最终复检：守卫放行")

if failures:
    print("")
    print("负例对照失败：")
    for item in failures:
        print("   - " + item)
    sys.exit(1)
print("")
print("负例对照通过：守卫真的会拦人，豁免会过期，而且不会误伤")
