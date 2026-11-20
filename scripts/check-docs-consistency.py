# -*- coding: utf-8 -*-
"""交付前的文档一致性终检：把三份文档里的数字互相对一遍。

任何一项对不上就退出码非零 —— 这道检查防止「文档自己和自己打架」。

对账的四条线：
  1. 命令矩阵 CSV（唯一数据源）里的条数与对齐数；
  2. 评分卡第 3 节的汇总行（已具备 / 合计 / 未做 / 已达对齐及以上）；
  3. 评分卡第 7 节的合计行与命令级口径段；
  4. 版本审计里**最新一轮**的汇总行（第七次优先）。

外加两条：
  5. 欠账清单（R4 第②类缺口）：每一项都要在评分卡里、状态必须是「对齐」、
     欠什么与补齐后的验收方式都不能空；
  6. 第 3 节「欠账」行列的编号必须与欠账清单逐条对得上。

这个文件原来放在 build/ 下，而 build/ 在 .gitignore 里 ——
意味着它只是本机私货，新克隆根本跑不到，所以提升到 scripts/ 让它进仓库、进流水线。
"""
import csv
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCORE = os.path.join(ROOT, "docs", "FOXIT_BENCHMARK.md")
AUDIT = os.path.join(ROOT, "docs", "VERSION_AUDIT.md")
CSV_PATH = os.path.join(ROOT, "docs", "benchmark", "foxit-features.csv")

problems = []

# ---- 矩阵 ----
with io.open(CSV_PATH, "r", encoding="utf-8-sig", newline="") as handle:
    rows = list(csv.DictReader(handle))
counter = {}
for row in rows:
    counter[row["status"]] = counter.get(row["status"], 0) + 1
total = len(rows)
aligned = counter.get("aligned", 0)
print("矩阵：%d 条，aligned %d，对齐率 %.1f%%" % (total, aligned, 100.0 * aligned / total))

# ---- 评分卡 ----
score = io.open(SCORE, "r", encoding="utf-8-sig").read()
states = {}
for line in score.splitlines():
    match = re.match(r"^\|\s*([A-L]\d+)\s*\|", line)
    if not match:
        continue
    columns = [cell.strip() for cell in line.strip().strip("|").split("|")]
    if len(columns) < 6:
        continue
    states[match.group(1)] = columns[3]

ready = sum(1 for s in states.values() if s.replace("*", "").startswith("已具备"))
beyond = sum(1 for s in states.values() if s.replace("*", "").startswith("超越"))
align = sum(1 for s in states.values() if s.replace("*", "").startswith("对齐"))
unfinished = sum(1 for s in states.values() if s.replace("*", "").startswith("未做"))
items = len(states)
print("评分卡：%d 项，已具备 %d，超越 %d，对齐 %d，未做 %d" % (items, ready, beyond, align, unfinished))

if ready + beyond + align + unfinished != items:
    problems.append("评分卡状态和 != 项目数")

# 第 3 节的汇总行
def summary_row(label):
    match = re.search(r"^\| " + re.escape(label) + r" \| (\d+) \|", score, re.M)
    return int(match.group(1)) if match else None

s_ready = summary_row("已具备")
s_align_above = re.search(r"已达「对齐」及以上 (\d+)", score)
s_total = summary_row("评分项合计")
s_unfinished = summary_row("未做（未实现）")
if s_ready != ready:
    problems.append("§3「已具备」写的 %s，实际 %d" % (s_ready, ready))
if s_total != items:
    problems.append("§3「评分项合计」写的 %s，实际 %d" % (s_total, items))
if s_unfinished != unfinished:
    problems.append("§3「未做」写的 %s，实际 %d" % (s_unfinished, unfinished))
if s_align_above and int(s_align_above.group(1)) != ready + beyond:
    problems.append("§3「已达对齐及以上」写的 %s，实际 %d" % (s_align_above.group(1), ready + beyond))

# 第 7 节的合计
m7 = re.search(r"\| \*\*合计\*\* \| \*\*(\d+) / (\d+)（(\d+)%）\*\*", score)
if not m7:
    problems.append("§7 合计行找不到")
else:
    if int(m7.group(1)) != ready + beyond:
        problems.append("§7 合计 %s，实际已具备+超越 %d" % (m7.group(1), ready + beyond))
    if int(m7.group(2)) != items:
        problems.append("§7 总数 %s，实际 %d" % (m7.group(2), items))
    expect_pct = round((ready + beyond) * 100.0 / items)
    if int(m7.group(3)) != expect_pct:
        problems.append("§7 百分比 %s，实际应为 %d" % (m7.group(3), expect_pct))

# §7 命令级那段
mc = re.search(r"\*\*命令级的对齐率是另一个口径，不要混用\*\*：(\d+) 条福昕命令里 (\d+) 条完全对齐、(\d+) 条部分、\n(\d+) 条未做，\*\*对齐率 (\d+)%", score)
if not mc:
    problems.append("§7 命令级口径段找不到")
else:
    if int(mc.group(1)) != total:
        problems.append("§7 命令总数 %s，实际 %d" % (mc.group(1), total))
    if int(mc.group(2)) != aligned:
        problems.append("§7 对齐数 %s，实际 %d" % (mc.group(2), aligned))
    if int(mc.group(3)) != counter.get("partial", 0):
        problems.append("§7 部分数 %s，实际 %d" % (mc.group(3), counter.get("partial", 0)))
    if int(mc.group(4)) != counter.get("missing", 0):
        problems.append("§7 未做数 %s，实际 %d" % (mc.group(4), counter.get("missing", 0)))
    if int(mc.group(5)) != round(100.0 * aligned / total):
        problems.append("§7 对齐率 %s，实际 %d" % (mc.group(5), round(100.0 * aligned / total)))

