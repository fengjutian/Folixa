# AI PDF Editor

AI PDF Editor 是一个采用 C++20、Qt 6 和 PDFium 规划实现的原生 PDF 编辑器。当前版本已建立可复现的 Windows 构建、测试与打包环境：主程序、独立 AI Worker、本地 IPC 健康检查、自动化测试和可独立运行的开发包。

## 当前能力

- Qt Widgets 主窗口和多文档标签框架
- 文档视图、缩略图、书签和 AI 助手面板占位
- 独立 `PdfAiWorker` 进程
- 基于 `QLocalSocket`/`QLocalServer` 的版本化 JSON Lines IPC
- Worker 自动启动、连接、超时和健康检查
- IPC 协议单元测试与 Worker 集成测试
- 固定工具链、CI、可重复的打包与自动化验收

PDF 渲染、索引和真实模型 Provider 将在后续里程碑接入。

## 构建环境

固定组合：

| 组件 | 版本 | 说明 |
|---|---|---|
| Qt | 6.8.3 `mingw_64` | 官方二进制套件，仅 qtbase |
| MinGW-w64 | GCC 13.1.0 | Qt 官方工具包 |
| CMake | 3.25+ | 系统安装 |
| Ninja | 1.10+ | 系统安装 |

一条命令装好 Qt 与 MinGW（免管理员权限）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\bootstrap-toolchain.ps1
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

主程序与 Worker 输出到同一个 `bin` 目录，运行：

```powershell
.\build\windows-debug\bin\AiPdfEditor.exe
```

主程序会自动拉起同目录下的 `PdfAiWorker.exe` 并完成健康检查。

## 启动验收

```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify-m0.ps1
```

启动主程序，检查 Worker 拉起、就绪状态、版本显示、健康检查返回值与退出清理，
可用 `-ScreenshotPath` 保存窗口截图。

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

## 持续集成

`.github/workflows/windows.yml` 在 `windows-2022` 上执行与上面完全相同的命令：
安装固定工具链 → Debug/Release 构建与测试 → 启动验收 → 打包 → 无 SDK 打包验收 → 上传安装包。

## 文档

- [技术设计](docs/AI_PDF_EDITOR_TECHNICAL_DESIGN.md)
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
