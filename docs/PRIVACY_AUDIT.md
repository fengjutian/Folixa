# 隐私与许可证审计（v0.15.0）

**审计日期：** 2026-11-14  
**范围：** 主程序与 AI Worker 在本机留下的全部数据、出本机的数据路径、随包分发的第三方组件与许可证。  
**结论：** 未发现需要阻断 1.0 的问题。2 项在本版本修掉，3 项接受并记录理由。

审计不是一次性的声明：能被自动检查的部分已经做成发布门
（`scripts/verify-dependencies.ps1`、`scripts/verify-logs.ps1`、
`scripts/verify-licenses.ps1`、`scripts/verify-privacy.ps1`、`scripts/verify-sbom.ps1`），
每次流水线都会重新证明一遍。

## 1. 数据清单

| 数据 | 什么时候产生 | 存在哪 | 出本机 | 保留与删除 |
|---|---|---|---|---|
| 文档原文、渲染位图、文本布局 | 打开、翻页、搜索 | 只在进程内存 | 否 | 关窗即释放 |
| 本地索引（块文本、坐标、章节路径、向量缓存） | 打开文档后台建索引 | `%LOCALAPPDATA%\Folixa\index`（`AIPDF_INDEX_DIR` 可改） | 否 | 随文档内容复用；不自动过期 |
| 恢复日志（文档路径 + 编辑命令） | 文档被修改且未保存 | `%LOCALAPPDATA%\Folixa\recovery\<sha1>.json`（`AIPDF_RECOVERY_DIR` 可改） | 否 | 保存成功或用户选择丢弃后删除 |
| AI 配置（provider、base URL、模型、单价、数据模式） | 在设置对话框保存 | `%LOCALAPPDATA%\Folixa\ai\config.json` | 否 | 用户修改前一直保留 |
| API 密钥 | 在设置对话框保存 | Windows 凭据管理器，目标 `Folixa/ai/<服务商 id>/api-key`（不同服务商各存各的） | 否 | 用户点「删除密钥」时删除 |
| AI 审计 | 每次云端请求/提案确认 | `%LOCALAPPDATA%\Folixa\ai\audit.jsonl` | 否 | 追加写，不自动轮转 |
| 云端请求 | 用户点「发送」且数据模式允许 | 发往配置的 `baseUrl` | **是**，仅当前页/选区或检索出的块，上限 4000 字 | 由服务商策略决定 |
| 本机模型请求（Ollama 等） | 用户点「发送」，服务地址指向本机（`127.0.0.1` / `localhost` / `::1`） | 发往本机的 `baseUrl`（如 `127.0.0.1:11434`） | **否**：请求不出这台机器，文档内容不出本机 | 由本机模型决定 |
| OCR 页面图 | 扫描件识别 | `QTemporaryDir` 下的临时 PNG，进程退出即删 | 否 | 正常结束删除；崩溃时可能残留，重启后由系统临时目录清理 |
| 诊断轨迹 | 仅当设置 `AIPDF_TRACE_FILE`（验收/排查） | 环境变量指定的文件 | 否 | 由调用方清空；默认脱敏 |

## 2. 审计发现

### F1（已修复）提案审计写入了文件绝对路径

`appendProposalAudit()` 原先把 `documentPath` 原样写进 `audit.jsonl`。审计的原则是
「只记发生了什么，不记内容」，绝对路径会把用户名与目录结构带进一个本该只有元数据的文件。
现在只落文件名，路径不再出现；回归测试
`tests/DataPolicyTests.cpp::proposalAuditRecordsOnlyFileName` 守这条线
（用带 `Users/alice/secret-project` 的临时路径断言审计里没有 `alice`）。

### F2（已修复）诊断轨迹默认写入选区文字、复制内容与搜索词

`PageCanvas`、`PdfView`、`TextController` 的轨迹原本直接拼原始文本。轨迹只在设置
`AIPDF_TRACE_FILE` 时才写，但它仍然是一份可能落盘的文档内容副本。现在三处都经过
`src/app/TraceText.h::traceText()`：默认写 `<redacted:长度>`，只有显式设置
`AIPDF_TRACE_TEXT=1` 才写原文。验收脚本在 `scripts/AcceptanceProbe.ps1` 里统一打开这个开关，
所以「拖选到了什么、复制了什么、搜了什么」的验收强度没有降低。