# ---- VERSION_AUDIT ----
audit = io.open(AUDIT, "r", encoding="utf-8-sig").read()
# 取**最新一轮**的汇总行（第十九次优先；更早的是历史记录，不再拿它对账）。
ma = None
for round_word in (u"第二十次", u"第十九次", u"第十八次", u"第十七次", u"第十六次", u"第十五次", u"第十四次", u"第十三次", u"第十二次", u"第十一次", u"第十次", u"第九次", u"第八次", u"第七次", u"第六次", u"第五次", u"第四次"):
    found = re.search(round_word + u"（本轮）增至 (\d+) / (\d+) 与 (\d+)%（(\d+) / (\d+)）", audit)
    if found:
        ma = found
        break
if not ma:
    problems.append("VERSION_AUDIT 最新一轮汇总找不到")
else:
    if int(ma.group(1)) != ready:
        problems.append("VERSION_AUDIT「已具备」%s，实际 %d" % (ma.group(1), ready))
    if int(ma.group(2)) != items:
        problems.append("VERSION_AUDIT 总数 %s，实际 %d" % (ma.group(2), items))
    if int(ma.group(4)) != aligned:
        problems.append("VERSION_AUDIT 对齐数 %s，实际 %d" % (ma.group(4), aligned))
    if int(ma.group(5)) != total:
        problems.append("VERSION_AUDIT 命令总数 %s，实际 %d" % (ma.group(5), total))

# ---- 欠账清单（R4 第②类缺口）----
DEBT_CSV = os.path.join(ROOT, "docs", "benchmark", "scorecard-debt.csv")
if os.path.exists(DEBT_CSV):
    with io.open(DEBT_CSV, "r", encoding="utf-8-sig", newline="") as handle:
        debt_rows = list(csv.DictReader(handle))
    debt_count = len(debt_rows)
    print("欠账清单：%d 项" % debt_count)
    for row in debt_rows:
        code = row.get("code", "").strip()
        if code not in states:
            problems.append("欠账清单里的 %s 不在评分卡里" % code)
            continue
        if not states[code].replace("*", "").startswith("对齐"):
            problems.append("欠账清单里的 %s 现在是「%s」，不再是「对齐」" % (code, states[code]))
        if not row.get("acceptance", "").strip():
            problems.append("欠账清单里的 %s 没写补齐后的验收方式" % code)
        if not row.get("owed", "").strip():
            problems.append("欠账清单里的 %s 没写欠什么" % code)
    # 第 3 节的欠账行要与清单对得上
    m_debt = re.search(r"^\| 欠账（对齐但承诺范围未达成） \| (\d+) \| ([^|]+) \|", score, re.M)
    if not m_debt:
        problems.append("第 3 节「欠账」行找不到")
    else:
        if int(m_debt.group(1)) != debt_count:
            problems.append("第 3 节「欠账」写的 %s，欠账清单实际 %d 条" % (m_debt.group(1), debt_count))
        # 说明格里混着链接文本，按编号模式取更稳（按「/」切会被链接里的斜杠切坏）。
        listed = re.findall(r"[A-L]\d+", m_debt.group(2))
        if sorted(listed) != sorted(row.get("code", "").strip() for row in debt_rows):
            problems.append("第 3 节「欠账」行列的编号与欠账清单对不上")
else:
    problems.append("欠账清单不存在：docs/benchmark/scorecard-debt.csv")

# ---- 要修改的功能列表（docs/FEATURE_MODIFICATION_BACKLOG.md）----
BACKLOG = os.path.join(ROOT, "docs", "FEATURE_MODIFICATION_BACKLOG.md")
if os.path.exists(BACKLOG):
    backlog = io.open(BACKLOG, "r", encoding="utf-8-sig").read()
    m_total = re.search(r"命令矩阵共 \*\*(\d+)\*\* 条：aligned \*\*(\d+)\*\*、partial \*\*(\d+)\*\*", backlog)
    if not m_total:
        problems.append("待改清单：开头那句总数行找不到")
    else:
        if int(m_total.group(1)) != total:
            problems.append("待改清单：命令总数 %s，实际 %d" % (m_total.group(1), total))
        if int(m_total.group(2)) != aligned:
            problems.append("待改清单：aligned %s，实际 %d" % (m_total.group(2), aligned))
        if int(m_total.group(3)) != counter.get("partial", 0):
            problems.append("待改清单：partial %s，实际 %d" % (m_total.group(3), counter.get("partial", 0)))
    m_debt_head = re.search(r"## 第一梯队：欠账 (\d+) 项", backlog)
    if not m_debt_head:
        problems.append("待改清单：第一梯队标题找不到")
    elif int(m_debt_head.group(1)) != debt_count:
        problems.append("待改清单：第一梯队 %s 项，欠账清单实际 %d 条" % (m_debt_head.group(1), debt_count))
    m_partial_head = re.search(r"## 第二梯队：矩阵 partial (\d+) 项", backlog)
    if not m_partial_head:
        problems.append("待改清单：第二梯队标题找不到")
    elif int(m_partial_head.group(1)) != counter.get("partial", 0):
        problems.append("待改清单：第二梯队 %s 条，矩阵 partial 实际 %d 条"
                        % (m_partial_head.group(1), counter.get("partial", 0)))
    print("待改清单：欠账 %d 项 / partial %d 条" % (debt_count, counter.get("partial", 0)))
else:
    problems.append("待改清单不存在：docs/FEATURE_MODIFICATION_BACKLOG.md")

print()
if problems:
    print("!! 一致性检查未通过：")
    for item in problems:
        print("   - " + item)
    sys.exit(1)
print("三份文档的数字互相对得上")
