# AI PDF Editor

AI PDF Editor 是一个采用 C++20、Qt 6 和 PDFium 规划实现的原生 PDF 编辑器。当前版本处于 M0 工程骨架阶段，已经包含桌面应用、独立 AI Worker 和本地 IPC 健康检查。

## 当前能力

- Qt Widgets 主窗口和多文档标签框架
- 文档视图、缩略图、书签和 AI 助手面板占位
- 独立 `PdfAiWorker` 进程
- 基于 `QLocalSocket`/`QLocalServer` 的版本化 JSON Lines IPC
- Worker 自动启动、连接、超时和健康检查
- IPC 协议单元测试与 Worker 集成测试
- 可重复执行的启动验收脚本

PDF 渲染、索引和真实模型 Provider 将在后续里程碑接入。

## 构建环境

- CMake 3.25+
- Ninja
- C++20 编译器（当前固定 MinGW-w64 GCC 13.1.0）
- Qt 6.8.3（官方二进制套件 `mingw_64`）

Qt 与 MinGW 工具链不需要管理员权限，用 `aqtinstall` 直接装到本地目录：

```powershell
pip install aqtinstall
aqt install-qt windows desktop 6.8.3 win64_mingw --outputdir C:\Qt --archives qtbase
aqt install-tool windows desktop tools_mingw1310 qt.tools.win64_mingw1310 --outputdir C:\Qt
```

工具链路径写在 `cmake/toolchains/qt-mingw.cmake`，也可用环境变量覆盖：

```powershell
$env:AIPDF_QT_ROOT = 'C:\Qt\6.8.3\mingw_64'
$env:AIPDF_MINGW_ROOT = 'C:\Qt\Tools\mingw1310_64'
```

## Windows 构建

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

Release 构建与测试：

```powershell
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

脚本会启动主程序，检查 Worker 拉起、就绪状态、版本显示、健康检查返回值与退出清理，
可额外用 `-ScreenshotPath` 保存窗口截图。

## 文档

- [技术设计](docs/AI_PDF_EDITOR_TECHNICAL_DESIGN.md)
- [版本与任务管理规范](docs/VERSIONING.md)
- [版本任务索引](docs/versions/README.md)
- [依赖与许可证政策](docs/DEPENDENCY_POLICY.md)
- [IPC 协议](docs/IPC_PROTOCOL.md)
- [第三方许可证清单](THIRD_PARTY_LICENSES.md)

本项目实行“一项主要任务对应一个软件版本”。当前开发版本为
[`0.1.0`](docs/versions/v0.1.0.md)，只有当前版本通过验收后才进入下一版本。

## 开发原则

- 不将第三方商业应用二进制作为产品构建依赖。
- UI 不直接调用 PDFium，所有引擎调用通过适配层。
- AI 不直接修改 PDF；变更必须转为可验证、可撤销的命令。
- 文档内容是不可信数据，不能扩大模型或工具权限。
- 密钥不写入源码、普通配置文件或日志。
