# -*- coding: utf-8 -*-
"""负例对照：证明 check-test-counts.py 真的会拦人，不是永远放行。

做法：把「当前口径」里的一处专项单测条数改坏（改成对不上的数），
门禁必须当场变红；然后原样还原，必须复绿。

还原用字节级备份，不靠 git —— 这些文档本身可能有未提交的改动，
git checkout 会把它们一起冲掉。

这个文件原来放在 build/ 下（build/ 在 .gitignore 里），新克隆跑不到，
所以提升到 scripts/ 并接进 scripts/ci.ps1：
门禁没有负例对照，就等于「永远绿灯」也可能没人知道。
"""
import io
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCORE = os.path.join(ROOT, "docs", "FOXIT_BENCHMARK.md")
GUARD = os.path.join(ROOT, "scripts", "check-test-counts.py")
BIN_DIR = os.path.join(ROOT, "build", "windows-debug", "bin")

COUNT_RE = re.compile(r"(`?)([A-Za-z][A-Za-z0-9]*Tests)\1\s*[（(]?\s*(\d+)\s*条")


def read_bytes(path):
    with io.open(path, "rb") as fh:
        return fh.read()


def run_guard():
    proc = subprocess.run(
        [sys.executable, GUARD, "--bin-dir", BIN_DIR],
        capture_output=True,
        cwd=ROOT,
        encoding="utf-8",
        errors="replace",
    )
    return proc.returncode, (proc.stdout or "") + (proc.stderr or "")


def restore():
    with io.open(SCORE, "wb") as fh:
        fh.write(original)


def first_current_claim(text):
    """评分卡第 2 节能力表里第一处「XxxTests N 条」——返回 (名, 条数, 位置)。"""
    start = text.index("### A. 阅读与显示")
    end = text.index("## 3. 汇总与达标线")
    body = text[start:end]
    match = COUNT_RE.search(body)
    if not match:
        return None
    return match.group(2), int(match.group(3)), start + match.start()


failures = []
original = read_bytes(SCORE)
try:
    text = original.decode("utf-8-sig")
    found = first_current_claim(text)
    if found is None:
        failures.append("负例 1：评分卡第 2 节里找不到「XxxTests N 条」")
    else:
        name, claimed, pos = found
        # 故意写成一个对不上的数（+1）。用 +1 而不是写死某个数：
        # 文档里的条数本来就会随实现变，写死会让负例自己失效 ——
        # 门禁还是绿的，却以为「证明过它会拦人」。
        broken = text[:pos] + text[pos:].replace(
            f"{claimed} 条", f"{claimed + 1} 条", 1
        )
        with io.open(SCORE, "wb") as fh:
            fh.write(broken.encode("utf-8"))
        code, out = run_guard()
        if code == 0:
            failures.append(
                f"负例 1：把 {name} 的条数改坏后门禁仍然放行（应该变红）"
            )
        else:
            print("负例 1 按预期变红：")
            hits = [l for l in out.splitlines() if l.strip().startswith("[X]")]
            print("   " + (hits[0].strip() if hits else out.strip().splitlines()[-1]))

    restore()

    # ---- 还原后必须复绿 ----
    code, out = run_guard()
    if code != 0:
        failures.append("还原后门禁没有复绿：\n" + out)
    else:
        print("还原后复绿")
finally:
    restore()

code, out = run_guard()
if code != 0:
    failures.append("最终复检没过：\n" + out)
else:
    print("最终复检：门禁放行")

if failures:
    print("")
    print("负例对照失败：")
    for item in failures:
        print("   - " + item)
    sys.exit(1)
print("")
print("负例对照通过：条数门真的会拦人")