### F3（接受）OCR 临时图片会短暂落盘

Tesseract 路径必须把页面渲染成 PNG 交给命令行程序（`src/ocr/TesseractCliEngine.cpp`），
文件放在 `QTemporaryDir` 里，作用域结束即删。选择接受而不是改成管道：Tesseract 不支持从
stdin 读图；临时目录在用户自己的 `%TEMP%` 下，权限继承用户配置。崩溃时残留的图片
不含元数据，且会被系统清理。

### F4（接受）本地索引与恢复日志不加密

两者都只含用户自己文档的内容/路径，落在用户自己的 `%LOCALAPPDATA%`，不经过网络。
加密需要一个密钥管理方案（DPAPI 或用户口令），那是 1.6.0「加密与脱敏」的范围；
在 1.6 之前不引入半套方案。清理方式：删除 `%LOCALAPPDATA%\Folixa\index` 与
`...\recovery`（`AIPDF_INDEX_DIR` / `AIPDF_RECOVERY_DIR` 指向别处时删对应目录）。

### F5（接受）审计文件不轮转

`audit.jsonl` 每次请求/提案追加一行元数据，长期使用会持续增长。它不含正文与密钥，
危害上限是磁盘占用。轮转与保留期属于 1.6.0 的文档生命周期工作；当前规模（单行数百字节）
不构成发布阻断。

## 3. 许可证审计

- 依赖登记表 `THIRD_PARTY_LICENSES.md` 已从「Pending version pin」占位改成可核对的事实：
  Qt、MinGW-w64、PDFium（含 19 份内置第三方许可证）与可选 Tesseract 各自有精确版本、
  关系、许可证与更新路径；SQLite / OpenSSL 明确不是本项目的依赖，已从登记表删除。
- `licenses/CHECKSUMS.txt` 的 3 份全文与 `licenses/pdfium-CHECKSUMS.txt` 的 19 份全文
  由 `fetch-licenses.ps1 -VerifyOnly` 与打包阶段的逐份哈希校验；发布门 `verify-licenses.ps1`
  再核对包内文件集合（不少也不多）。
- 包内每个 DLL/EXE 都必须能归到登记表里的组件；v0.15.0 之前 `windeployqt` 会顺带复制
  `D3Dcompiler_47.dll`（微软 DirectX 运行库），它既不在登记表里也没有随包许可证全文。
  现在打包显式加 `--no-system-d3d-compiler --no-system-dxc-compiler`，不再分发它；
  依赖门会把任何未登记的二进制挡在发布之外。
- 项目自身仍是「保留所有权利」，安装包是内部开发产物；这条没有变化，也不在本版本处理。

## 4. 复核命令

```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify-dependencies.ps1
powershell -ExecutionPolicy Bypass -File scripts\fetch-licenses.ps1 -VerifyOnly
powershell -ExecutionPolicy Bypass -File scripts\verify-logs.ps1 -SelfTest
powershell -ExecutionPolicy Bypass -File scripts\verify-privacy.ps1   # 会抢桌面
```

2026-11-14 本机结果：依赖门通过（登记表与 Qt 6.8.3 / MinGW 13.1.0 /
PDFium chromium/8152 / API 159 一致，仓库无提交二进制）；许可证 3 份全文通过；
敏感日志扫描通过（自检的假密钥被抓到、干净文件不误报）。
`verify-privacy.ps1` 的界面场景由完整流水线执行，结果记在 v0.15.0 完成记录里。

## 5. 未覆盖 / 下一次

- 服务商侧的保留与训练策略不受本项目控制，只能在界面上如实展示「发到哪」；
  对隐私要求高的用户应使用本地模式（选 Ollama 这类本机模型时内容同样不出本机，
  判定见 `ai::isLoopbackUrl`，由 `tests/ProviderPresetTests.cpp` 守住）。
- 依赖 CVE 复核是人工判断（Qt 6.8.3 / PDFium 8152 / MinGW 13.1.0 当日无已知未修补高危漏洞），
  自动门只保证「复核过的对象没有被悄悄换掉」。下一次复核在 1.0.0 发布前与每次升级 PDFium 时。
- 索引与恢复日志的加密、审计轮转归入 1.6.0。
