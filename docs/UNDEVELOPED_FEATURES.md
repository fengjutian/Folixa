# 尚未开发或尚未完整交付的功能

更新日期：2026-11-24

## 结论

按照项目当前的功能矩阵，276 个对标命令中：

- `aligned`：239 项
- `partial`：8 项
- `missing`：0 项
- `excluded`：6 项
- `na`：23 项

因此，目前没有被矩阵登记为完全缺失的核心功能。剩余工作主要是部分能力补齐、真实环境验证，以及底层实现与界面入口之间的状态统一。

数据来源：

- `docs/benchmark/foxit-features.csv`
- `docs/FEATURE_MODIFICATION_BACKLOG.md`
- `src/app/shell/MainWindow.cpp`

## 建议继续开发的功能

### 1. 接通静态 XFA 的界面编辑入口

XFA 后端已经支持字段提取、字段值改写和部分计算脚本执行，但 Ribbon 中的“编辑静态 XFA”仍为禁用命令，普通用户无法从该入口操作。

相关位置：`src/app/shell/MainWindow.cpp:2080`。

建议：

- 为字段读取和修改提供正式对话框。
- 将已有的 `writeXfaFieldValue`、`writeXfaFieldValues` 等能力接入界面。
- 明确提示动态 XFA、完整布局、事件与 SOM 运行时不在支持范围内。
- 增加真实 UI 验收测试。

### 2. 补齐扫描仪兼容性和真机验证

当前扫描功能已经实现 WIA 设备枚举、取帧和多页 PDF 生成，但仍有以下边界：

- 只支持 WIA，不支持 TWAIN。
- 支持 BMP、JPEG、PNG，不支持 TIFF。
- 模拟帧源的自动测试已经通过，但扫描仪真机取帧尚未验证。

建议：

- 增加 TWAIN 支持，或明确维持 WIA-only 的产品范围。
- 增加 TIFF 输入及多页 TIFF 处理。
- 建立至少一组真实扫描仪的验收环境。

### 3. 开发原生 OFD 读取与转换能力

当前 OFD 批量转换依赖用户自行安装外部转换器，应用本身没有 OFD 读取引擎。没有外部工具时，该能力不可用。

建议：

- 评估引入固定版本的 OFD 解析依赖，或开发最小可用的 OFD 读取层。
- 支持直接预览 OFD，而不仅是转换为 PDF。
- 保留现有外部转换器作为兼容或降级路径。

### 4. 完成 PDF 优化和 PDF/A 认证链路验证

PDF 优化的真实压缩路径依赖 Ghostscript；当前环境没有安装 Ghostscript，因此该路径尚未完整实跑。PDF/A 正式认证依赖 veraPDF，应用内置能力只能进行结构预检。

建议：

- 在 CI 或专用验证机安装并固定 Ghostscript 版本。
- 对压缩率、文字可搜索性、图片质量和逐页渲染差异建立验收门。
- 安装并固定 veraPDF，区分“内部预检”和“标准认证”。

### 5. 补齐高级打印的应用侧能力

双面打印、多页合一等能力目前取决于打印机驱动，应用侧没有统一实现和跨设备保证。

建议：

- 对多页合一进行应用侧排版和栅格化，减少驱动差异。
- 明确区分应用提供的打印能力和驱动提供的能力。
- 使用实体打印机和虚拟打印机进行组合测试。

### 6. 完成 OneNote 真机集成验证

OneNote 导出代码和自动测试已经完成，但依赖 Windows 上的 OneNote 桌面版与 COM。目前尚未验证真实写入以及 OneNote 中的文字搜索效果。

建议：

- 在安装 OneNote 桌面版的机器上执行端到端验收。
- 验证多页文档、中文文字层、特殊字符和大文档。
- 验证 COM 初始化失败、首次启动未配置和写入中断等场景。

### 7. 完成 AI 助手的真实 Provider 验收

当前已内置 DeepSeek、Kimi、Qwen、MiniMax 和本机 Ollama 等 Provider 配置，但仍需真实服务验证模型响应格式、流式输出、异常处理和兼容性。

顶部快捷项中的“改写”和“校对”仍保留“将在后续版本中提供”的文案，也需要确认是否已经接入实际动作。

相关位置：`src/app/shell/MainWindow.cpp:2820`。

建议：

- 为每个 Provider 建立最小在线冒烟测试。
- 验证流式输出、取消、超时、限流和错误响应。
- 将“改写”“校对”等快捷入口接到现有 AI 面板能力，或移除误导性入口。

## 界面与实现状态不一致

### “添加 3D”旧占位入口

编辑页附件组中仍有一个禁用的“添加 3D”按钮：`src/app/shell/MainWindow.cpp:2023`。

但项目已经存在可用的“插入 3D 模型”和“查看 3D 模型”入口，功能矩阵也将 3D 标记为 `aligned`。该按钮属于遗留占位，并不代表 3D 能力完全未开发。

建议删除旧按钮，或让它跳转到已有的 3D 插入功能。

### XFA 状态不一致

功能矩阵把静态 XFA 标记为 `aligned`，但 Ribbon 中的编辑入口仍然禁用。需要统一以下口径之一：

- 接通界面后继续保持 `aligned`；或
- 在界面完成前将矩阵状态调整为 `partial`。

## 明确不在产品范围内的功能

以下功能被登记为 `excluded` 或 `na`，属于产品决策，不应直接统计为开发遗漏：

- SharePoint、iManage、eDOCS 等企业内容管理连接器。
- 第三方插件 SDK、插件管理和插件商店。
- 云文档和云协作。
- 手机遥控。
- 商业激活、购买和订阅。
- 云端对内/对外电子签平台及邀请他人签署。
- CAJ 输入。
- 企业敏感度标签。

## 推荐优先级

1. 接通静态 XFA 的 UI。
2. 清理 3D、AI 快捷项等遗留占位和状态冲突。
3. 增加 TWAIN、TIFF，并完成扫描仪真机验证。
4. 建设原生 OFD 读取能力。
5. 完成 Ghostscript、veraPDF 的固定依赖与端到端验证。
6. 完成 OneNote 和 AI Provider 的真实环境验收。
7. 改进高级打印的应用侧排版能力。

## 维护说明

后续修改功能状态时，应同步更新：

1. `docs/benchmark/foxit-features.csv`；
2. `docs/FEATURE_MODIFICATION_BACKLOG.md`；
3. 对应的代码入口和测试；
4. 文档一致性检查结果。

可使用以下脚本重新生成和检查相关文档：

```powershell
python scripts/dump-feature-modification-list.py
python scripts/check-docs-consistency.py
```
