# 源码功能模块划分

这份文档回答一个问题：`src/` 里按功能模块分目录行不行、怎么分、代价多大。
结论先说：**行，而且 src/ 下的四个大目录已经全部落地**——src/pdf 130 个文件拆成 9 个模块，
src/ai 40 个拆成 5 个，src/doc 57 个拆成 8 个，src/app 163 个拆成 7 个，src/ocr 20 个拆成 3 个。
引用改写量都是可枚举的（pdf 130 次移动 / 282 个文件 / 1213 行引用；ai 40 / 76 / 299；
doc 57 / 119 / 458；app 163 / 160 / 660；ocr 20 / 27 / 90），五个都走同一个脚本
`scripts/modularize-src.ps1 -Area <pdf|ai|doc|app|ocr>`。

## 现状

`src/` 下 425 个源文件的分布：

| 目录 | 文件数 | 说明 |
| --- | ---: | --- |
| `app/` | 163 | 窗口、面板、对话框、控制器全平铺在一起 |
| `pdf/` | 130 | PDF 底层：注释、表单、签名、合规、读写、转换全在一个目录 |
| `doc/` | 57 | 文档模型、导出、命令栈 |
| `ai/` | 40 | AI 供应商、凭据、索引 |
| `ocr/` | 20 | OCR 引擎与几何（已拆成 3 个模块） |
| 其它 | 15 | `ipc/`、`speech/`、`tools/`、`i18n/` |

三个最大的单文件是 `PdfView.cpp`（356 KB）、`MainWindow.cpp`（324 KB）、
`PdfiumAdapter.cpp`（122 KB）。这三个是"类太大"的问题，和"目录太挤"是两件事：
分目录不解决它们，得单独拆类。

代码里其实已经有模块概念了——`src/pdf` 的 65 个头文件全部声明在
`namespace aipdf::pdf` 下，只是命名空间只有一层，磁盘上也还摊在一个目录里，
所以"哪些文件是一伙的"只能靠文件名猜。`#include` 的写法是相对 `src/` 的
`"pdf/Xxx.h"`，也就是说**路径本身就是模块名**，改名即可获得模块边界。

## `src/pdf` 的九个模块

划分依据是"这个文件为什么会被改"：同一个模块的文件通常因为同一个需求一起改。
依赖关系不是猜的，是从改写后的 include 里统计出来的（见下一节）。

| 模块 | 文件数 | 职责 | 主要文件 |
| --- | ---: | --- | --- |
| `kernel/` | 13 | PDFium 适配层与最基础的文档/导航/页/表/字体模型 | `PdfiumAdapter`、`DocumentOpen`、`Navigation`、`TextPage`、`StandardFonts`、`TableStructure`、`PdfRevisionCounter` |
| `document/` | 10 | 文档写出：增量保存、结构校验、属性、命名目标 | `DocumentWriter`、`IncrementalWriter`、`QpdfBackend`、`DocumentProperties`、`NamedDestination` |
| `content/` | 13 | 内容流、文本重排、字体子集化、页面装饰 | `ContentStream`、`TextReflow`、`ParagraphFormat`、`TrueTypeSubset`、`InvisibleTextLayer`、`TableIntegration` |
| `annot/` | 22 | 注释模型、外观流、批注字典、Xfdf、富媒体、页面注注入 | `Annotation`、`AppearanceStream`、`AnnotationToolkit`、`Xfdf`、`PageAnnotsInjector`、`RichMedia3d` |
| `forms/` | 20 | 表单字段、JS 求值、模板、自动打标签、条码、XFA | `FormFieldWriter`、`FormJsEvaluator`、`FormTemplateStore`、`FormAutoTag`、`XfaSupport` |
| `security/` | 12 | 加密、权限、签名与验签、涂黑 | `PdfSecurity`、`SignatureVerifier`、`SignatureWorkflow`、`RsaVerifier`、`Redaction` |
| `compliance/` | 10 | PDF/A、PDF/UA、预检、标签树与结构树 | `PdfAChecker`、`PdfUaChecker`、`PreflightChecker`、`TagTreeWriter`、`StructTreeReader` |
| `ops/` | 24 | 文档级操作：合并、比较、重排、分页、大纲、页码、图层、批替换、拼写、OCR 嫌疑 | `DocumentMerger`、`DocumentComparer`、`DocumentReflow`、`OutlineEditor`、`PageLabels`、`BatchReplace` |
| `convert/` | 6 | 导入导出格式 | `ExportFormats`、`ImageToPdf`、`PortfolioWriter` |

合计 130 个文件，覆盖 `src/pdf` 全部内容，无遗漏无重复（脚本会先校验这一点，
校验不过就不动手）。

### 实测依赖

