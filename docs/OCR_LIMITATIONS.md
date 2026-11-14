# OCR 妥协清单

**状态：** 现行（自 v0.13.0 起）  
**适用范围：** `src/ocr/**`、`src/app/OcrController.cpp`、`scripts/verify-ocr.ps1`

## 为什么单独立一份

v0.13.0 的目标是「零安装、零新依赖，把扫描件 OCR 这条链路先跑通」，所以几乎每一层都留了妥协：
用 PowerShell 子进程去够 WinRT、渲染长边只给 2000px、置信度直接留 0、取消只对队列有效。

这些妥协写在代码注释里只能覆盖局部。后来的人看到 `meanConfidence = 0`、临时目录里的 `.ps1`、
`renderThumbnail(pageIndex, 2000)`，没法判断哪些是**主动选择**、哪些是**已知缺陷**、哪些**已经到期该还**。
这份文档按「现状 / 理由 / 代价 / 偿还条件」逐条记录。

原则：宁可写明「这里是坏的」，也不要让读者以为它是深思熟虑的设计。

## 清单

| 编号 | 妥协 | 位置 | 性质 | 偿还条件 |
|---|---|---|---|---|
| C-01 | 用 PowerShell 子进程间接调 `Windows.Media.Ocr` | `WindowsOcrEngine.cpp` | 主动选择 | 换 C++/WinRT 或并入随包引擎 |
| C-02 | 引擎选择：tesseract 优先，Windows OCR 兜底 | `OcrEngineFactory.cpp` | 主动选择 | 随包分发单一引擎 |
| C-03 | 无置信度，`meanConfidence` 恒为 0 | `WindowsOcrEngine.cpp` | 主动选择 | 换带置信度的引擎 |
| C-04 | 只有 word 级包围盒，无字符级 | `WindowsOcrEngine.cpp` | 引擎限制 | 换引擎 |
| C-05 | 无版面分析：分栏、表格、阅读顺序不还原 | `OcrChunkBridge.cpp` | 已知缺陷 | 引入版面分析 |
| C-06 | 无方向检测：旋转页静默返回空 | `WindowsOcrEngine.cpp` | **已知缺陷** | 旋转择优或 EXIF 方向 |
| C-07 | 渲染长边 2000px（A4 ≈ 171 dpi） | `OcrController.cpp` | 主动选择 | 提到 300 dpi 并夹到引擎上限 |
| C-08 | 全角标点未归一化 | `OcrTextUtils.cpp` | **已知缺陷** | 输出前做归一化 |
| C-09 | 单并发，每页一个进程 | `OcrService.cpp` | 主动选择 | 批量模式或进程常驻 |
| C-10 | `cancel()` 只能丢队列，打不断在跑的识别 | `OcrService.cpp` | **已知缺陷** | 改为可 kill 子进程 |
| C-11 | `isAvailable()` 只探测 powershell | `WindowsOcrEngine.cpp` | 已知缺陷 | 探测语言包 |
| C-12 | 验收脚本靠匹配错误文案判断「没有引擎」 | `verify-ocr.ps1` | 已知缺陷 | 改为显式的 engine probe |
| C-13 | 页面图像经 `StorageFile.GetFileFromPathAsync` 读入 | `WindowsOcrEngine.cpp` | 已知缺陷 | 改内存流 |
| C-14 | 只识别当前页，不做整本批量 | `OcrController.cpp` | 主动选择 | 批量 OCR 入口 |
| C-15 | 进程隔离未做，OCR 跑在 UI 进程的工作线程 | `OcrService.cpp` | 主动选择 | 独立低权限 Worker |

## 引擎与部署

### C-01 用 PowerShell 子进程调 WinRT

**现状：** 运行时把脚本写到临时目录，`powershell -NoProfile -ExecutionPolicy Bypass -File ocr.ps1`
调 `Windows.Media.Ocr`，读完删掉。

**理由：** MinGW 构建用不了 C++/WinRT 头，而这一版不想为此换成 MSVC 工具链，也不想为 OCR 引入
原生依赖（见 [依赖政策](DEPENDENCY_POLICY.md)）。

