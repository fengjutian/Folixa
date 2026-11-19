#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成内置词表头文件（可复现）。

用法：
    python src/pdf/ops/spell/generate_dictionaries.py [--cache <dir>] [--out-dir <dir>]

做什么：
    1. 取两份来源（都带固定 SHA-256，来源变了直接报错，不生成不一样的词表）：
       - 英文：SCOWL 的 english-words.<频次档> 词频分档文件（随 @neopass/wordlist 分发）。
       - 中文：jieba 的 dict.txt（MIT）。
    2. 按下面的阈值筛成两份内置词表，写成 C++ 头文件里的原始字符串常量。
    3. 把手工维护的形近/音近字组与词组级错字表一起转成头文件，
       并校验「错词」确实不在中文词表里（否则会误报正确写法）。

来源、许可、体积见同目录 SOURCES.md。
"""

from __future__ import annotations

import argparse
import hashlib
import io
import os
import re
import sys
import tarfile
import tempfile
import urllib.request

# --------------------------------------------------------------------------
# 来源定义：URL + 期望的 SHA-256。哈希对不上就直接失败——
# 词表是拼写检查的基础，悄悄换一份来源比报错糟糕得多。
# --------------------------------------------------------------------------
SOURCES = {
    "scowl": {
        "url": "https://registry.npmjs.org/@neopass/wordlist/-/wordlist-0.5.2.tgz",
        "sha256": "29f13d889586c51deea87531350d1c697411c1b69454231e90dbe9658116c541",
        "member_prefix": "package/scowl/",
    },
    "jieba": {
        "url": "https://raw.githubusercontent.com/fxsjy/jieba/master/jieba/dict.txt",
        "sha256": "7197c3211ddd98962b036cdf40324d1ea2bfaa12bd028e68faa70111a88e12a8",
    },
}

# 英文词频档：SCOWL 的档位越小越常用（10 = 最常用）。
#   COMMON_TIERS  -> 拿来找「只差一个字母」的近邻词，也就是常用词集合。
#   KNOWN_TIERS   -> 拿来做「这个词认不认识」的完整集合。
#   NAME_TIERS    -> 专有名词（人名地名）：只进已知词集合。
#                     不进常用词集合——专有名词离常用词太近，容易互相误命中；
#                     但要让检查器认识它们，否则 "Smith" 这种词会被当成
#                     「句中大写异常」误报。
# 档位的取舍靠 sim 脚本实测的误报率定，不是拍脑袋。
COMMON_TIERS = ("10", "20", "35")
KNOWN_TIERS = ("10", "20", "35", "40", "50", "55", "60")
NAME_TIERS = ("50", "60", "70")

# 中文词表：只收 2~4 个汉字、且语料里足够常见的词。
CHINESE_MIN_LEN = 2
CHINESE_MAX_LEN = 4
CHINESE_MIN_FREQ = 50
# 「常见词」门槛：字级错字判定要求改完之后是常见词。
# 门槛太高会漏掉生僻词的错字，太低会把「洋楼」「分文」这种偶合词当成正确写法，
# 误报率会明显上升。300 是实测折中的位置（见 SOURCES.md）。
CHINESE_COMMON_FREQ = 300

CJK_ONLY = re.compile(r"^[一-鿿]+$")
ASCII_WORD = re.compile(r"^[a-z]+$")

HERE = os.path.dirname(os.path.abspath(__file__))


def sha256_of(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fetch(name: str, cache_dir: str) -> str:
    spec = SOURCES[name]
    target = os.path.join(cache_dir, os.path.basename(spec["url"]))
    if not os.path.exists(target):
        sys.stderr.write("下载 %s ...\n" % spec["url"])
        with urllib.request.urlopen(spec["url"], timeout=180) as response:
            data = response.read()
        with open(target, "wb") as handle:
            handle.write(data)
    actual = sha256_of(target)
    if actual != spec["sha256"]:
        raise SystemExit(
            "来源校验失败：%s\n  期望 sha256=%s\n  实际 sha256=%s\n"
            "词表来源变了必须人工确认后再更新 SOURCES.md 与这里的哈希。"
            % (name, spec["sha256"], actual)
        )
    return target


def load_scowl_tiers(tarball: str, cache_dir: str) -> dict:
    """从 tarball 里取出各频次档的英文词表。"""
    extract_dir = os.path.join(cache_dir, "scowl")
    marker = os.path.join(extract_dir, "package", "scowl", "VERSION")
    if not os.path.exists(marker):
        with tarfile.open(tarball) as archive:
            archive.extractall(extract_dir)
    words_dir = os.path.join(extract_dir, "package", "scowl", "words")
    tiers = {}
    for tier in sorted({t for t in COMMON_TIERS} | {t for t in KNOWN_TIERS}, key=int):
        path = os.path.join(words_dir, "english-words.%s" % tier)
        if not os.path.exists(path):
            raise SystemExit("SCOWL 词频档缺失：%s" % path)
        words = set()
        with io.open(path, encoding="utf-8", errors="ignore") as handle:
            for line in handle:
                word = line.strip()
                if "/" in word:
                    word = word.split("/")[0]
                word = word.strip().lower()
                if ASCII_WORD.match(word):
                    words.add(word)
        tiers[tier] = words
    names = {}
    for tier in NAME_TIERS:
        path = os.path.join(words_dir, "english-proper-names.%s" % tier)
        if not os.path.exists(path):
            raise SystemExit("SCOWL 专有名词表缺失：%s" % path)
        words = set()
        with io.open(path, encoding="utf-8", errors="ignore") as handle:
            for line in handle:
                word = line.strip()
                if "/" in word:
                    word = word.split("/")[0]
                word = word.strip().lower()
                if ASCII_WORD.match(word):
                    words.add(word)
        names[tier] = words
    return tiers, names


def load_jieba_words(path: str) -> dict:
    """jieba 词频表 -> {词: 词频}，只留 2~4 个汉字的纯中文词。"""
    frequencies = {}
    with io.open(path, encoding="utf-8", errors="ignore") as handle:
        for line in handle:
            parts = line.split()
            if len(parts) < 2:
                continue
            word = parts[0]
            if not (CHINESE_MIN_LEN <= len(word) <= CHINESE_MAX_LEN):
                continue
            if not CJK_ONLY.match(word):
                continue
            try:
                frequency = int(parts[1])
            except ValueError:
                continue
            if frequency < CHINESE_MIN_FREQ:
                continue
            frequencies[word] = frequency
    return frequencies


def read_lines(path: str) -> list:
    rows = []
    with io.open(path, encoding="utf-8") as handle:
        for raw in handle:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            rows.append(line)
    return rows


def write_blob(path: str, name: str, words, header_lines: list) -> None:
    """把词表写成头文件里的原始字符串常量。"""
    body = "\n".join(words)
    if ')SPELL"' in body:
        raise SystemExit("词表内容与原始字符串定界符冲突")
    with io.open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("#pragma once\n")
        handle.write("// 自动生成，勿手改：改词表请改生成脚本或来源文件后重新生成。\n")
        for line in header_lines:
            handle.write("// %s\n" % line)
        handle.write("namespace aipdf::pdf::spell {\n\n")
        handle.write('inline constexpr char %s[] = R"SPELL(\n' % name)
        handle.write(body)
        handle.write('\n)SPELL";\n\n')
        handle.write("}  // namespace aipdf::pdf::spell\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    # 下载缓存默认放临时目录：仓库里不留下载产物。
    parser.add_argument("--cache", default=os.path.join(tempfile.gettempdir(), "aipdf-spell-cache"))
    parser.add_argument("--out-dir", default=HERE)
    args = parser.parse_args()
    os.makedirs(args.cache, exist_ok=True)

    scowl_tarball = fetch("scowl", args.cache)
    jieba_path = fetch("jieba", args.cache)
    tiers, name_tiers = load_scowl_tiers(scowl_tarball, args.cache)

    common = set()
    for tier in COMMON_TIERS:
        common |= tiers[tier]
    known = set()
    for tier in KNOWN_TIERS:
        known |= tiers[tier]
    for tier in NAME_TIERS:
        known |= name_tiers[tier]

    # 频次档小的在前：建议列表按这个次序出，高频词排前面。
    ordered_known = []
    seen = set()
    for tier in sorted(KNOWN_TIERS, key=int):
        for word in sorted(tiers[tier]):
            if word not in seen:
                seen.add(word)
                ordered_known.append(word)
    for tier in sorted(NAME_TIERS, key=int):
        for word in sorted(name_tiers[tier]):
            if word not in seen:
                seen.add(word)
                ordered_known.append(word)

    chinese = load_jieba_words(jieba_path)

    # 词组级错字表：错词必须在中文词表里被剔除，否则「自己」这种正确写法会被报错。
    phrase_rows = read_lines(os.path.join(HERE, "chinese_phrase_typos.txt"))
    wrong_forms = set()
    for row in phrase_rows:
        if "|" not in row:
            raise SystemExit("词组级错字表格式不对（缺 |）：%s" % row)
        wrong, right = row.split("|", 1)
        wrong, right = wrong.strip(), right.strip()
        if not wrong or not right:
            raise SystemExit("词组级错字表有空项：%s" % row)
        if wrong == right:
            raise SystemExit("词组级错字表不能把词改成它自己：%s" % row)
        if not CJK_ONLY.match(wrong) or not CJK_ONLY.match(right):
            raise SystemExit("词组级错字表只收纯中文词：%s" % row)
        wrong_forms.add(wrong)

    # 这些写法在语料里本身就是错的（语料也收录了错别字），必须从词表里剔除，
    # 否则窗口匹配会认为「自已」「按装」是词，就永远不会报错。
    wrong_forms.update({"自已", "按装", "既使", "即然", "报歉", "以经", "做业", "在见", "记的",
                        "决对", "象素", "帐单", "座落", "侯车"})
    purged = [word for word in wrong_forms if word in chinese]
    for word in purged:
        del chinese[word]

    ordered_chinese = [word for word, _ in sorted(chinese.items(), key=lambda kv: (-kv[1], kv[0]))]

    # 形近/音近字组：分组类型由注释里的分段标记决定（形近 / 音近）。
    # 两类的判定口径不一样，所以要分开：
    #   形近组 -> 参与字级判定（改一个字以后整个窗口能对上常见词才算错字）；
    #   音近组 -> 只做参考。音近字在中文里太常见（「于其」「存再」这种组合
    #             本来就是对的），字级判定按它跑误报率高，音近错字交给词组级
    #             错字表兜（见 chinese_phrase_typos.txt）。
    shape_groups = []
    homophone_groups = []
    current = shape_groups
    with io.open(os.path.join(HERE, "chinese_confusables.txt"), encoding="utf-8") as handle:
        for raw in handle:
            line = raw.strip()
            if not line:
                continue
            if line.startswith("#"):
                if "形近" in line:
                    current = shape_groups
                elif "音近" in line:
                    current = homophone_groups
                continue
            members = line.split()
            if len(members) < 2:
                raise SystemExit("形近/音近字组至少要有两个字：%s" % line)
            for member in members:
                if not CJK_ONLY.match(member):
                    raise SystemExit("形近/音近字组只收汉字：%s" % line)
            if len(set(members)) != len(members):
                raise SystemExit("形近/音近字组里有重复字：%s" % line)
            current.append(members)
    groups = shape_groups + homophone_groups

    # 校验：词组级错字表的「对词」应该在中文词表里，否则整词对照没有意义。
    missing_right = [right for row in phrase_rows
                      for right in [row.split("|", 1)[1].strip()] if right not in chinese]
    if missing_right:
        sys.stderr.write("警告：这些对词不在中文词表里（可能是词表阈值太严）：%s\n"
                         % "、".join(missing_right[:20]))

    write_blob(
        os.path.join(args.out_dir, "EnglishWords.h"),
        "kEnglishWords",
        ordered_known,
        [
            "英文内置词表：SCOWL 频次档 %s 的并集（按频次档排序）。" % "、".join(KNOWN_TIERS),
            "来源与许可见 SOURCES.md。",
        ],
    )
    write_blob(
        os.path.join(args.out_dir, "EnglishCommonWords.h"),
        "kEnglishCommonWords",
        [w for w in ordered_known if w in common],
        [
            "英文常用词表：SCOWL 频次档 %s 的并集，只用来找近邻词。" % "、".join(COMMON_TIERS),
            "来源与许可见 SOURCES.md。",
        ],
    )
    write_blob(
        os.path.join(args.out_dir, "ChineseWords.h"),
        "kChineseWords",
        ordered_chinese,
        [
            "中文常用词表：jieba 词频表中 2~4 字、词频 >= %d 的纯中文词。" % CHINESE_MIN_FREQ,
            "来源与许可见 SOURCES.md。",
        ],
    )
    write_blob(
        os.path.join(args.out_dir, "ChineseCommonWords.h"),
        "kChineseCommonWords",
        [word for word in ordered_chinese if chinese[word] >= CHINESE_COMMON_FREQ],
        [
            "中文高频词表：jieba 词频 >= %d 的词，字级错字判定要求改完之后是这里的词。" % CHINESE_COMMON_FREQ,
            "来源与许可见 SOURCES.md。",
        ],
    )

    # 形近/音近字组 + 词组级错字表合成一个头文件。
    out_path = os.path.join(args.out_dir, "ChineseConfusables.h")
    with io.open(out_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("#pragma once\n")
        handle.write("// 自动生成，勿手改：改表请改 chinese_confusables.txt / chinese_phrase_typos.txt 后重新生成。\n")
        handle.write("//\n")
        handle.write("// 形近字组：每行一组，组内用空格分隔；组里的字字形相近、容易写错。\n")
        handle.write("//           只有这一类参与字级判定（误报率低）。\n")
        handle.write("// 音近字组：读音相同或相近的字，只作参考数据，不参与字级判定：\n")
        handle.write("//           音近字在中文里太常见，「于其」「存再」这种组合本来就是对的，\n")
        handle.write("//           按音近组判会到处误报。音近错字由词组级错字表兜。\n")
        handle.write("// 词组级错字表：每行「错词|对词」。\n")
        handle.write("//\n")
        handle.write("// 这两张表是人工整理的（见 SOURCES.md），不是从来源不明的地方抄来的。\n")
        handle.write("namespace aipdf::pdf::spell {\n\n")
        handle.write('inline constexpr char kShapeConfusableGroups[] = R"SPELL(\n')
        handle.write("\n".join(" ".join(group) for group in shape_groups))
        handle.write('\n)SPELL";\n\n')
        handle.write('inline constexpr char kHomophoneConfusableGroups[] = R"SPELL(\n')
        handle.write("\n".join(" ".join(group) for group in homophone_groups))
        handle.write('\n)SPELL";\n\n')
        handle.write('inline constexpr char kPhraseTypos[] = R"SPELL(\n')
        handle.write("\n".join(phrase_rows))
        handle.write('\n)SPELL";\n\n')
        handle.write("}  // namespace aipdf::pdf::spell\n")

    print("英文已知词：%d（常用 %d）" % (len(ordered_known), len(common)))
    print("中文词：%d（剔除了 %d 个错写形式）" % (len(ordered_chinese), len(purged)))
    print("形近/音近字组：%d；词组级错字：%d" % (len(groups), len(phrase_rows)))
    for path in ("EnglishWords.h", "EnglishCommonWords.h", "ChineseWords.h", "ChineseConfusables.h"):
        full = os.path.join(args.out_dir, path)
        print("  %-26s %8d bytes" % (path, os.path.getsize(full)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