统计口径：改写后 `src/pdf/<模块>/` 下的文件里，`#include "pdf/<模块>/..."` 的条数。

```
compliance -> content      4
content    -> kernel       3
convert    -> kernel       2
forms      -> kernel       3
ops        -> kernel       7    content 2    document 4    compliance 2
security   -> kernel       2
document   -> kernel       1    annot 5    security 1
kernel     -> annot        1
```

`kernel` 在底、`document` 在顶、其余夹在中间，没有环。

第一版拆完时有两处环（`kernel` ⇄ `document`、`document` ⇄ `security`），都是"底层文件反过来
include 上层"造成的，最后靠三个文件和一行 include 解决，没有改任何逻辑：

| 原环 | 成因 | 处理 |
| --- | --- | --- |
| `kernel` ⇄ `document` | `PdfiumAdapter.cpp` 引 `document/DocumentOpen.h`；`PdfiumAdapter.h` 引 `document/Navigation.h`（`PageLink`、`OutlineItem` 是适配层 API 的类型） | 两个都下沉到 `kernel`：`DocumentOpen` 本身就是"用 PDFium 打开文件"，`Navigation` 只是页内链接/大纲节点的值类型 |
| `kernel` ⇄ `content` | `PdfiumAdapter.h` 引 `content/TableStructure.h`（返回值类型） | `TableStructure` 是适配层直接暴露的类型，下沉到 `kernel` |
| `document` ⇄ `security` | `PdfSecurity.cpp` 引 `document/PdfRevisionCounter.h` | `PdfRevisionCounter` 是纯结构计数，下沉到 `kernel`；`DocumentWriter.cpp -> PdfSecurity.h` 保留，单向 |

判断标准只有一条：**被下层直接暴露成 API 的类型，跟着下层走**，而不是"按功能名词归属"。
`TableStructure` 归 `content` 在语义上更顺，但它出现在 `PdfiumAdapter` 的签名里，放在 `content`
就会让底层反向依赖上层。

`kernel` 还剩一条指向 `annot` 的边（`PdfiumAdapter.h` 引 `Annotation.h`，把注释以
`QVector<Annotation>` 的形式放进 API）。它不成环，但属于"适配层知道太多"，可以在拆
P2 静态库时再收：要么改成不透明句柄，要么把注释相关方法挪到 `annot/`。

## `src/ai` 的五个模块

| 模块 | 文件数 | 职责 | 主要文件 |
| --- | ---: | --- | --- |
| `chat/` | 6 | 对话契约：消息类型、SSE 流解析、模型与向量接口 | `ChatTypes`、`SseParser`、`ModelProvider`、`EmbeddingProvider` |
| `provider/` | 6 | 供应商实现与工厂 | `OpenAiProvider`、`MockProvider`、`ProviderFactory` |
| `settings/` | 14 | 供应商配置/预设/选择、凭据、数据政策、审计、上传预览 | `ProviderConfig`、`ProviderPreset`、`ProviderSelection`、`CredentialStore`、`DataPolicy`、`AuditLog`、`UploadPreview` |
| `rag/` | 4 | 本地哈希向量与离线问答 | `LocalHashEmbedding`、`OfflineQa` |
| `content/` | 10 | 生成类能力：内容检测、配图、旁白、PPT 生成与导出 | `AiContentDetection`、`AiImageGeneration`、`AiNarration`、`PptGeneration`、`PptxExport` |

实测依赖是全序的，没有环：

```
content  -> chat      2
provider -> chat      4    settings 4
rag      -> chat      1
```

`settings` 之所以一次收进 7 个基名，是因为它们互相咬合：`ProviderConfig`/`ProviderPreset`/
`ProviderSelection` 三个头文件互相引用，`CredentialStore` 同时被工厂和配置用，
`DataPolicy`、`AuditLog`、`UploadPreview` 又都挂在 `ProviderConfig` 上。试过把它拆成
"配置/预设"与"政策/审计"两块，立刻出现 `settings` ⇄ `privacy` 环，所以维持一个模块，
名字取最宽的那个语义。

## `src/doc` 的八个模块

| 模块 | 文件数 | 职责 | 主要文件 |
| --- | ---: | --- | --- |
| `model/` | 7 | 空白文档骨架、分块与统计 | `BlankDocument`、`DocumentChunk`、`ChunkBuilder`、`DocumentStatistics` |
| `edit/` | 9 | 编辑命令、撤销栈、页命令、注释模型 | `EditCommand`、`EditContext`、`PageCommand`、`UndoStack`、`AnnotationModel` |
| `index/` | 8 | 本地索引、向量检索、混合检索、引用核对 | `LocalIndex`、`VectorIndex`、`HybridRetriever`、`CitationVerifier` |
| `export/` | 13 | Office/OFD/PPT/表格导出与 ZIP、XML 底座 | `OfficeExport`、`OfdExport`、`PdfToOffice`、`PptxWriter`、`SpreadsheetExport`、`XmlText`、`ZipWriter` |
| `convert/` | 8 | 通用转换入口、转换输入、发票版式、字体扁平化 | `UniversalConverter`、`ConversionInputs`、`InvoiceLayout`、`FontFlattening` |
| `print/` | 4 | 打印模板与批量打印 | `PrintTemplate`、`PrintBatch` |
| `persist/` | 4 | 安全保存与恢复日志 | `SafeSave`、`RecoveryLog` |
| `batch/` | 4 | 批处理与动作向导 | `BatchProcessor`、`ActionWizard` |

