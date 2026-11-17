# 性能与兼容语料（K4 / K5 / K6）

评分卡 `docs/FOXIT_BENCHMARK.md` 的 K 类里，K3（滚动帧率）、K4（1 GB 级文件）、
K5（畸形文件容错）、K6（500 页内存）四项需要**语料**和**可重复的采样**。
这一目录配套三个脚本：

| 文件 | 作用 |
|---|---|
| `scripts/gen-compat-corpus.py` | 语料生成器（纯 Python 标准库） |
| `scripts/verify-corpus-tolerance.ps1` | K5：成批打开畸形语料，要求零崩溃 |
| `scripts/verify-perf-budget.ps1` | K3 / K4 / K6：帧率、大文件、峰值内存 |

---

## 一、生成语料

```powershell
# 畸形语料 1000 份 + 对照样本 + 500 页内存基准文档
python scripts\gen-compat-corpus.py --out build\corpus --count 1000

# 额外生成 1 GiB 大文件（K4）；很慢，默认不生成
python scripts\gen-compat-corpus.py --out build\corpus --count 1000 --with-large
```

产物布局：

```
build/corpus/
  malformed/          K5：m0001-truncate-01.pdf …（每份旁边一个同名 .meta.json）
  baseline/           对照样本：baseline-valid.pdf（完全合法，必须能打开）
  large/              K4：large-1gb.pdf（--with-large 才生成）
  pages/              K6：pages-500.pdf
  manifest.json       全部产物的 sha256 清单（确定性基准）
```

参数：

| 参数 | 默认 | 说明 |
|---|---|---|
| `--out` | `build/corpus` | 输出目录 |
| `--count` | `1000` | 畸形语料份数 |
| `--seed` | `20261116` | 随机种子；同一种子产物逐字节一致 |
| `--with-large` | 关 | 是否生成大文件语料 |
| `--large-gb` | `1.0` | 大文件目标大小（GiB） |
| `--pages` | `500` | K6 文档页数 |
| `--page-image-size` | `256` | K6 每页图片边长（像素） |
| `--quiet` | 关 | 少打印 |

### 确定性

同一条命令跑两次，产物逐字节一致（`manifest.json` 的 sha256 也一致）：
随机数来自固定种子，像素来自 SHAKE-128 伪随机流，文件里**不写时间戳**。
换种子会改变产物——实测 100 份里 85 份内容不同（剩下的是 0 字节、只有 `%%EOF`
这类内容本来就与种子无关的样本）。

---

## 二、损坏类别清单

10 大类、30 种具体写法。`expectation` 是**建议**，硬要求对所有样本都一样：
**进程不能崩，打不开时必须给出可读的中文原因**。

| 类别 | 变体 | 怎么造的 | 期望 |
|---|---|---|---|
| 截断 | `truncate-01/05/20/50/90` | 在正常 PDF 的 1%/5%/20%/50%/90% 处砍断 | 01 拒绝，其余 either（90 期望能开） |
| xref 损坏 | `xref-offset` | `startxref` 的偏移量 +13 | either |
| | `xref-missing` | 整段 `startxref … %%EOF` 被删 | either |
| | `xref-nonnumeric` | `startxref` 填 `not-a-number` | either |
| | `xref-entry-wrong` | xref 表里某个对象的偏移 +7 | either |
| 对象头损坏 | `obj-header-case` | `N 0 obj` → `N 0 oBj` | either |
| | `obj-no-endobj` | 删掉一个 `endobj` | either |
| | `obj-bad-generation` | `N 0 obj` → `N 7 obj` | either |
| 页面树损坏 | `pagetree-kids-missing` | `/Kids` 指向不存在的对象号 | 拒绝 |
| | `pagetree-count-mismatch` | `/Count` 与实际页数差 6 | either |
| | `pagetree-self-cycle` | `/Kids` 指向自己 | 拒绝 |
| 嵌套/递归 | `pagetree-nested-cycle` | 两个 `Pages` 节点互相指向 | 拒绝 |
| 字典语法错误 | `dict-missing-close` | 少一个 `>>` | either |
| | `dict-value-not-object` | `/Pages 2 0 R` → `/Pages /NotAnIndirectObject` | either |
| | `dict-invalid-key` | 键名含非法字符 `%` | either |
| 流长度不符 | `stream-length-mismatch` | `/Length` 比实际大 4096 | either |
| | `stream-no-endstream` | 删掉 `endstream` | either |
| | `stream-garbage` | 流内容换成随机字节（长度不变） | either |
| 资源损坏 | `resource-font-missing` | `/Font` 指向不存在的对象 | 期望能开 |
| | `image-width-zero` | 图像 `/Width 0` | either |
| | `image-width-huge` | 图像 `/Width 100000` | either |
| 加密损坏 | `encrypt-broken` | trailer 加 `/Encrypt`，但字典缺 `/O`、`/U` | 拒绝 |
| 空壳文件 | `empty-file` | 0 字节 | 拒绝 |
| | `whitespace-only` | 只有空白 | 拒绝 |
| | `eof-only` | 只有 `%%EOF` | 拒绝 |
| | `header-only` | 只有 `%PDF-1.7` | 拒绝 |

