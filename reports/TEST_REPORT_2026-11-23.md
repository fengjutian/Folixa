# AI PDF Editor 测试报告

**日期：** 2026-11-23
**版本：** 1.0.0（HEAD `5bf958e`）
**构建：** `build/windows-debug`（Debug / Qt 6.8.3 / MinGW 13.1 / Ninja）
**范围：** 构建、单元与集成测试、非 GUI 静态门禁。界面验收只跑了前两项，其余因桌面被占用不具判定力（见第 6 节）。

## 1. 结论

- 构建通过。
- `ctest` 共 **208** 项：**修复后 208 通过，0 失败**（修复前为 206 通过、2 失败）。
- 两个失败都是**最近 UI 改动没有同步主题/翻译表**，不是功能缺失；已按第 9 节修完并复验。
- 非 GUI 门禁（文档一致性、功能声称、partial 边界、权限覆盖、版本、依赖、敏感日志）**全部通过**。
- 没有证据表明“核心功能大面积不通”；目前所见红色集中在**深色主题**与**英文界面**两处。

## 2. 失败项 1：深色主题残留浅色（已修复，见 9.1）

`ThemeTests`：7 通过、2 失败。

```
FAIL!  ThemeTests::everyStylesheetColorHasADarkCounterpart()
       文字色漏登记：#424242
FAIL!  ThemeTests::darkStylesheetHasNoLeftoverLightColors()
       色板里还残留浅色：#D9DEE8
```

- `#424242`：`src/app/ui/Theme.cpp:106`，工具栏“工作台”按钮的文字色，引入于 `b84f50d`。
- `#D9DEE8`：`src/app/ui/Theme.cpp:83`，`QMenu` 描边，引入于 `a4024a9`。
- 两处都没有登记进 `darkTextColors()` / `darkSurfaceColors()`。暗色主题下这两个颜色会被原样保留：菜单描边发白、工作台按钮文字偏黑。
- **修法：** 在 `darkTextColors()` 给 `#424242` 一个暗色文字值（如 `#C3CEDD`），在 `darkSurfaceColors()` 给 `#D9DEE8` 一个暗色描边值（如 `#344156`），与相邻色阶保持一致。

## 3. 失败项 2：英文翻译漏 9 条（已修复，见 9.2）

`I18nCoverageTests`：源码待翻译 3440 条，已覆盖 3431 条，**漏翻 9 条**：

| 上下文 | 文案 |
|---|---|
| `aipdf::app::AiPanel` | Folixa AI |
| `aipdf::app::PageCanvas` | Ctrl+Enter 保存 · Esc 取消 · 点击外部取消 |
| `aipdf::app::PageCanvas` | 就地文字编辑器 |
| `aipdf::app::PageCanvas` | 已取消文字编辑，PDF 未修改 |
| `aipdf::app::PdfView` | 无法访问系统剪贴板 |
| `aipdf::app::PdfView` | 正在编辑文字：按 Ctrl+Enter 保存，按 Esc 或点击外部取消 |
| `aipdf::app::PdfView` | 没有可复制的文字 |
| `aipdf::app::SettingsDialog` | 显示菜单栏（与 Ribbon 同时显示） |
| `aipdf::app::SettingsDialog` | 界面模式 |

影响：英文界面下这些位置回退成中文。主要来自就地文字编辑（`dc92486`）、菜单栏开关（`63a2133`）两次提交。

另有 **204 条陈旧词条**（目录里有、源码里已找不到）。不影响运行，建议顺手清理。

**修法：** 在 `src/i18n/ai-pdf-editor_en.ts` 补 9 条英文翻译，再跑 `ctest -R I18nCoverageTests`。

## 4. 非 GUI 门禁结果

| 门禁 | 命令 | 结果 |
|---|---|---|
| 文档一致性 | `check-docs-consistency.py` | ✅ 矩阵 276 条（aligned 239 / 86.6%）、评分卡 102 项、欠账 0，三份文档数字对齐 |
| 功能声称 | `check-feature-claims.py` | ✅ 239 条 aligned 行的降级措辞都有登记豁免 |
| partial 边界 | `check-partial-boundaries.py` | ✅ 8 条 partial 都写清了差在哪 |
| 权限覆盖 | `check-permission-coverage.py` | ✅ 改文档入口 47 个，42 个有权限检查、5 个登记豁免 |
| 版本一致性 | `check-version.ps1` | ✅ 1.0.0 / Released，索引一致 |
| 依赖登记 | `verify-dependencies.ps1` | ✅ 固定版本、许可证、更新路径、无夹带二进制 |
| 敏感日志 | `verify-logs.ps1 -SelfTest` | ✅ 自检通过，13 个文件无密钥形态字符串 |

未跑：`check-test-counts.py`。该门禁会**逐个执行 208 个测试可执行文件**取 `Totals`，本地跑会重复整轮测试并可能弹出测试窗口，本轮主动终止。

## 5. 构建记录

- 首次 `cmake --build --preset windows-debug` 在链接 `AiPdfEditor.exe` 时失败：`cannot open output file ... Permission denied`。
- 原因：一个 13:08 启动的 `AiPdfEditor.exe` 实例占用了 exe。
- 关闭该实例后重新链接成功，主程序 342,524,839 字节（Debug）。
- 其余 210 个构建目标在第一次就已成功。