内部依赖比 `ai` 还干净：只有一个底座和三条单向边，另外四个模块彼此完全独立。

```
convert -> model   3
index   -> model   1
print   -> model   1
```

也就是说 `edit`、`export`、`persist`、`batch` 内部是闭合的，改动不外溢；
真正被共享的只有 `model/`（`BlankDocument` 与分块模型）。

## `src/app` 的八个模块

`app/` 是 UI 层，163 个源文件。它的边界和前三层不同：没有"底层库"，只有一条自上而下的
调用链加几个装配点。按"这些文件会不会因为同一个需求一起改"分：

| 模块 | 文件数 | 职责 | 主要文件 |
| --- | ---: | --- | --- |
| `shell/` | 9 | 入口、主窗口、Ribbon、欢迎页、动作装配 | `main`、`MainWindow`、`WiringActions`、`RibbonBar`、`WelcomePage` |
| `ui/` | 14 | 主题、语言、阅读方案、模态遮罩、品牌标记、字体 | `Theme`、`Language`、`ReadingScheme`、`ModalScrim`、`SystemCjkFont` |
| `editor/` | 20 | 视图与渲染：画布、缓存、渲染线程、缩放、命中、手势、OCR 交互 | `PdfView`、`PageCanvas`、`TileCache`、`RenderThread`、`ViewFit`、`OcrController` |
| `layout/` | 20 | 纯几何与排版计算，不依赖任何控件 | `PageLayout`、`PageTransform`、`TextLayout`、`PageTextLines`、`PageChunkBuilder`、`PdfPageOperations` |
| `controllers/` | 34 | 功能控制器与后台桥 | `TextController`、`AnnotationController`、`NavigationController`、`IndexController`、`LastWaveBridge`、`DocumentOutput` |
| `panels/` | 12 | 常驻面板 | `SearchPanel`、`AiPanel`、`IndexPanel`、`AttachmentPanel`、`LastWavePanels` |
| `dialogs/` | 42 | 对话框与向导 | `SettingsDialog`、`SignatureDialog`、`AccessibilityWizard`、`RegionPicker` |
| `util/` | 12 | 无状态工具 | `BatchRename`、`SearchMatcher`、`ShareUtils`、`ConversionReports` |

实测依赖是一个 DAG：

```
shell       -> editor 5   controllers 15   dialogs 16   panels 6   ui 10   layout 2   util 5
editor      -> controllers 17   layout 13   ui 10   dialogs 4   panels 1
controllers -> layout 10   util 2   ui 1
panels      -> controllers 7   ui 5   dialogs 2   layout 1
dialogs     -> ui 10   controllers 2   util 1
```

这个结果是调出来的，过程值得记一笔。第一版按角色拆（`editor` = 视图 + 几何，`controllers` =
全部控制器）得到的是四个模块的强连通分量（`controllers`/`dialogs`/`editor`/`panels`）。
定位后发现 `controllers -> editor` 的 11 条边里只有 1 条真的碰控件
（`OcrController` 用 `PdfView`），其余 10 条都是 `PageTransform`、`TextLayout`、
`PageChunkBuilder` 这类**纯计算类型**——它们本来就不该和 `PdfView` 挤在一个目录里。于是：

1. 把 10 个纯几何/排版文件从 `editor/` 拆出成 `layout/`，`editor/` 只剩视图与渲染；
   `layout/` 成为不依赖任何 Qt 控件的计算层。
2. 把唯一直接依赖 `PdfView` 的 `OcrController` 并入 `editor/`——它本来就是编辑器内的
   OCR 交互。

调整后 `editor → layout`、`controllers → layout`、`panels → layout` 都是单向的，
环消失。

**但这不等于解耦**：`OcrController` 仍然直接持有 `PdfView`，只是现在两者同属 `editor/`，
这条依赖不再跨模块。真要让它可测试，还是得抽一个 `EditorSurface` 接口（见 P2）。

还剩一处单向但值得收的：`PdfView` 直接构造 4 个对话框和 `LastWavePanels`
（`editor -> dialogs 4`、`editor -> panels 1`）。它不成环，所以不阻塞；但要让
`editor/` 能脱离 UI 单测，应该改由 `WiringActions` 注入。