**代价：**

- 每页多一次进程启动（本机实测约 0.35 s 固定开销）；
- `-ExecutionPolicy Bypass` + 反射调 WinRT 是企业 EDR/AppLocker 的典型告警模式；
  WDAC 的 ConstrainedLanguage 会直接禁掉 `Add-Type` 和 WinRT 类型字面量，此时引擎整体不可用；
- 打成 MSIX/AppContainer 后无法再起子进程，这条路会断；
- 只能靠 `powershell`（5.1）；`pwsh` 7 没有 WinRT，不能替换。

**偿还条件：** 上 C++/WinRT（换 MSVC 或引入 `Microsoft.Windows.CppWinRT`），或直接换成随包分发的引擎。

### C-02 tesseract 优先、Windows OCR 兜底

**现状：** 装了 tesseract（PATH 或 `AIPDF_TESSERACT_PATH`）就用它，否则退回系统 OCR。

**理由：** 系统 OCR 零安装、零许可证负担，适合当兜底；tesseract 可控、可脱机分发、有 TSV 和置信度。

**代价：** 两条路径的**输出质量不同，UI 上不做区分**。同一份扫描件在两台机器上识别结果可能差很多，
用户无从判断是文档问题还是引擎问题。目前只有 `engineId` 字段记下用的是谁。

**偿还条件：** 随包分发单一引擎；在此之前，至少在设置里显示当前引擎名。

## 输入质量

### C-07 渲染长边 2000px

**现状：** `OcrController::requestPage()` 调 `view->renderThumbnail(pageIndex, 2000)`，
A4 页面约 171 dpi。

**理由：** 2000px 是缩略图路径现成的尺寸，内存和耗时可接受。

**代价（本机实测）：** 同一张 A4 双栏页，长边 3508（≈300 dpi）时「扫描件」20 行全对；
缩到长边 2000（≈171 dpi）后只有 5 行对，15 行认成「扫庙件」——两版字符总数都是 880，
**从字数上完全看不出来**，错的是字本身。再叠加小字号：300 dpi 下约 7pt 的「第17行」已经丢了数字，
171 dpi 只会更差。低质量样张（信息量约 40%）上则整句崩坏。
OCR 一般按 300 dpi 取图，171 dpi 对中文正文偏薄。

**偿还条件：** 长边提到约 3000–3500（A4 @ 300 dpi ≈ 3508），并夹在 `OcrEngine::MaxImageDimension`
以内——本机实测该值为 10000，超限时 WinRT 抛 `Image dimensions are too large`。

## 结果质量

### C-03 没有置信度

**现状：** `meanConfidence` 恒为 0.0，上层不据此过滤。

**理由：** 给 1.0 会假装「很确定」，给 0 至少不撒谎；`OcrTypes.h` 里已注明该值不可用。

**代价：** 低质量页只能整页收下，脏文本直接进索引；将来回写隐藏文本层时也无法按置信度取舍。

**偿还条件：** 换带置信度的引擎；或先做启发式（行级字符数/异常符号比例）作为临时信号，
但要在文档里标成启发式，不能冒充引擎置信度。

### C-05 没有版面分析

**现状：** 行按识别顺序进 `chunksFromOcrPage()`，只按行距切段。

**理由：** 版面分析是独立的大工程，v0.13.0 只要求「能搜到、能问答」。

**代价：** 双栏页的两栏会被交错串成一段；表格只留下按行散落的单元格文本，行列关系全丢；
页眉页脚会混进正文。问答引用这些块时，上下文是错乱的。

**偿还条件：** 引入分栏检测 + 表格结构识别；在此之前，UI 上不要承诺「表格可问答」。

### C-06 没有方向检测（已知缺陷，建议优先还）

**现状：** 直接把渲染图交给引擎。

**代价（本机实测）：** 整页旋转 90° 的扫描件返回 **exit 0、零行输出**。上层 `OcrController`
于是报「这一页没有识别出文字」——用户会以为是扫描质量差，实际是方向问题。
横向页、双面扫描的背面页在真实文档里很常见。

