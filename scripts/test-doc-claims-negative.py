# -*- coding: utf-8 -*-
"""负例对照：证明 check-doc-claims.py 真的会拦人，不是永远放行。

做法与 `test-feature-claims-negative.py` 同一套路：字节级备份 → 故意改坏 →
守卫必须当场变红 → 原样还原 → 必须复绿。

**为什么用字节级备份而不是 git checkout**：文档可能正有未提交的改动（本文件
自己就是刚改完还没提交），git checkout 会把那些改动一起冲掉。

三个负例分别打三件事：

  1. 把登记过的过时措辞塞回它该出现的文件 —— 守卫必须变红
     （这就是 H5 那次真实矛盾的原样复现）。
  2. 塞到 `since` 标记之前 —— **不该**变红：历史轮次里的旧说法是「当时的账」，
     留着是存档的一部分。这条守的是「别把守卫写成误伤」。
  3. 抽掉一个表格行的收尾竖线 —— 守卫必须变红（结构检查不是摆设）。

用法：python scripts/test-doc-claims-negative.py
"""
import io
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REGISTRY = os.path.join(ROOT, "docs", "benchmark", "doc-claim-contradictions.csv")
GUARD = os.path.join(ROOT, "scripts", "check-doc-claims.py")


def read_registry():
    import csv
    with io.open(REGISTRY, "r", encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


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


registry = read_registry()
plain = [r for r in registry if not (r.get("since") or "").strip()]
scoped = [r for r in registry if (r.get("since") or "").strip()]

if not plain:
    print("登记表里没有「整份文件都算」的条目，负例无从下手")
    sys.exit(1)

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


def write_text(path, text):
    with io.open(path, "wb") as fh:
        fh.write(text.encode("utf-8-sig"))


try:
    # ---- 负例 1：把登记过的过时措辞塞回去，守卫必须变红 ----
    row = plain[0]
    target = os.path.join(ROOT, row["file"].strip())
    phrase = row["phrase"].strip()
    backup(target)
    with io.open(target, "r", encoding="utf-8-sig") as fh:
        original = fh.read()
    write_text(target, original + "\n" + phrase + "\n")
    code, out = run_guard()
    if code == 0:
        failures.append("负例 1：把「%s」塞回 %s 后守卫仍然放行（应该变红）"
                        % (phrase, row["file"]))
    else:
        print("负例 1 按预期变红：")
        print("   " + first_problem(out))
    restore()

    # ---- 负例 2：塞到 since 标记之前，不该变红（历史轮次允许留旧说法）----
    if scoped:
        row = scoped[0]
        target = os.path.join(ROOT, row["file"].strip())
        phrase = row["phrase"].strip()
        since = row["since"].strip()
        backup(target)
        with io.open(target, "r", encoding="utf-8-sig") as fh:
            original = fh.read()
        lines = original.split("\n")
        marker = None
        for index, line in enumerate(lines):
            if since in line:
                marker = index
                break
        if marker is None:
            failures.append("负例 2：找不到 since 标记「%s」" % since)
        else:
            lines.insert(0, phrase)
            write_text(target, "\n".join(lines))
            code, out = run_guard()
            if code != 0:
                failures.append("负例 2：把旧说法放到历史轮次里（since 之前）守卫就变红了"
                                "—— 这条守卫会误伤存档，应改成从标记起往后算：\n" + out)
            else:
                print("负例 2 按预期放行：历史轮次里的旧说法不算矛盾")
            restore()
    else:
        print("负例 2 跳过：登记表里没有带 since 的条目")

    # ---- 负例 3：抽掉一个表格行的收尾竖线，守卫必须变红 ----
    backup(os.path.join(ROOT, "docs", "FOXIT_BENCHMARK.md"))
    target = os.path.join(ROOT, "docs", "FOXIT_BENCHMARK.md")
    with io.open(target, "r", encoding="utf-8-sig") as fh:
        lines = fh.read().split("\n")
    victim = None
    for index, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("|") and stripped.endswith("|") and not set(stripped) <= set("|-: "):
            victim = index
            break
    if victim is None:
        failures.append("负例 3：找不到可抽掉收尾竖线的表格行")
    else:
        lines[victim] = lines[victim].rstrip().rstrip("|")
        write_text(target, "\n".join(lines))
        code, out = run_guard()
        if code == 0:
            failures.append("负例 3：抽掉表格行的收尾竖线后守卫仍然放行（应该变红）")
        else:
            print("负例 3 按预期变红：")
            print("   " + first_problem(out))
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
print("负例对照通过：守卫真的会拦人，而且不会误伤历史存档")