每份语料的 `.meta.json` 记录：损坏类别（中英）、变体名、构造方式、期望行为、
字节数、sha256、生成参数（种子、序号）。

底稿是**每份都不一样**的合法 PDF（页数 2~5、每页一行文本 + 一张 8×8 的图），
所以 1000 份语料覆盖到的字节组合也不一样。

---

## 三、K5：容错验收

```powershell
python scripts\gen-compat-corpus.py --out build\corpus --count 1000
powershell -File scripts\verify-corpus-tolerance.ps1 -CorpusDir build\corpus\malformed
```

每份语料断言三件事：

1. **进程不崩**：没有非预期退出，也没有「系统错误 / Application Error」这类弹窗；
2. **打不开时给出可读的中文原因**（轨迹里的 `msg=` 必须含汉字）；
3. **能打开的，第一页文本能抽出来**（轨迹里出现 `text page=0`）。

汇总输出总数 / 打开成功 / 明确拒绝 / 要求密码 / 无响应 / 崩溃，**崩溃数必须为 0**；
崩溃数 > 0 或出现「无响应」（静默失败）时退出码非 0。

常用参数：

| 参数 | 说明 |
|---|---|
| `-CorpusDir` | 语料目录，默认 `build\corpus\malformed` |
| `-Limit N` | 只跑前 N 份（冒烟用） |
| `-OpenTimeoutSeconds` | 单份等待 open 记录的超时，默认 25 秒 |
| `-ReportPath` | 写一份 JSON 报告（每份语料的结果） |
| `-StrictExpectation` | 把 meta 里的期望也当硬断言（默认只要求「不崩 + 有可读结果」） |
| `-SelfTest` | 离线自检：只验轨迹判定逻辑，不启动应用 |
| `-DryRun` | 只打印语料概况与计划，不启动应用 |

**运行时长**：应用一次只接受一个命令行文件（`src/app/main.cpp` 只取 `arguments.at(1)`），
所以每份语料都要启动一次应用。1000 份大约 **40~60 分钟**，CI 上建议放夜间任务，
或先用 `-Limit` 冒烟。

---

## 四、K3 / K4 / K6：性能预算

```powershell
python scripts\gen-compat-corpus.py --out build\corpus --count 1000 --with-large
powershell -File scripts\verify-perf-budget.ps1 -CorpusDir build\corpus
```

| 指标 | 预算 | 依据 | 可调参数 |
|---|---|---|---|
| K3 滚动帧率 | ≥ 55 fps | 评分卡 K3（对齐福昕）。60Hz 屏一帧 16.7ms，55fps 允许偶发掉帧但不能持续低于 | `-MinFps` |
| K3 P95 帧时间 | 默认不设硬断言 | 偶发长帧是否可接受取决于场景 | `-MaxP95FrameMs`（0 = 不断言） |
| K4 打开耗时 | ≤ 60000 ms | 1 GiB 按 60 MB/s 慢盘估算读盘就要 ~17s，60s 留 3 倍余量给解析与首屏 | `-MaxOpenMs` |
| K4 翻页响应 | ≤ 2000 ms | 跳页 + 渲染一页；本地渲染是几十毫秒量级，2s 给慢机器留余量 | `-MaxPageTurnMs` |
| K6 峰值工作集 | < 400 MiB | 评分卡 K6（超越） | `-MaxPeakWorkingSetMiB` |

