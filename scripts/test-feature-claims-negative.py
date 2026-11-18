# -*- coding: utf-8 -*-
"""负例对照：证明 check-feature-claims.py 真的会拦人，不是永远放行。

做法：把一条 aligned 行的说明改坏（塞进降级措辞且不登记豁免），
守卫必须当场变红；然后原样还原，必须复绿。
还原用字节级备份，不靠 git —— 矩阵 CSV 本身可能有未提交的改动，git checkout 会把它一起冲掉。

这个文件原来放在 build/ 下（build/ 在 .gitignore 里），新克隆跑不到，
所以提升到 scripts/ 并接进 scripts/ci.ps1：
门禁没有负例对照，就等于「永远绿灯」也可能没人知道。
"""
import io
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV_PATH = os.path.join(ROOT, "docs", "benchmark", "foxit-features.csv")
EXEMPT_PATH = os.path.join(ROOT, "docs", "benchmark", "feature-claim-exemptions.csv")
GUARD = os.path.join(ROOT, "scripts", "check-feature-claims.py")

with io.open(CSV_PATH, "rb") as fh:
    original = fh.read()
with io.open(EXEMPT_PATH, "rb") as fh:
    original_exempt = fh.read()


def run_guard():
    proc = subprocess.run(
        [sys.executable, GUARD],
        capture_output=True,
        cwd=ROOT,
        encoding="utf-8",
        errors="replace",
    )
    return proc.returncode, (proc.stdout or "") + (proc.stderr or "")


def restore():
    with io.open(CSV_PATH, "wb") as fh:
        fh.write(original)
    with io.open(EXEMPT_PATH, "wb") as fh:
        fh.write(original_exempt)


failures = []
try:
    # ---- 负例 1：aligned 行出现降级措辞且没有豁免 ----
    text = original.decode("utf-8")
    lines = text.split("\n")
    target_index = None
    for index, line in enumerate(lines):
        fields = line.split(",")
        if len(fields) >= 5 and fields[3] == "aligned":
            target_index = index
            break
    if target_index is None:
        failures.append("负例 1 找不到 aligned 行")
    else:
        lines[target_index] = lines[target_index] + "（本行只导第一页）"
        with io.open(CSV_PATH, "wb") as fh:
            fh.write("\n".join(lines).encode("utf-8"))
        code, out = run_guard()
        if code == 0:
            failures.append("负例 1：把降级措辞塞进 aligned 行后守卫仍然放行（应该变红）")
        else:
            print("负例 1 按预期变红：")
            print("   " + [l for l in out.splitlines() if l.strip().startswith("-")][0].strip())

    restore()

    # ---- 负例 2：豁免过期（说明列里已经没有那个短语）----
    code, out = run_guard()
    if code != 0:
        failures.append("还原后守卫没有复绿：\n" + out)
    else:
        print("还原后复绿")

    # 把豁免清单里**第一条**豁免的短语改成一个不存在的短语。
    #
    # 为什么不能写死某个短语（原来写的是「只到」）：豁免清单会随实现变化，
    # 写死的那个短语一旦被删掉，replace 就成了空操作 —— 负例自己失效了，
    # 门禁还是绿的，却以为「证明过守卫会拦人」。所以这里从清单里现取第一条。
    exempt_text = original_exempt.decode("utf-8")
    exempt_lines = exempt_text.split("\n")
    broken_lines = list(exempt_lines)
    replaced = False
    for index, line in enumerate(exempt_lines):
        if not line.strip() or line.startswith("tab,"):
            continue
        fields = line.split(",")
        if len(fields) < 4:
            continue
        fields[3] = "不存在的短语"
        broken_lines[index] = ",".join(fields)
        replaced = True
        break
    if not replaced:
        failures.append("负例 2：豁免清单里找不到可改的短语")
    else:
        with io.open(EXEMPT_PATH, "wb") as fh:
            fh.write("\n".join(broken_lines).encode("utf-8"))
        code, out = run_guard()
        if code == 0:
            failures.append("负例 2：豁免失效后守卫仍然放行（应该变红）")
        else:
            print("负例 2 按预期变红：")
            print("   " + [l for l in out.splitlines() if l.strip().startswith("-")][0].strip())
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
print("负例对照通过：守卫真的会拦人")
