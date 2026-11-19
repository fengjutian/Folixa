# 拼写检查词表的来源与许可

这份文件说明内置词表（英文词表、中文词表、形近/音近字组、词组级错字表）
是从哪里来的、什么许可、体积多大、怎么复现。

生成脚本：`generate_dictionaries.py`（同目录）。
运行方式：

```
python src/pdf/ops/spell/generate_dictionaries.py [--cache <下载缓存目录>] [--out-dir <输出目录>]
```

脚本会校验来源文件的 SHA-256；哈希对不上直接失败，不会生成一份不一样的词表。

---

## 1. 英文词表

| 项 | 内容 |
| --- | --- |
| 来源 | SCOWL（Spell Checker Oriented Word Lists）的 `english-words.<频次档>` 与 `english-proper-names.<频次档>` |
| 本次取用的文件 | npm 包 `@neopass/wordlist@0.5.2` 里的 `package/scowl/words/`，内含 SCOWL 2018.04.16 的词频分档 |
| 上游地址 | http://wordlist.aspell.net/ （Git 源码：https://github.com/en-wl/wordlist） |
| 下载地址 | https://registry.npmjs.org/@neopass/wordlist/-/wordlist-0.5.2.tgz |
| SHA-256 | `29f13d889586c51deea87531350d1c697411c1b69454231e90dbe9658116c541` |
| 许可 | SCOWL 自带许可（见 npm 包内 `package/scowl/Copyright`）：*Permission to use, copy, modify, distribute and sell these word lists … provided that the above copyright notice appears in all copies and that both that copyright notice and this permission notice appear in supporting documentation.*（MIT 类，署名即可再分发） |
| 署名 | Copyright 2000-2016 by Kevin Atkinson；SCOWL 部分内容源自 Alan Beale 的 12Dicts 与 Geoff Kuenning 的 Ispell（BSD） |

取用的频次档：

- 已知词表（`EnglishWords.h`）：档位 10/20/35/40/50/55/60 + 专有名词档 50/60/70，共 **78 899 词**。
- 常用词表（`EnglishCommonWords.h`）：档位 10/20/35，共 **38 051 词**，
  只用来找「只差一个字母」的近邻词。

为什么分两档：词表越大，「只差一个字母」的偶然命中越多，误报率会跟着涨。
实测（见下）里常用词子集已经够用，而且近邻搜索快得多。

## 2. 中文词表

| 项 | 内容 |
| --- | --- |
| 来源 | jieba 的词频表 `jieba/dict.txt`（GitHub: https://github.com/fxsjy/jieba） |
| 下载地址 | https://raw.githubusercontent.com/fxsjy/jieba/master/jieba/dict.txt |
| SHA-256 | `7197c3211ddd98962b036cdf40324d1ea2bfaa12bd028e68faa70111a88e12a8` |
| 许可 | MIT（Copyright (c) 2013 Sun Junyi）；再分发时保留版权声明与许可声明即可 |

筛选口径：只留 2~4 个汉字、且词频 ≥ 50 的纯中文词，共 **45 476 词**。
另外导出一份「高频词」子集（词频 ≥ 300，14 244 词），
字级错字判定要求改完之后落在这个子集里。

> jieba 的词频表来自语料，语料里也混进了错别字（「自已」词频 263）。
> 这些错写形式会从词表里剔除，否则窗口匹配会认为「自已」是词，就永远不会报错。

## 3. 形近/音近字组与词组级错字表

| 表 | 文件 | 条数 | 来源 |
| --- | --- | --- | --- |
| 形近字组 | `chinese_confusables.txt`（`# ---- 形近 ----` 段） | 138 组 | 本仓库人工整理 |
| 音近字组 | `chinese_confusables.txt`（`# ---- 音近 ----` 段） | 354 组 | 本仓库人工整理 |
| 词组级错字 | `chinese_phrase_typos.txt` | 59 条 | 本仓库人工整理 |

这三张表**不是从任何第三方词表拷贝的**，是按公开的常见错字整理的，
整理口径写在各文件头部注释里。收录的每一条都是真实语料里高频出现的错写形式。

音近字组只作参考数据，不参与字级判定：音近字在中文里太容易撞词
（「于其」「存再」本来就是对的），按音近组做字级判定误报率高。
音近错字由词组级错字表兜（成语、惯用语、常用词整词对照）。

## 4. 体积

| 文件 | 字节 | 说明 |
| --- | --- | --- |
| `EnglishWords.h` | 737 702 | 78 899 个英文词 |
| `EnglishCommonWords.h` | 337 441 | 38 051 个常用英文词 |
| `ChineseWords.h` | 368 511 | 45 476 个中文词 |
| `ChineseCommonWords.h` | 107 622 | 14 244 个高频中文词 |
| `ChineseConfusables.h` | 7 401 | 形近/音近字组 + 词组级错字表 |
| 合计 | 1 558 677（约 1.49 MB） | 源码内嵌（应用不依赖外部词表文件） |

词表是编译进二进制的（`src/pdf/ops/SpellChecker.cpp` 直接 include 这些头文件），
应用运行时不需要额外文件；进程内只构建一次。

再加上 `SpellChecker.cpp` 里手写的高频词表（3 066 词，按使用频率排序，
含 PDF 领域词），运行时实际可用：英文 79 028 词、近邻搜索池 38 217 词、
中文 45 476 词、高频中文词 14 244 词。

## 5. 误报率实测

英文（近邻规则 + 词形还原）：

| 语料 | 样本量 | 被误报的词 | 误报率 |
| --- | --- | --- | --- |
| 《Alice in Wonderland》（Project Gutenberg，公有领域） | 20 000 词 | 73 | 0.36% |
| 本仓库 docs/*.md（技术英文） | 8 000 词 | 20 | 0.25% |

中文（形近字组 + 词组级错字表）：

| 语料 | 样本量 | 被误报的字 | 误报率 |
| --- | --- | --- | --- |
| 《岳阳楼记》节选（宋·范仲淹，公有领域） | 126 汉字 | 0 | 0 处/百字 |
| 自撰白话说明文 | 约 200 汉字 | 0 | 0 处/百字 |
| 常用词逐词核对（153 个词） | 153 词 | 0 | 0.00% |

## 6. 复现

```
python src/pdf/ops/spell/generate_dictionaries.py
```

会重新下载两个来源（校验 SHA-256）、重新生成五个头文件。
生成结果与仓库里的一致，说明词表没有被手工改过。