## 6. 界面验收：只跑通两项，其余不作数

| 脚本 | 结果 | 说明 |
|---|---|---|
| `verify-m0.ps1`（启动/IPC） | ✅ 全过 | 主程序、Worker、健康检查、强杀回收 |
| `verify-pdf.ps1`（打开/渲染/密码） | ✅ 24/24 | 加密文档、错误密码、取消均正确 |
| `verify-scroll.ps1`（连续滚动） | ✅ 11/11 | |
| `verify-text.ps1` | ❌ 7 过 6 失败 | 剪贴板只拿到 `'Fol'`，搜索状态为空 |
| `verify-navigation.ps1` | ❌ 18 过 13 失败 | |
| `verify-annotations.ps1` | ❌ 4 过 27 失败 | |
| `verify-pages.ps1` | ❌ 5 过 19 失败 | 日志反复出现 `worker state=starting` |
| `verify-index.ps1` 及其后 14 项 | 未跑 | 用户要求在不停用桌面的前提下停止 |

这些脚本会合成鼠标事件、抢占前台窗口，并要求固定窗口位置与焦点。运行时用户正在使用桌面，且日志里出现点击落空、剪贴板被抢占、`worker state=starting` 等环境特征，**不能据此判定功能不通**。要得到可信结论，需在独占桌面的会话（CI 或在无人使用机器时）重跑第 6 节全部脚本。

## 7. 建议的下一步

1. ~~补 `#424242` / `#D9DEE8` 两个暗色映射~~ —— 已做，实际漏登记不止两个（见第 9 节）。
2. ~~补 9 条英文翻译~~ —— 已完成；204 条陈旧词条暂未清理。
3. ~~复验两个失败测试~~ —— 已复验，全套 208 项全绿。
4. 界面验收挑一个无人使用桌面的时间窗跑完整套；本文第 6 节的失败项在那种环境下重新判定。

## 8. 复现命令

```powershell
cmake --build --preset windows-debug
ctest --test-dir build/windows-debug --output-on-failure
python scripts\check-i18n.py --src src --ts src\i18n\ai-pdf-editor_en.ts
python scripts\check-docs-consistency.py
python scripts\check-feature-claims.py
python scripts\check-partial-boundaries.py
python scripts\check-permission-coverage.py
powershell -File scripts\check-version.ps1
powershell -File scripts\verify-dependencies.ps1 -RepositoryRoot .
```

原始日志：`build/test-ctest.log`、`build/i18n-current.log`、`build/gate-*.log`、`build/acceptance-suite/*.log`。

## 9. 修复记录（2026-11-23）

### 9.1 深色主题漏登记（`src/app/ui/Theme.cpp`）

`ThemeTests` 的 `QVERIFY2` 一次只报第一个失败，所以第一轮看到的 2 个只是冰山一角。改用静态比对（`build/analyze-theme-colors.py`：按 `color:` 位置把浅色样式表拆成文字色/表面色，再和两张暗色表对账）后，一次找全：

- 文字色漏 4 个：`#424242`、`#242424`、`#344054`、`#667085`；
- 表面色漏 16 个：`#D9DEE8`、`#E0E0E0`、`#E1E4EA`、`#EAECF0`、`#D9E1EC`、`#E8E8EB`、`#E8E8EC`、`#EEF0F4`、`#D3DCEB`、`#C8D1DF`、`#9FB4D7`、`#7F9BCB`、`#FAFAFA`、`#F5F5F5`、`#F5F6F8`、`#24499D`（`#24499D` 同时作主色文字和主色按钮底，两张表都要有）；
- 半透明叠加漏 1 个：`rgba(255, 255, 255, 245)`。

补的值都按相邻色阶取：文字 `#C3CEDD`/`#EDF2F8`/`#8491A5`，描边 `#2B374B`–`#46566F`，主色底 `#3E63BE`，半透明沿用 `rgba(27,36,50,0.94)`。

### 9.2 英文翻译（`src/i18n/ai-pdf-editor_en.ts`）

补齐第 3 节列出的 9 条。插入时踩到一个坑：文件里有些 `</context>` 顶格写、有些缩进 4 空格，按缩进找边界会把 7 条插进下一个 context；改成「名字后的第一个 `</context>`」后 3440/3440 全覆盖。

### 9.3 顺带修掉的门禁 bug（`scripts/check-i18n.py`）

补完翻译后 `I18nCoverageTests` 仍然红，但手动跑脚本退出码是 0。原因是脚本最后打印 204 条陈旧词条时，其中含 `▸`（U+25B6），Windows 控制台是 GBK，`print` 抛 `UnicodeEncodeError`。现在脚本启动时把 stdout/stderr 重设为 UTF-8（`errors="replace"`），提示信息不再能把门禁判死。

### 9.4 复验

```
ctest --test-dir build/windows-debug -j 4
100% tests passed, 0 tests failed out of 208
Total Test time (real) = 45.45 sec
```

改动文件：`src/app/ui/Theme.cpp`、`src/i18n/ai-pdf-editor_en.ts`、`scripts/check-i18n.py`。未提交。