**偿还条件：** 旋转 0/90/180/270 各跑一次取行数最多者，或用投影方差先判方向；
成本约为单页耗时的 2–4 倍，可只用低分辨率试跑定方向、再正式识别一次。

### C-08 全角标点（已知缺陷）

**现状：** 中文引擎输出全角标点，`joinWords()` 只处理空格，不做字形归一化。

**代价（本机实测）：** `No．04123456`、`¥ 8 ， 640 ． 00`、`1 234 ． 56`。
索引里搜 `1234.56`、`8,640.00`、`No.04123456` 全部落空——票据、合同金额这类最该搜到的数字反而搜不到。

**偿还条件：** 输出前把 `．，：；（）％＃` 等全角标点映射到半角，并把「数字被空格拆开」的序列重新粘合。
这是一次纯函数改动，可单测，代价最低，建议先做。

## 任务与取消

### C-10 取消名不副实（已知缺陷）

**现状：** `WindowsOcrEngine::cancel()` 只置 `cancelled_` 标志；`QProcess` 是 `recognize()` 的栈变量，
外部拿不到句柄。`OcrService::cancelAll()` 只能清空队列。

**代价：** 正在跑的识别无法打断，最长阻塞到 120 s 超时。v0.13.0 文档里「OCR 可取消」目前只对**排队中**的页成立。
用户关文档、切页后仍会等，观感是「卡住」。

**偿还条件：** 子进程句柄提升到引擎成员，`cancel()` 直接 `kill()`；并把超时从固定的 120 s 改成按页面积估算。

### C-09 单并发、每页一个进程

**现状：** 一个 worker 线程，一次一页，每页重跑一次 `AsTask` 反射查找。

**代价（本机实测）：** 单页 0.6–0.8 s，其中约三分之一到一半是进程启动固定开销
（失败用例也要 0.45–0.6 s，基本就是启动 + WinRT 初始化）。200 页的扫描件顺序跑约 2–3 分钟，
期间不能并行、不能批量。

**偿还条件：** 一个进程连续处理多页（脚本接受页列表），或做整本批量入口（C-14）。

### C-15 进程隔离未做

**现状：** OCR 跑在 UI 进程的工作线程里，只做了线程隔离。

**理由：** v0.13.0 的进程隔离成本大于收益，`docs/AI_PDF_EDITOR_TECHNICAL_DESIGN.md` 已把
「高风险输入处理进低权限 Worker」列为后续项。

**代价：** 引擎崩溃（PowerShell 异常退出、WinRT 报错）只损伤任务，不损伤进程——但这依赖当前实现的健壮性，
不是架构保证；畸形 PDF 的渲染风险仍在主进程。

**偿还条件：** 接入独立 Worker（与 AI Worker 同构）后一并解决。

## 探测与验收

### C-11 `isAvailable()` 只探测 powershell

**现状：** 找到 `powershell` 就算「引擎可用」。

**代价：** 「引擎可用」≠「能识别」。只装了 en-US 语言包的机器上，中文扫描件**不报错**，
返回拉丁垃圾或空；`TryCreateFromUserProfileLanguages()` 还会跟着用户首选语言列表走，
中文包装了但不在首选列表里就返回 null。v0.13.0 文档里「Win10+ 通常带中文语言包」的说法不准确，
语言包需要单独安装。

**偿还条件：** 启动时调一次 `AvailableRecognizerLanguages`（或用脚本探一次），把「缺中文包」变成明确文案，
并在设置里显示识别语言。

### C-12 验收靠错误文案匹配

**现状：** `verify-ocr.ps1` 用正则匹配 `没有可用的系统 OCR` / `没有找到 tesseract` 来决定 SKIP。

**代价：** 文案一改，SKIP 分支就失效——要么把真实失败当失败（噪音），要么把失败当 SKIP（漏检）。

**偿还条件：** 提供一个 `--probe-ocr` 之类的显式探测入口，验收读退出码而不是读中文文案。

### C-13 页面图像走 StorageFile（已知缺陷）

**现状：** 脚本用 `StorageFile.GetFileFromPathAsync()` 打开临时 PNG。

