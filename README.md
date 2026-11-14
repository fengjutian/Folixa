# AI PDF Editor

AI PDF Editor 是一个采用 C++20、Qt 6 和 PDFium 规划实现的原生 PDF 编辑器。当前版本已建立可复现的 Windows 构建、测试与打包环境：主程序、独立 AI Worker、本地 IPC 健康检查、自动化测试和可独立运行的开发包。

## 当前能力

- Qt Widgets 主窗口和多文档标签框架
- 文档视图、缩略图导航面板与 AI 助手面板
- **用 PDFium 打开 PDF 并渲染首页**（支持密码保护文件、旋转页、缩放与高 DPI）
- **连续页面视口与瓦片渲染**：按视口渲染、邻页预取、LRU 缓存（按像素字节计量上限）、
  渲染任务按代取消，500 页文档可连续浏览
- 缩放、用户旋转、适应宽度与适应页面
- **文本提取、选择、复制与全文搜索**：后台提取文本层、
  鼠标拖选与双击选词、Ctrl+C 复制、Ctrl+F 搜索面板（结果列表、上一处/下一处、停止）、
  命中高亮与跳转；长文档搜索可随时取消
- **目录、书签与页面链接**：后台取回文档大纲（分层树）与页面链接注释，
  目录树/书签列表可点击跳转，内部链接跳页、外部链接交给系统浏览器，
  悬停在链接上显示手型光标
- **基础批注与撤销重做**：高亮、下划线、删除线（作用于文字选区），
  便签、矩形、墨迹（作用于页面区域）；批注模型 + 可序列化编辑命令 + 统一撤销栈，
  Ctrl+Z / Ctrl+Shift+Z 撤销重做，批注面板可列出、跳转与删除；
  批注同时写进 PDFium 文档，成为文档里真实的注释对象
- **页面管理与安全保存**：页面页里插入空白页、删除页、旋转页、上下移动页，
  页面操作与批注操作共用同一个撤销栈；保存走「临时文件 → 重新打开校验 →
  原子替换」，失败时原文件原封不动，原文件旁边留一份 `.folixa.bak` 备份；
  另存为可换路径；改过的文档会写恢复日志，崩溃后下次启动可以恢复未保存的工作
- **文档分块与本地索引**：后台把文档按排版特征切成块（段落、标题、列表项），
  每块带页码、PDF 用户空间坐标、章节路径与标题层级；索引落在本地，
  内容没变的页直接复用（前面插了页也不用重建），按关键词检索后
  点结果即可跳到原文并高亮对应区域
- **流式 AI 对话**：AI 面板提供总结当前页、解释选区、翻译选区与就当前页提问，
  回答逐段流出、可随时停止，并报出 token 用量与估算费用；
  接入 OpenAI 兼容的云端模型，API 密钥存 Windows 凭据管理器（不进配置文件与日志）；
  没配密钥时走演示模式，界面上显式标注「演示模式」
- **发送范围先展示再上传**：只有当前页文本或当前选区会被送出（上限 4000 字），
  发送前界面先显示「本次发送：第 N 页 · M 字」
- **可验证的整篇问答**：把范围切到「整篇文档」后提问，先用关键词 + 向量混合检索
  （RRF 融合）挑出最相关的几块，只把这几块送去回答；回答里的每条引用都会被
  **离线逐字核对**（编号是否存在、引文是否真的出现在原文里、页码是否一致），
  核验不通过的引用标成「无法核实」；点引用即可跳到那一页并高亮那一块
- 独立 `PdfAiWorker` 进程
- 基于 `QLocalSocket`/`QLocalServer` 的版本化 JSON Lines IPC
- Worker 自动启动、连接、超时和健康检查
- Worker 跟随主程序退出：主程序被强制结束时 Worker 自行退出，不留孤儿进程
- IPC 协议单元测试、Worker 集成测试、PDFium 适配层测试、视口/缓存单元测试、
  批注/撤销栈单元测试、页面管理/安全保存单元测试、文档索引单元测试与模型接入单元测试
- 固定工具链、CI、可重复的打包与自动化验收

## OCR 引擎（可选）

扫描件识别优先用本机的 Tesseract；没装时退回 Windows 自带 OCR（Win10+ 无需安装）。

想用 Tesseract，二选一：

