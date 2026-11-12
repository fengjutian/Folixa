# AI PDF Editor

AI PDF Editor 是一个采用 C++20、Qt 6 和 PDFium 规划实现的原生 PDF 编辑器。当前版本已建立可复现的 Windows 构建、测试与打包环境：主程序、独立 AI Worker、本地 IPC 健康检查、自动化测试和可独立运行的开发包。

## 当前能力

- Qt Widgets 主窗口和多文档标签框架
- 文档视图、缩略图、书签和 AI 助手面板占位
- **用 PDFium 打开 PDF 并渲染首页**（支持密码保护文件、旋转页、缩放与高 DPI）
- **连续页面视口与瓦片渲染**：按视口渲染、邻页预取、LRU 缓存（按像素字节计量上限）、
  渲染任务按代取消，500 页文档可连续浏览
- 缩放、用户旋转、适应宽度与适应页面
- **文本提取、选择、复制与全文搜索**：后台提取文本层、
  鼠标拖选与双击选词、Ctrl+C 复制、Ctrl+F 搜索面板（结果列表、上一处/下一处、停止）、
  命中高亮与跳转；长文档搜索可随时取消
- 独立 `PdfAiWorker` 进程
- 基于 `QLocalSocket`/`QLocalServer` 的版本化 JSON Lines IPC
- Worker 自动启动、连接、超时和健康检查
- Worker 跟随主程序退出：主程序被强制结束时 Worker 自行退出，不留孤儿进程
- IPC 协议单元测试、Worker 集成测试、PDFium 适配层测试与视口/缓存单元测试
- 固定工具链、CI、可重复的打包与自动化验收

PDF 索引、文本提取和真实模型 Provider 将在后续里程碑接入。

## 构建环境

固定组合：

| 组件 | 版本 | 说明 |
|---|---|---|
| Qt | 6.8.3 `mingw_64` | 官方二进制套件，仅 qtbase |
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

- [技术设计](docs/AI_PDF_EDITOR_TECHNICAL_DESIGN.md)
- [Prism Document UI 设计规范](docs/UI_DESIGN_SYSTEM.md)
- [版本与任务管理规范](docs/VERSIONING.md)
- [版本任务索引](docs/versions/README.md)
- [依赖与许可证政策](docs/DEPENDENCY_POLICY.md)
- [IPC 协议](docs/IPC_PROTOCOL.md)
- [第三方许可证清单](THIRD_PARTY_LICENSES.md)

本项目实行“一项主要任务对应一个软件版本”。当前开发版本为
[`0.2.0`](docs/versions/v0.2.0.md)，只有当前版本通过验收后才进入下一版本。

## 开发原则

- 不将第三方商业应用二进制作为产品构建依赖。
- UI 不直接调用 PDFium，所有引擎调用通过适配层。
- AI 不直接修改 PDF；变更必须转为可验证、可撤销的命令。
- 文档内容是不可信数据，不能扩大模型或工具权限。
- 密钥不写入源码、普通配置文件或日志。