**代价：** 该 API 受 broker 权限约束。在本仓库的受限执行环境里实测直接失败：
`拒绝访问……该项目没有位于应用程序可以访问的位置`；同样的图改用
`ReadAllBytes` + `InMemoryRandomAccessStream` 可以正常识别。普通桌面进程通常不受影响，
但这意味着「换个运行上下文就坏」，且报错信息对用户毫无意义。

**偿还条件：** 改为读字节流 + `InMemoryRandomAccessStream`，顺带省掉一次磁盘往返。

## 其他已知的小妥协

- **C-14 只识别当前页。** 自动触发只在「切到某页」时跑一次（每页每会话一次），没有「整本识别」入口。
- **脚本是无 BOM 的 UTF-8，内含中文错误文案。** 走 `-File` 时编码依赖系统代码页，加 BOM 或把文案改成 ASCII 更稳。
- **临时目录每页建一次**（脚本 + PNG），失败路径上清理依赖 `QTemporaryDir` 析构。

## 实测记录

环境：Windows 11，`Windows.Media.Ocr` 只有 `zh-Hans-CN` 一个语言包，`MaxImageDimension = 10000`。
方法：把 `WindowsOcrEngine.cpp` 里的 `kScriptTemplate` 原样抽出，用同样的命令行调用，样图为程序生成的票据/双栏页。

| 用例 | 结果 | 耗时 |
|---|---|---|
| 干净中英混排 1200×900 | 文字全出；`No．04123456`、`USD 1 234 ． 56` | 0.58–0.67 s |
| A4 双栏，长边 3508（≈300 dpi） | 60 行；「扫描件」20/20 正确；7pt 的 `第17行` 丢数字；表格线全丢 | 0.83 s |
| 同一页，长边 2000（≈171 dpi，即当前实现） | **「扫描件」只有 5/20 正确**，15 行认成「扫庙件」；字符总数与 300 dpi 相同（880） | 0.72 s |
| 低分辨率（信息量约 40%） | 错字成片，金额与单号不可用 | 0.58 s |
| 旋转 90° | **exit 0，零行输出** | 0.58 s |
| 1200×10200 | 抛 `Image dimensions are too large!` | 0.45 s |

复现方式：

```powershell
# 1. 从源码抽出脚本
$src = [IO.File]::ReadAllText('src/ocr/WindowsOcrEngine.cpp', [Text.Encoding]::UTF8)
$script = [regex]::Match($src, '(?s)R"PS\((.*?)\)PS"').Groups[1].Value
[IO.File]::WriteAllText("$env:TEMP\ocr.ps1", $script, (New-Object Text.UTF8Encoding($false)))

# 2. 用引擎的原样命令行跑一张图
powershell -NoProfile -ExecutionPolicy Bypass -File "$env:TEMP\ocr.ps1" -ImagePath C:\path\to\page.png
```

注意：`StorageFile` 路径在受限上下文（受限令牌 / AppContainer / 沙箱）下会失败，见 C-13；
要在这类环境里复现识别质量，需把脚本换成内存流版本。

## 偿还顺序（建议）

1. **C-08 标点归一化**、**C-13 内存流**、**C-06 方向检测**、**C-07 提到 300 dpi** —— 都是局部改动，
   直接决定「搜得到 / 搜不到」和「有没有结果」。
2. **C-11 语言包探测**、**空结果归因**（区分「没有文字」「方向不对」「缺语言包」） —— 把静默降质变成可解释的提示。
3. **C-10 真取消**、**C-12 显式探针** —— 修掉名不副实的能力和脆弱的验收。
4. **C-05 版面分析**、**C-09/C-14 批量**、**C-01/C-15 引擎与进程** —— 属于换引擎或独立 Worker 的范畴，
   连同 [依赖政策](DEPENDENCY_POLICY.md) 一起决策。

## 不打算偿还的

- **C-04 字符级框。** 引擎只给 word 级，除非换引擎，否则无解；在「复制/高亮」场景下 word 级够用。
- **C-02 双引擎。** 只要还允许「用户自己装 tesseract」，两套输出就会并存；接受这一点，
  但要把引擎名暴露给用户（见 C-02 的偿还条件）。