- 把 `tesseract.exe` 放到 PATH，或用环境变量 `AIPDF_TESSERACT_PATH` 指定；
- 放到仓库的 `tools/tess/`（解压版放 `tesseract.exe` 同级 + `tessdata/`，
  conda 版保持 `Library/bin` + `share/tessdata` 结构）——应用会自动找到它。

`tools/tess/` 不进仓库（约 1 GB），语言包目录也可以用 `AIPDF_TESSDATA_PREFIX` 指定。
## 构建环境

固定组合：

| 组件 | 版本 | 说明 |
|---|---|---|
| Qt | 6.8.3 `mingw_64` | 官方二进制套件，qtbase + qtsvg |
| MinGW-w64 | GCC 13.1.0 | Qt 官方工具包 |
| PDFium | API 159（chromium/8152） | 预编译 Windows x64 包，按 SHA-256 校验 |
| CMake | 3.25+ | 系统安装 |
| Ninja | 1.10+ | 系统安装 |

一条命令装好 Qt 与 MinGW（免管理员权限）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\bootstrap-toolchain.ps1
```

PDFium 单独获取（同样免管理员权限，校验 SHA-256）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\bootstrap-pdfium.ps1
```

脚本会打印实际路径。按需设置环境变量覆盖默认位置：

```powershell
$env:AIPDF_QT_ROOT = 'C:\Qt\6.8.3\mingw_64'
$env:AIPDF_MINGW_ROOT = 'C:\Qt\Tools\mingw1310_64'
```

工具链文件在 `cmake/toolchains/qt-mingw.cmake`。vcpkg 以 `builtin-baseline` 固定版本，
当前不引入源码构建的第三方依赖。

## OCR 引擎（可选）

扫描件识别优先用本机的 Tesseract；没装时退回 Windows 自带 OCR（Win10+ 无需安装）。

想用 Tesseract，二选一：

- 把 `tesseract.exe` 放到 PATH，或用环境变量 `AIPDF_TESSERACT_PATH` 指定；
- 放到仓库的 `tools/tess/`（解压版放 `tesseract.exe` 同级 + `tessdata/`，
  conda 版保持 `Library/bin` + `share/tessdata` 结构）——应用会自动找到它。

`tools/tess/` 不进仓库（约 1 GB），语言包目录也可以用 `AIPDF_TESSDATA_PREFIX` 指定。
## 构建与测试

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug

cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

构建后 CMake 会自动把 Qt 运行库与平台插件部署到 `bin` 目录，
**构建树是自包含的**——即使机器上没有 Qt SDK，`ctest` 与验收脚本也能直接运行。

主程序与 Worker 输出到同一个 `bin` 目录，运行：

```powershell
.\build\windows-debug\bin\AiPdfEditor.exe
```

主程序会自动拉起同目录下的 `PdfAiWorker.exe` 并完成健康检查。

## 一条命令跑完整流水线

```powershell
powershell -ExecutionPolicy Bypass -File scripts\ci.ps1
```

依次执行：安装固定工具链 → Debug 构建/测试/启动验收 → Release 构建/测试 →
打包 → 无 SDK 打包验收 → 版本一致性 → 发布门负例守卫。
本地和 CI 跑的是同一个脚本。

界面验收会抢占前台窗口并用合成鼠标事件操作桌面，**在别人正在用这台机器时不要跑**：
那种情况下加 `-SkipAcceptance` 跳过界面验收，其余步骤照跑。

```powershell
powershell -ExecutionPolicy Bypass -File scripts\ci.ps1 -SkipAcceptance
```

## 启动验收

```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify-m0.ps1
```

启动主程序，检查 Worker 拉起、就绪状态、版本显示、健康检查返回值与退出清理，
可用 `-ScreenshotPath` 保存窗口截图。

## PDF 能力验收

```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify-pdf.ps1
```

用命令行参数直接打开测试 PDF，验证标签页、页码信息、缩放比例与损坏文件的错误提示。

也可以直接打开文件：

```powershell
.\build\windows-debug\bin\AiPdfEditor.exe 某个文件.pdf
```

## 批注验收

```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify-annotations.ps1
```

用真实界面路径验证批注：拖选文字做高亮、切换矩形与墨迹工具拖拽、便签输入框，
并检查批注是否写进了 PDFium 文档、撤销与重做是否同步、批注面板能否列出与删除。
脚本会抢前台并用合成鼠标操作桌面，**只应在 CI 或空闲机器上运行**。