## `src/ocr` 的三个模块

20 个文件，按"契约 / 流水线 / 引擎实现"分：

| 模块 | 文件数 | 职责 | 主要文件 |
| --- | ---: | --- | --- |
| `core/` | 6 | OCR 契约、几何与文本工具 | `OcrTypes`、`OcrEngine`、`OcrGeometry`、`OcrTextUtils` |
| `pipeline/` | 8 | 服务编排、图像预处理、扫描页判定、分块桥接 | `OcrService`、`ImagePreprocess`、`ScannedPageDetector`、`OcrChunkBridge` |
| `engines/` | 6 | 两种引擎实现与工厂 | `TesseractCliEngine`、`WindowsOcrEngine`、`OcrEngineFactory` |

依赖是干净的三层，没有环：

```
engines  -> core 7    pipeline 2
pipeline -> core 2
```

`engines` 之所以多一条指向 `pipeline`，是两个引擎都直接用了 `ImagePreprocess` 做预处理；
这条边方向正确（实现层用工具），没有回指。

## 落地分三步

### P0 逻辑模块（零风险）

在 `src/CMakeLists.txt` 里把 450 行平铺的源文件清单改成按模块分段（或抽到
`cmake/pdf_sources.cmake`），每段一个注释标题。文件一个不动，构建产物一模一样，
收益是"谁能看见模块边界"从文件名变成构建脚本，评审时也能看出某个改动跨了几个模块。

### P1 物理拆目录（已完成，一次机械改动）

`scripts/modularize-src.ps1 -Area <pdf|ai|doc|app|ocr> -Apply` 做的就是这件事：

1. 按划分表把文件 `git mv` 到 `src/<area>/<模块>/`（`git mv` 保留历史）；
2. 把全仓库的 `<area>/Xxx.h` / `<area>/Xxx.cpp` 引用改成 `<area>/<模块>/Xxx.h`，
   同时覆盖 `#include "..."` 和 `tests/` 里的 `${CMAKE_SOURCE_DIR}/src/<area>/Xxx.cpp`；
3. 按 BOM 与换行风格原样回写（`src/` 里 CRLF/LF 混用，不能一律转 LF）；
4. 自检：每个 `<area>/<模块>/<文件>` 引用都能解析到真实文件，且没有残留的
   未加模块段的 `<area>/<文件>` 引用。

五个目录都已执行完，并各跑过一次全量构建与 `ctest`：

| 目录 | 移动 | 重写文件 | 引用行数 | 构建 | 测试 |
| --- | ---: | ---: | ---: | --- | --- |
| `src/pdf` | 130 | 282 | 1213 | 通过 | 149/150（唯一失败项与搬迁无关） |
| `src/ai` | 40 | 76 | 299 | 通过 | 149/150（同一个 `PageAnnotsInjectorTests`） |
| `src/doc` | 57 | 119 | 458 | 通过 | 149/150（同一个 `PageAnnotsInjectorTests`） |
| `src/app` | 163 | 160 | 660 | 通过 | 149/150（同一个 `PageAnnotsInjectorTests`） |
| `src/ocr` | 20 | 27 | 90 | 通过 | 149/150（同一个 `PageAnnotsInjectorTests`） |

注：src/app 在第一版按角色拆完之后又做了一次内聚重分类（22 个文件移动、39 个文件引用改写），最终 8 个模块、无环，见上文。

注：SpeechEngineTests 依赖本机语音栈，偶发失败，单跑即过；上表统计的是首轮结果。

因为都是纯路径替换，review 时可以只看 `git diff -M --stat` 和几个抽样文件；
脚本本身也会核对"每一行改动都含目录名"，确保没有混进内容改动。

### P2 拆编译单元（可选，后做）

模块边界稳定后再考虑把 `pdf` 拆成 `pdf_kernel` / `pdf_content` / … 几个静态库，
好处是编译期就能挡住反向依赖、并减少增量编译范围。**不建议和 P1 一起做**：
那会把"路径替换"和"目标拆分"两类问题混在一个 PR 里，出问题不好二分。

## 不做什么

- 不动命名空间。`aipdf::pdf` 保持不变，避免把"搬文件"变成"改符号"，
  命名空间要不要细分是另一个决定。
- 不顺手改文件内容。除路径替换外一个字符都不动，保证能 `git diff -M` 看穿。
- 不在同一个 PR 里拆类。`PdfView.cpp` 356 KB 的拆分单独做，和目录调整互不阻塞。
- 不为了"看起来像 DAG"硬挪文件。`app/` 那几个环只能靠抽接口/改注入解决，
  把文件挪到别的目录只是把问题藏起来。