### K3 的测量口径（重要）

应用里**没有**帧时间埋点，所以 K3 是外部采样：

1. 把窗口置顶，用合成滚轮事件连续滚动（默认 8ms 一个滚轮步进）；
2. 同时从**屏幕 DC** 抓取窗口中央 400×300 的区域，算像素校验和；
3. 校验和变了 = 一帧已经呈现出来，记录时刻；
4. 由相邻帧间隔算出 fps 与 P50/P95/最长帧时间。

抓屏与屏幕刷新同步，单次抓取约 15ms，**采样率天然被屏幕刷新率卡住**（60Hz 屏约 60 次/秒）。
所以测到的 fps 是「屏幕内容每秒变化多少次」，即**用户实际看到的有效呈现帧率**，
不是应用内部的绘制调用次数——这正是「滚动帧率」该有的口径。
代价是分辨不出 60fps 与 120fps，但能分清 30fps 与 55fps。

其他参数：`-ScrollSampleMs`（采样窗口，默认 5000ms）、`-WheelIntervalMs`（滚轮步进间隔，默认 8ms）。

### K6 的测量口径

`Process.PeakWorkingSet64`（进程工作集高水位，Windows 上与「峰值 RSS」对应），
在打开 500 页文档并用滚动条翻过 12 屏之后读取。
注意：峰值工作集是**只涨不跌**的高水位，包含启动阶段与文件缓冲；
只统计**主程序**进程，不含 AI worker 子进程。

---

## 五、已知边界

- **1 GiB 文件曾经打不开**：`src/pdf/PdfiumAdapter.h` 的 `kMaxPdfFileSizeBytes`
  原来是 256 MiB，超过就返回「文件过大：… 字节，上限 … 字节」——这挡住了 K4 的目标。
  **现已提到 2 GiB**，1 GB 级文档可以打开了。
  代价：打开走 `file.readAll()`，整份文档进内存，内存占用与文件大小同量级。
  渲染侧的像素上限（`kMaxRenderPixelDimension` / `kMaxRenderPixelCount`）与文件大小无关，仍然独立生效。
- **K6 文档大小**：`pages-500.pdf` 默认 500 页 × 256×256 的真图，约 **94 MiB**；
  上限放开后可以用 `--page-image-size` 加大，以逼近 400 MiB 的峰值工作集预算。
- **语料总体积**：1000 份畸形语料 + 1000 份 meta 约 **2.5 MiB**（远小于 200 MB 的上限）；
  大文件语料按 `--large-gb` 线性增长（1 GiB 就是 1 GiB 磁盘）。
- **生成耗时（实测，本机）**：1000 份畸形语料 + 500 页文档共约 **1.6 秒**；
  1 GiB 大文件约 **3.5 秒**（受磁盘写入速度影响）。
  生成过程常驻内存恒定：产出 1 GiB 文件时，生成器的峰值工作集只有 **19.2 MiB**
  （像素按 1 MiB 一块流式写，不在内存里拼整个文件）。
- **CI 上预计跑多久**：K5 约 40~60 分钟（1000 次应用启动）；K3/K4/K6 约 3~5 分钟。
- **阈值可调**：上面每个预算都有对应的命令行参数，不需要改脚本。

---

## 六、本地怎么用（不抢桌面）

三个脚本都有**不启动应用**的模式，本地跑这些不会碰桌面：

```powershell
# 语料生成（本来就不碰桌面）
python scripts\gen-compat-corpus.py --out build\corpus-selfcheck --count 100

# 只验判定逻辑与采样通路，不启动应用、不合成输入
powershell -File scripts\verify-corpus-tolerance.ps1 -SelfTest
powershell -File scripts\verify-perf-budget.ps1 -SelfTest

# 只打印计划
powershell -File scripts\verify-corpus-tolerance.ps1 -DryRun
powershell -File scripts\verify-perf-budget.ps1 -DryRun
```

真正跑 K5 / K3 / K4 / K6 会**启动应用、合成滚轮、抓屏**，会抢前台——
放到 CI 或专门的验收会话里跑。