## 页面管理与安全保存验收

```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify-pages.ps1
```

用真实界面路径验证页面管理：命令栏的「页面」打开页面页，插入空白页、撤销、重做、
旋转、下移、删除，逐步核对页数；再点「保存」，核对原文件被换成新内容、
旁边留下备份、临时文件没有残留，以及恢复日志在保存之后被清掉；
最后模拟崩溃再重启，验恢复问询弹得出来、点「恢复」之后命令被重放。
同样会抢前台，**只应在 CI 或空闲机器上运行**。

## AI 面板验收

```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify-ai.ps1
```

用真实界面路径验证 AI 面板：演示模式标注、流式总结与用量、就当前页提问、
取消正在进行的生成、设置对话框的打开与取消。
同样会抢前台，**只应在 CI 或空闲机器上运行**。

## 打包

```powershell
cmake --build --preset windows-release --target package
```
产出 `dist/ai-pdf-editor-<版本>-win64.zip`，内含主程序、Worker、Qt 运行库与平台插件、
MinGW 运行库、`LICENSE`、`NOTICE.txt`、第三方许可证清单和 CycloneDX SBOM，
同时写出 ZIP 的 SHA-256。

## 打包验收（无 Qt SDK 环境）

```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify-package.ps1 `
    -PackagePath dist\ai-pdf-editor-0.2.0-win64.zip -Version 0.2.0
```

脚本把安装包解压到临时目录、把 `PATH` 限制为系统目录（找不到任何 Qt SDK 与工具链），
再启动其中的主程序，验证包确实可以脱离开发环境运行。

## 发布门

| 门 | 作用 |
|---|---|
| `scripts\verify-package.ps1` | 安装包内容齐全，且在没有 Qt SDK 的环境可运行 |
| `scripts\verify-sbom.ps1` | 包内每个文件都在 SBOM 中登记，且 SHA-256 与实际一致 |
| `scripts\verify-licenses.ps1` | 安装包随附第三方许可证全文，与 `licenses\CHECKSUMS.txt` 和 `licenses\pdfium-CHECKSUMS.txt` 一致（文件集合与哈希，不少也不多） |
| `scripts\verify-logs.ps1` | 构建产物、安装包与运行时数据目录里没有密钥形态字符串（脚本自带负例守卫） |
| `scripts\check-version.ps1` | CMake、vcpkg 清单、版本文档、索引状态、exe 版本、安装包名称一致 |
| `scripts\test-gates.ps1` | 上述门的负例守卫：故意弄坏输入，门必须变红 |

单独执行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\check-version.ps1
powershell -ExecutionPolicy Bypass -File scripts\test-gates.ps1
```

## 第三方许可证

`licenses/` 保存随包分发的许可证全文（LGPL-3.0、GPL-3.0、GCC 运行时例外）及来源说明，
打包时随安装包一起分发并按 SHA-256 校验：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\fetch-licenses.ps1 -VerifyOnly
```

PDFium 及其 19 份第三方组件许可证原文的清单是 `licenses\pdfium-CHECKSUMS.txt`，
打包时按清单逐份复制并核对哈希，缺一份即中止打包；发布门再核对安装包内的文件集合与哈希。
清单与固定版本 PDFium 的一致性由获取脚本守卫：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\bootstrap-pdfium.ps1
```

## 持续集成

`.github/workflows/windows.yml` 在 `windows-2022` 上调用 `scripts/ci.ps1`，
即与本地完全相同的流水线：安装固定工具链 → Debug/Release 构建与测试 → 启动验收
→ 打包 → 无 SDK 打包验收 → 发布门，并上传安装包与验收证据。

## 文档

- [对标基线：Foxit PDF Editor 2026](docs/FOXIT_BENCHMARK.md)
- [技术设计](docs/AI_PDF_EDITOR_TECHNICAL_DESIGN.md)
- [Prism Document UI 设计规范](docs/UI_DESIGN_SYSTEM.md)
- [版本与任务管理规范](docs/VERSIONING.md)
- [版本任务索引](docs/versions/README.md)
- [依赖与许可证政策](docs/DEPENDENCY_POLICY.md)
- [OCR 妥协清单](docs/OCR_LIMITATIONS.md)
- [IPC 协议](docs/IPC_PROTOCOL.md)
- [第三方许可证清单](THIRD_PARTY_LICENSES.md)

