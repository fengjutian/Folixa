# -*- coding: utf-8 -*-
"""负例对照：证明 check-permission-coverage.py 真的会拦人，不是永远放行。

做法与 `test-partial-boundaries-negative.py` 同一套路：字节级备份 → 故意改坏 →
守卫必须当场变红 → 原样还原 → 必须复绿。

**为什么用字节级备份而不是 git checkout**：PdfView.cpp 可能正有未提交的改动，
git checkout 会把那些改动一起冲掉。

四个负例分别打两件事，外加一条「别把守卫写成误伤」：

  1. 把某个入口的权限检查删掉 —— 守卫必须变红
     （这就是这轮发现的 27 个漏点的原样复现）。
  2. 删掉一条豁免登记 —— 守卫必须变红
     （证明豁免不是摆设，删了就有人裸奔）。
  3. 豁免指向一个不存在的函数 —— 守卫必须变红
     （证明豁免会过期，不能变成永久通行证）。
  4. 加一条合法豁免 —— **不该**变红：豁免机制本身要能正常使用。

用法：python scripts/test-permission-coverage-negative.py
"""
import io
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VIEW = os.path.join(ROOT, "src", "app", "editor", "PdfView.cpp")
EXEMPT = os.path.join(ROOT, "docs", "benchmark", "permission-coverage-exemptions.csv")
GUARD = os.path.join(ROOT, "scripts", "check-permission-coverage.py")

# 用来做负例的入口：它确实会改文档，而且原本挂着权限检查。
TARGET_FUNCTION = "swapCurrentWithNext"
# 拿来做「删掉豁免」的样本：它本来就该被豁免。
EXEMPT_SAMPLE = "recordContentEdit"


def run_guard():
    proc = subprocess.run(
        [sys.executable, GUARD],
        capture_output=True,
        cwd=ROOT,
        encoding="utf-8",
        errors="replace",
    )
    return proc.returncode, (proc.stdout or "") + (proc.stderr or "")


def read_bytes(path):
    with io.open(path, "rb") as handle:
        return handle.read()


def write_bytes(path, data):
    with io.open(path, "wb") as handle:
        handle.write(data)


def main():
    failures = 0
    view_backup = read_bytes(VIEW)
    exempt_backup = read_bytes(EXEMPT)

    def restore():
        write_bytes(VIEW, view_backup)
        write_bytes(EXEMPT, exempt_backup)

    try:
        # ---- 对照组：现状必须是绿的 ----
        code, out = run_guard()
        if code != 0:
            print("FAIL 对照组：守卫在现状下就报红")
            print(out)
            failures += 1
        else:
            print("PASS  对照组：现状全绿")

        # ---- 负例 1：删掉某个入口的权限检查 ----
        source = view_backup.decode("utf-8")
        marker = "if (!requirePermission(effectivePermissions().allowAssembly, tr(\"交换页面\"))) {"
        if marker not in source:
            print("FAIL 负例 1：没找到要破坏的权限检查（源码结构变了？）")
            failures += 1
        else:
            broken = source.replace(marker, "if (false) {", 1)
            write_bytes(VIEW, broken.encode("utf-8"))
            code, out = run_guard()
            restore()
            if code == 0:
                print("FAIL 负例 1：删掉权限检查后守卫仍然放行")
                failures += 1
            elif TARGET_FUNCTION not in out:
                print("FAIL 负例 1：守卫报红了，但没点名 %s" % TARGET_FUNCTION)
                print(out)
                failures += 1
            else:
                print("PASS  负例 1：删掉权限检查当场报红并点名 %s" % TARGET_FUNCTION)

        # ---- 负例 2：删掉一条豁免登记 ----
        lines = exempt_backup.decode("utf-8-sig").splitlines()
        kept = [line for line in lines if not line.startswith(EXEMPT_SAMPLE + ",")]
        if len(kept) == len(lines):
            print("FAIL 负例 2：豁免表里找不到 %s" % EXEMPT_SAMPLE)
            failures += 1
        else:
            write_bytes(EXEMPT, ("\n".join(kept) + "\n").encode("utf-8"))
            code, out = run_guard()
            restore()
            if code == 0:
                print("FAIL 负例 2：删掉豁免后守卫仍然放行")
                failures += 1
            else:
                print("PASS  负例 2：删掉豁免登记当场报红")

        # ---- 负例 3：豁免指向不存在的函数 ----
        text = exempt_backup.decode("utf-8-sig")
        text = text + "noSuchFunctionAtAll,不存在的函数\n"
        write_bytes(EXEMPT, text.encode("utf-8"))
        code, out = run_guard()
        restore()
        if code == 0:
            print("FAIL 负例 3：豁免指向不存在的函数，守卫仍然放行")
            failures += 1
        else:
            print("PASS  负例 3：豁免指向不存在的函数当场报红")

        # ---- 负例 4：加一条合法豁免，必须仍然绿 ----
        text = exempt_backup.decode("utf-8-sig")
        text = text + "swapCurrentWithNext,负例专用：临时豁免\n"
        write_bytes(EXEMPT, text.encode("utf-8"))
        # 同时把检查删掉——没有检查时才轮到豁免说话。
        source = view_backup.decode("utf-8")
        broken = source.replace(marker, "if (false) {", 1)
        write_bytes(VIEW, broken.encode("utf-8"))
        code, out = run_guard()
        restore()
        if code != 0:
            print("FAIL 负例 4：合法的豁免没有生效，守卫误伤了")
            print(out)
            failures += 1
        else:
            print("PASS  负例 4：合法豁免放行，守卫不误伤")

    finally:
        restore()

    # ---- 最终复检：还原后必须回到绿 ----
    code, out = run_guard()
    if code != 0:
        print("FAIL 复检：还原后守卫仍然报红")
        print(out)
        failures += 1
    else:
        print("PASS  复检：还原后全绿")

    if failures > 0:
        print("\n负例对照失败：%d 项" % failures)
        return 1
    print("\n负例对照通过：删检查 / 删豁免 / 豁免过期 三种情况都被拦，合法豁免放行")
    return 0


if __name__ == "__main__":
    sys.exit(main())