本项目实行“一项主要任务对应一个软件版本”。当前开发版本为
[`0.12.0`](docs/versions/v0.12.0.md)，只有当前版本通过验收后才进入下一版本。

1.0 之后的能力路线以 [对标基线](docs/FOXIT_BENCHMARK.md) 为准：先补齐打印、批注互操作、
表单、加密与签名，再做内容编辑，最后按 96 项评分卡收敛到 2.0。

## 开发规范

### 一次改动的工作方式

1. **先写代码**：把功能从头到尾写完再动测试。
   写一块就跑一遍全量验收，会把「代码还没写完」和「功能真的坏了」混在一起，
   既分不清是谁的问题，也把跑验收的时间摊到了每个小改动上。
2. **最后统一测试**：代码写完后一次性跑完整流水线
   （`scripts/ci.ps1`：Debug/Release 构建与测试 → 界面验收 → 打包 → 发布门）。
   写代码过程中只做**编译级**自检（改完能编过、逻辑自洽），验收留到最后。
3. **测试与实现一起提交**：验收脚本、测试夹具和单元测试属于本次功能的一部分，
   不允许「先合功能、测试以后补」。
4. **验收项必须有负例守卫**，且守卫要自带「我确实破坏了输入吗」的自检——
   否则守卫自己坏掉时，门会一直绿着，把产品缺陷伪装成通过。

### 界面验收会抢桌面

`verify-m0` / `verify-pdf` / `verify-scroll` / `verify-text` / `verify-navigation` /
`verify-annotations` / `verify-pages` / `verify-index` / `verify-ai` / `verify-qa` / `verify-privacy` / `verify-ocr` / `verify-proposals`
这十三个脚本要驱动真实界面：
把窗口抢到前台、置顶、挪到屏幕左上角，并用合成的鼠标事件点屏幕坐标。
**在开发机上跑会打断正在用这台机器的人**（真实指针会被挪走，窗口会突然跳出来）。

因此：

- 共享的开发机上跑流水线时加 `-SkipAcceptance`，只跑构建、测试与发布门：

  ```powershell
  powershell -ExecutionPolicy Bypass -File scripts\ci.ps1 -SkipAcceptance
  ```

- 完整的界面验收交给 CI（runner 是独立会话，照跑不误）。
- 验收用的应用实例把设置写到临时目录（`AIPDF_TRACE_FILE` 存在时），
  不会覆盖使用者自己的窗口布局与面板可见性；
  脚本跑完还会把鼠标指针放回原位。
- 验收环境变量：
  `AIPDF_SKIP_RECOVERY_PROMPT`（由 `scripts/AcceptanceProbe.ps1` 统一设置）
  跳过启动时的恢复问询——那个问询是模态弹窗，会挡住验收要点的界面；
  `AIPDF_RECOVERY_DIR` 把恢复日志放到临时目录，各脚本互不干扰；
  `AIPDF_INDEX_DIR` 把本地索引放到临时目录，索引验收脚本之间互不干扰，
  也不会写进使用者自己的索引目录；
  `AIPDF_AI_MOCK_DELAY_MS` 把演示模式的吐字速度调慢，
  取消功能的验收才有确定的时序（默认 30ms 一段，答案 1 秒内就吐完了）。

### 写代码时的硬约束

- UI 不直接调用 PDFium，所有引擎调用通过 `src/pdf` 适配层。
- 引擎调用（大纲、链接、文本层、渲染）都在后台线程做，主线程不碰 PDFium 句柄；
  跨线程只回传纯数据，句柄由各自的工作线程自己 `isolatedCopy`。
- 新增或修改的 `.ps1` 必须是 UTF-8 BOM（PowerShell 5.1 没有 BOM 会按 GBK 解析中文脚本），
  行尾按 `.gitattributes` 用 CRLF 检出。
- 提交信息用 `git commit -F <文件>`：消息里带 ASCII 双引号会被参数解析拆开。

## 开发原则

- 不将第三方商业应用二进制作为产品构建依赖。
- UI 不直接调用 PDFium，所有引擎调用通过适配层。
- AI 不直接修改 PDF；变更必须转为可验证、可撤销的命令。
- 文档内容是不可信数据，不能扩大模型或工具权限。
- 密钥不写入源码、普通配置文件或日志。
