# Folixa Prism Document UI 设计规范

**文档版本：** 1.0  
**产品：** Folixa AI PDF Editor  
**设计系统：** Prism Document  
**技术基线：** C++20、Qt 6 Widgets、QPainter  
**平台：** Windows 优先，预留 macOS/Linux 适配  
**状态：** 设计基线，待版本任务批准后实施

## 1. 文档目的

本文定义 Folixa 的品牌视觉、空间模型、设计 Token、组件状态、关键页面、AI 交互、Qt 实现边界和验收方法。它既是设计规范，也是研发、测试和无障碍验收的共同契约。

当前代码版本为 `0.1.0`，只包含 Qt 主程序、AI Worker 和 IPC 工程骨架；Qt 依赖尚未在本机安装，PDFium、文档渲染和真实模型 Provider 尚未接入。因此本文中的 PDF 与 AI 界面属于目标规范，不代表当前已实现能力。

## 2. 产品定位

### 2.1 产品主张

> A refined workspace for documents and intelligence.

Folixa 是围绕文档构建的智能工作空间，而不是传统 PDF 工具栏的复制，也不是附带 PDF 预览的聊天应用。

### 2.2 设计目标

- 文档始终是视觉中心。
- 高频操作快速、明确、可逆。
- AI 与页面、选区、引用和操作上下文直接连接。
- 复杂功能按需出现，不制造永久视觉负担。
- 浅色、深色、高 DPI 和键盘操作具有一致体验。
- 设计系统可独立演进，并可以由真实 Qt 组件验证。

### 2.3 设计权重

| 维度 | 权重 |
|---|---:|
| 信息清晰度 | 40% |
| 空间层次 | 25% |
| 材质表现 | 20% |
| 动态效果 | 15% |

如果视觉效果与信息清晰度、响应速度或可访问性冲突，应优先后者。

## 3. 核心原则

### 3.1 Reading First

- PDF 工作区占据窗口最大连续区域。
- 面板默认可折叠、可调整，并记忆布局。
- 弹层不得遮挡用户当前操作的页面区域。
- 视觉装饰不得改变 PDF 本身的颜色和比例。

### 3.2 Spatial Depth

- 使用材质、边界、遮挡和轻阴影表达层级。
- 不依靠大面积渐变、发光和高饱和色制造层次。
- 同一 Z 层的组件使用一致阴影和边框。

### 3.3 Visual Restraint

- 一个视图最多只有一个强主操作。
- 蓝色用于主操作，紫色用于 AI 能力，语义色只表达状态。
- 常规工具优先使用中性色和 Ghost 形态。
- 不为每个控件增加阴影、模糊或动画。

### 3.4 Contextual Intelligence

- AI 回答必须显示上下文范围和引用状态。
- AI 快捷操作随选区、页面或文档上下文变化。
- AI 引用是独立结构化组件，不是回答正文中的普通页码。

### 3.5 User Control

- AI 文档变更必须经过“提案—校验—预览—确认—执行”。
- 修改进入统一撤销栈。
- 错误、取消和过期状态必须用文字说明，不能只改变图标或颜色。

## 4. 品牌基础

### 4.1 品牌属性

| 项目 | 定义 |
|---|---|
| 产品名称 | Folixa |
| 设计语言 | Prism Document |
| 产品类别 | Professional Document Workspace |
| 视觉风格 | Minimal + Controlled Liquid Glass |
| 品牌主色 | Prism Blue |
| AI 辅助色 | Intelligence Violet |
| 图标风格 | Rounded Linear |
| 默认主题 | Light |

Liquid Glass 是空间层次手段，不是所有控件的皮肤。PDF 页面使用 Paper，编辑确认使用 Solid，浮动菜单使用 Overlay，只有工具栏和侧面板可以使用受控 Glass。

### 4.2 命名与文案

- 用户界面使用“Folixa”，不使用全大写品牌名作为常规标题。
- 功能名采用动词或动宾结构，例如“打开 PDF”“总结当前页”。
- 错误文案说明问题、影响和可执行的下一步。
- AI 不使用人格化或夸大措辞，不声称“完全理解”文档。

## 5. 色彩系统

### 5.1 Prism Blue

| Token | Light value | 用途 |
|---|---|---|
| `prism.50` | `#F1F4FF` | 轻选中背景 |
| `prism.100` | `#E4EAFF` | Hover 背景 |
| `prism.300` | `#AABAF9` | 次级边框 |
| `prism.500` | `#5277E8` | 主操作、焦点 |
| `prism.600` | `#4267D8` | Hover |
| `prism.700` | `#3555B7` | Pressed |

### 5.2 Intelligence Violet

| Token | Light value | 用途 |
|---|---|---|
| `ai.50` | `#F6F2FF` | AI 轻背景 |
| `ai.100` | `#EEE8FF` | AI Hover |
| `ai.300` | `#C6B6F4` | AI 边框 |
| `ai.500` | `#8A72DF` | AI 主色 |
| `ai.600` | `#765ECC` | AI Hover |
| `ai.700` | `#634CB4` | AI Pressed |

### 5.3 Neutral

| Token | Light | Dark | 语义 |
|---|---|---|---|
| `neutral.0` | `#FFFFFF` | `#FFFFFF` | 真实纸张 |
| `foundation` | `#F5F7FA` | `#111827` | 应用背景 |
| `surface.primary` | `#FFFFFF` | `#1B2432` | 主表面 |
| `surface.secondary` | `#F7F8FA` | `#222D3D` | 次级表面 |
| `surface.tertiary` | `#F0F2F6` | `#2A3648` | Hover/分组 |
| `border.default` | `#E3E7EE` | `#344156` | 默认边框 |
| `border.strong` | `#CBD2DE` | `#4A5870` | 强边框 |
| `text.primary` | `#202A3A` | `#EDF2F8` | 标题与正文 |
| `text.secondary` | `#465267` | `#B5C0D0` | 次级文字 |
| `text.muted` | `#8792A3` | `#8491A5` | 辅助文字 |
| `text.disabled` | `#AAB2C0` | `#667287` | 禁用文字 |

深色主题默认不反色 PDF 页面。页面阅读模式单独提供 Original、Sepia、Night 和 High Contrast，不与应用主题绑定。

### 5.4 Semantic

| Token | 色值 | 用途 |
|---|---|---|
| `success` | `#27A77A` | 成功、完成 |
| `warning` | `#D99B3E` | 待确认、风险 |
| `error` | `#E15E6C` | 错误、危险操作 |
| `info` | `#438FCE` | 中性信息 |

### 5.5 高亮颜色

| 类型 | 填充 | 边界 |
|---|---|---|
| 用户文本选择 | `#DCE7FF` 70% | Prism Blue |
| AI 引用 | `#EEE8FF` 72% | Intelligence Violet |
| 搜索匹配 | `#FFF0A6` 75% | `#D99B3E` |
| 用户批注 | 用户选色 35% | 同色 80% |
| 错误区域 | `#FFE5E8` 75% | Error |

颜色不是唯一状态载体。选区、引用和搜索还需要不同的边界、标识或动画。

## 6. Typography

### 6.1 字体栈

| 内容 | 字体栈 |
|---|---|
| Windows UI | `Segoe UI Variable`, `Microsoft YaHei UI`, sans-serif |
| 可选品牌英文 | `Inter`, `Segoe UI Variable`, sans-serif |
| 等宽内容 | `Cascadia Mono`, `JetBrains Mono`, monospace |
| PDF 页面 | 使用文档自身字体 |

如果未随应用合法分发 Inter 或其他字体，应回退到系统字体，不允许在线动态下载字体。

### 6.2 字号 Token

| Token | 字号 | 行高 | 字重 | 用途 |
|---|---:|---:|---:|---|
| `display` | 28 | 38 | 600 | 欢迎页标题 |
| `h1` | 22 | 30 | 600 | 页面标题 |
| `h2` | 18 | 26 | 600 | 面板大标题 |
| `h3` | 16 | 24 | 600 | 分组标题 |
| `body` | 14 | 21 | 400 | 正文 |
| `body.small` | 13 | 19 | 400 | 控件与辅助内容 |
| `caption` | 12 | 18 | 400 | 状态、标签 |
| `micro` | 11 | 16 | 500 | 极少量辅助标识 |

不大量使用 300 字重。高 DPI、中文和弱视环境下必须保持可读性。

## 7. 尺寸与形状 Token

### 7.1 4px 间距体系

| Token | 值 |
|---|---:|
| `space.1` | 4 |
| `space.2` | 8 |
| `space.3` | 12 |
| `space.4` | 16 |
| `space.5` | 20 |
| `space.6` | 24 |
| `space.8` | 32 |
| `space.10` | 40 |
| `space.12` | 48 |

### 7.2 圆角

| Token | 值 | 用途 |
|---|---:|---|
| `radius.xs` | 4 | 小标签 |
| `radius.sm` | 6 | 小输入和状态块 |
| `radius.md` | 10 | 按钮和输入框 |
| `radius.lg` | 14 | 卡片和 Popover |
| `radius.xl` | 18 | 侧面板和大卡片 |
| `radius.2xl` | 22 | 大型对话框 |

PDF 页面圆角为 0–2px，不使用明显圆角。

### 7.3 控件高度

| Token | 值 | 用途 |
|---|---:|---|
| `control.small` | 30 | 紧凑工具控件 |
| `control.medium` | 36 | 默认按钮/输入 |
| `control.large` | 40 | 主要操作/聊天输入 |
| `toolbar.height` | 44 | 文档工具栏 |
| `tabbar.height` | 38 | 文档标签栏 |
| `statusbar.height` | 26 | 状态栏 |

所有值是 100% DPI 下的逻辑像素，由 Qt 高 DPI 机制缩放。

## 8. 材质系统

### 8.1 Foundation

- 应用最底层背景。
- 使用单一浅冷灰或深蓝灰。
- 不使用复杂渐变和动态纹理。

### 8.2 Glass

用于顶部工具栏、侧面板和少量浮动工具。基础实现采用半透明填充、细边框和轻阴影；系统背景模糊只是可选增强。

| 属性 | 范围 |
|---|---|
| 表面不透明度 | 84%–94% |
| 可选模糊半径 | 8–16px |
| 边框不透明度 | 30%–65% |
| 阴影模糊 | 12–24px |
| 阴影不透明度 | 8%–14% |

### 8.3 Paper

- 用于真实 PDF 页面。
- 保持原始颜色和比例。
- 白色、细边框、极轻阴影。
- 不使用透明、模糊和高饱和边缘。

### 8.4 Solid

- 用于输入框、按钮、设置卡片和操作确认。
- 保证文字和交互状态具有稳定对比度。
- AI 操作确认必须使用 Solid，而不是易受背景干扰的 Glass。

### 8.5 Overlay

- 用于菜单、Tooltip、下拉框、Popover 和 Modal。
- 边界清晰，10–14px 圆角，轻至中等阴影。
- 与触发控件保持空间关联。

### 8.6 Z 层级

```text
Z0 Foundation
Z1 Document surface
Z2 Docked glass panel
Z3 Floating toolbar
Z4 Popover / Menu
Z5 Modal dialog
Z6 Critical confirmation
```

## 9. 阴影

阴影由自定义绘制或缓存资源实现，不为每个控件创建昂贵的图形特效。

| Token | 偏移 | 模糊 | 透明度 | 用途 |
|---|---|---|---|---|
| `shadow.xs` | 0, 1 | 4 | 6% | 小卡片 |
| `shadow.sm` | 0, 2 | 10 | 9% | PDF 页面、下拉菜单 |
| `shadow.md` | 0, 6 | 18 | 12% | 浮动工具栏 |
| `shadow.lg` | 0, 12 | 32 | 16% | Modal |

## 10. 图标系统

- 风格：Rounded Linear。
- 基础画布：20×20，兼容 16 和 24。
- 线宽：1.75–2px。
- 图标颜色来自语义 Token，不在 SVG 中硬编码产品颜色。
- 同一工具组不得混用填充、锐角和圆角风格。
- 复杂或低频功能使用图标加文字。
- 纯图标按钮必须有 Tooltip、可访问名称和快捷键说明。
- 图标资源必须记录来源和许可证。

## 11. 工作空间模型

### 11.1 四层结构

1. Global Command：菜单、全局搜索、账户和窗口级命令。
2. Document Command：标签、阅读、编辑、批注和页面命令。
3. Document Workspace：导航、PDF 画布和文档属性。
4. Contextual Intelligence：AI 对话、引用、建议和操作预览。

### 11.2 基准布局

```text
┌──────────────────────────────────────────────────────────────┐
│ Title / Menu / Global Search                      Window Cmd │
├──────────────────────────────────────────────────────────────┤
│ Document Tabs                                                │
├──────────────────────────────────────────────────────────────┤
│ Open Save │ Select Hand │ Zoom Page │ Annotate │ AI Actions │
├───────────────┬───────────────────────────────┬──────────────┤
│ Navigation    │                               │ AI Assistant │
│ Pages         │          PDF Canvas           │ Context      │
│ Bookmarks     │                               │ Conversation │
│ Search        │                               │ Citations    │
├───────────────┴───────────────────────────────┴──────────────┤
│ Page 1/20 │ 100% │ Page Size │ Index │ Privacy │ AI Status │
└──────────────────────────────────────────────────────────────┘
```

### 11.3 尺寸

| 项目 | 默认 | 最小 | 最大 |
|---|---:|---:|---:|
| 窗口 | 1440×900 | 1024×680 | 不限制 |
| 左侧导航 | 240 | 180 | 360 |
| AI 面板 | 340 | 280 | 480 |
| 属性面板 | 300 | 260 | 440 |

当窗口宽度不足时，按以下顺序降级：隐藏属性面板、收起导航文字、关闭 AI 面板、进入 Focus Mode。不得压缩 PDF 区域到不可用宽度。

### 11.4 工作模式

#### Focus Mode

- 隐藏导航和 AI 面板。
- 保留必要页面控制。
- 适合阅读、演示和小窗口。

#### Document Mode

- 左侧导航 + 中央 PDF。
- 右侧按需显示属性面板。
- 默认模式，适合阅读、搜索和批注。

#### Intelligence Mode

- 中央 PDF + 右侧 AI。
- 点击引用定位并同步高亮。
- 选区变化更新 AI 上下文提示。

工作模式是同一 Workspace 的布局状态，不是独立页面。切换不能丢失缩放、页码、选区或对话。

## 12. 通用组件状态

所有交互组件至少支持：

```text
Default → Hover → Pressed
Default → Focused
Default → Selected
Default → Disabled
Default → Loading
Default → Error
```

- Hover 不替代 Focus。
- Focus 必须对键盘用户可见。
- Disabled 状态不响应输入，并提供原因 Tooltip（适用时）。
- Loading 不允许重复提交。
- Error 包含文字说明和恢复动作。

## 13. Button

| 变体 | 用途 | 基础视觉 |
|---|---|---|
| Primary | 当前视图唯一主操作 | Prism Blue 实体 |
| Secondary | 次要动作 | Solid 中性表面 |
| Ghost | 工具栏和低强调操作 | 透明/轻 Hover |
| AI | 明确的智能动作 | Violet 轻填充或实体 |
| Danger | 删除和不可逆风险 | Error 色 |

规格：

- 高度：30/36/40。
- 水平 Padding：12–16。
- 图标：16 或 20。
- 圆角：8–10。
- 默认正文：13–14px Medium。
- 同一按钮文字保持稳定，处理中使用 Spinner 加原动作描述。

## 14. Input

包含 TextField、SearchField、ComboBox、NumberInput、ZoomInput 和 ChatInput。

- 默认高度 36，聊天输入最小 40，可自动扩展到 5 行。
- 1px 默认边框，Focus 使用 2px Prism Blue 视觉环。
- 错误状态同时显示边框和错误文字。
- SearchField 包含图标、清空、结果计数和快捷键。
- ZoomInput 接受百分比、适合页面和适合宽度，不接受无效范围。
- Placeholder 不能替代 Label。

## 15. Navigation

左侧使用 Navigation Rail + 内容面板：

```text
┌────┬────────────────────┐
│ 页 │ Pages              │
│ 书 │                    │
│ 搜 │ Page 01            │
│ 注 │ Page 02            │
│ 附 │ Page 03            │
└────┴────────────────────┘
```

页面缩略图状态：

- Default：Paper + 页码。
- Hover：`prism.50` 背景。
- Selected：2px Prism Blue 边框。
- Multi-selected：选中标记 + 边框。
- Dragging：70% 不透明度。
- Drop Target：2px 蓝色插入线，不只使用虚线框。
- Loading：页面比例一致的骨架。
- Error：保留页码并显示重试。

## 16. Document Workspace

### 16.1 画布

- Foundation 使用中性背景。
- 页面在可用宽度内居中。
- 页面间距使用 16 或 24。
- 快速缩放先复用已有位图，稳定后补高质量瓦片。
- 加载和错误状态局部显示，不遮挡整本文件。

### 16.2 页面图层

从下到上：

```text
Rendered Page
Search Highlight
AI Citation Highlight
Text Selection
Annotations
Interaction Handles
Transient Feedback
```

图层必须由独立模型驱动，不能把高亮直接烘焙到页面位图。

### 16.3 工具栏

- 阅读工具与编辑工具分组。
- 高频命令可见，低频命令进入 More。
- 选中批注时切换为上下文工具栏。
- 页面缩放和页码保留文本输入能力。
- 窄窗口按优先级折叠，而不是横向滚动工具栏。

## 17. 欢迎页

欢迎页包含：

- Folixa 标识和一句简短价值说明。
- 主操作“打开 PDF”。
- 拖放区域。
- 最近文档列表，展示文件名、时间、页数和缩略图（可用时）。
- 入口：“恢复未保存工作”（存在恢复项时）。

不包含营销轮播、强制登录和与当前任务无关的内容。最近文档缺失时显示清晰空状态，而不是空白容器。

## 18. AI Assistant

### 18.1 面板结构

```text
AI Assistant                          [···]
Current document · Page 8/24 · Cloud
──────────────────────────────────────────
[Summarize] [Translate] [Explain] [Extract]

Conversation
  User message
  AI message
  Citation cards

──────────────────────────────────────────
Context: Entire document        Privacy: Cloud
Ask about this document…                 [Send]
```

### 18.2 状态

| 状态 | 呈现 |
|---|---|
| Empty | 推荐动作和上下文说明 |
| Indexing | 进度、当前页数、取消 |
| Thinking | 文字状态 + 轻量动画 |
| Streaming | 增量正文 + Stop |
| Completed | 引用、复制、反馈和后续动作 |
| Failed | 错误原因、重试和设置入口 |
| Cancelled | 明确取消状态，可重新发送 |

### 18.3 AI Message

AI 消息由 Header、Body、CitationList、ActionList 和 Feedback 组成。正文支持安全的 Markdown 子集，但禁止任意 HTML、脚本和自动外链访问。

### 18.4 Citation Card

必须包含：

- 文档名称。
- 页码和位置描述。
- 原文摘录。
- Verified、Unverified、Missing 或 Outdated 状态。
- “定位原文”按钮。

模型生成的页码默认不是 Verified。只有 CitationResolver 在当前文档 revision 中找到对应 `chunk_id` 和坐标后才能显示 Verified。

### 18.5 AI Action Bar

动作随上下文变化：

| 上下文 | 建议动作 |
|---|---|
| 文本选区 | Explain、Translate、Summarize Selection |
| 当前页 | Summarize Page、Extract Data |
| 整份文档 | Summarize Document、Ask Questions |
| 多页选中 | Compare、Export、Propose Delete |

快捷操作只能填充或发起明确任务，不能绕过隐私或变更确认。

## 19. AI 操作预览

AI 不直接修改 PDF。操作预览使用 Solid Modal 或右侧 Inspector，包含：

- 操作名称和状态 `Awaiting approval`。
- AI 判断依据。
- 受影响页或对象列表。
- 缩略图或差异预览。
- 风险与可撤销说明。
- Approve 和 Cancel。

流程：

```text
Natural-language request
  → Structured draft
  → Permission and schema validation
  → Document revision validation
  → Visual preview
  → User approval
  → Document Command
  → Undo Stack and audit record
```

危险操作的确认按钮使用 Danger，而不是 AI Violet。

## 20. Citation Focus 动效

这是 Folixa 的标志性交互：

1. Locate：页面滚动到目标区域。
2. Focus：目标区域出现紫色填充和清晰边界。
3. Connect：对应引用卡片进入 Selected。
4. Settle：180–450ms 后填充减弱，保留轻边界数秒。

用户启用减少动画时，直接定位并显示静态边界，不进行缓动和闪烁。

## 21. Motion

| Token | 时长 | 用途 |
|---|---:|---|
| `instant` | 80ms | 按下反馈 |
| `fast` | 120ms | Hover |
| `normal` | 180ms | 状态切换 |
| `moderate` | 240ms | 面板展开 |
| `slow` | 320ms | 大型浮层 |
| `emphasis` | 450ms | Citation Focus |

- 默认使用 ease-out 进入、ease-in 退出。
- 动效不能阻止点击或滚动。
- PDF 缩放、手写和拖拽优先响应速度。
- 所有非必要动画必须可关闭。
- 不使用循环装饰动画。

## 22. Settings

分类：

| 分类 | 内容 |
|---|---|
| Appearance | Light/Dark/System、密度、强调色、减少动画 |
| Workspace | 面板、标签、布局恢复 |
| Reading | 页面模式、缩放、滚动、纸张显示模式 |
| AI | Provider、模型、上下文和费用提示 |
| Privacy | 上传策略、历史、索引和清除数据 |
| Shortcuts | 查看、搜索和修改快捷键 |
| Advanced | 缓存、日志、渲染和调试 |

设置保存即生效；危险设置需要说明影响并提供恢复默认值。

## 23. 反馈组件

### Toast

- 用于可恢复、无需打断的事件。
- 同时最多展示 3 条。
- 默认 4–6 秒，错误可保持到用户关闭。
- 提供动作时只保留一个主要动作。

### Progress

- 小于 400ms 的任务不显示进度，避免闪烁。
- 可计算任务显示百分比和取消。
- 不可计算任务显示明确阶段文字。
- 索引、OCR和 AI 请求分别显示，不合并成模糊的“处理中”。

### Dialog

- 普通确认使用 Solid Modal。
- 文案描述结果，不使用含糊的“确定吗”。
- 默认焦点不得落在危险操作上。

## 24. 空状态与错误状态

每个面板都必须设计：

- 无数据。
- 加载中。
- 权限不足。
- 数据损坏。
- 功能不可用。
- 网络失败。
- 用户取消。

错误格式：

```text
无法建立文档索引
PDF 文本提取在第 18 页失败，其他页面仍可使用。
[重试失败页] [查看详情]
```

不要只显示错误码；详细技术信息放在可展开区域和日志中。

## 25. 无障碍

- 文字与背景对比满足 WCAG AA；正文正常文字至少 4.5:1。
- 键盘可访问所有命令、面板和对话框。
- 焦点顺序与视觉顺序一致。
- 焦点环在浅色、深色和高对比度主题都可见。
- 纯图标按钮设置 AccessibleName 和 Tooltip。
- 状态不只通过颜色传达。
- 支持系统字体缩放、高对比度和减少动画。
- AI 流式输出不能持续抢夺屏幕阅读器焦点。
- 页面画布提供当前页、总页数和选区的可访问描述。

## 26. 国际化

- 首版 UI 结构同时适配中文和英文。
- 控件不能依赖固定文本宽度。
- 文案预留 30%–50% 扩展空间。
- 支持 RTL 镜像的组件不能硬编码左右语义。
- 日期、数字、文件大小和快捷键使用本地化格式。
- 截断文字提供 Tooltip；重要操作不得只显示省略文本。

## 27. Qt 6 Widgets 实现

### 27.1 模块结构

```text
src/ui/design/
├─ tokens/
│  ├─ ColorTokens.h
│  ├─ TypographyTokens.h
│  ├─ SpacingTokens.h
│  ├─ RadiusTokens.h
│  ├─ ShadowTokens.h
│  └─ MotionTokens.h
├─ theme/
│  ├─ ThemeManager.h/.cpp
│  ├─ ThemeDefinition.h
│  ├─ LightTheme.cpp
│  └─ DarkTheme.cpp
├─ style/
│  ├─ PrismStyle.h/.cpp
│  └─ StyleMetrics.h
├─ materials/
│  ├─ GlassPanel.h/.cpp
│  ├─ PaperSurface.h/.cpp
│  └─ OverlaySurface.h/.cpp
├─ components/
│  ├─ buttons/
│  ├─ inputs/
│  ├─ navigation/
│  ├─ document/
│  ├─ intelligence/
│  └─ feedback/
├─ motion/
│  └─ MotionManager.h/.cpp
└─ gallery/
   └─ PrismStudioWindow.h/.cpp
```

### 27.2 技术映射

| 设计对象 | Qt 实现 |
|---|---|
| 主窗口 | `QMainWindow` |
| Dock 面板 | `QDockWidget` + 自定义标题栏 |
| 文档标签 | `QTabBar`/`QTabWidget` 子类 |
| 主题 | `QPalette` + Token + `QProxyStyle` |
| 局部样式 | 受控 QSS |
| 自绘组件 | `QWidget` + `QPainter` |
| 动画 | `QPropertyAnimation` |
| Popup | `Qt::Popup` 自定义窗口 |
| 页面画布 | 自定义 `QWidget`，必要时评估 `QGraphicsView` |

### 27.3 Token API

业务组件读取语义 Token，不读取十六进制颜色：

```cpp
enum class ColorRole {
    Foundation,
    SurfacePrimary,
    BorderDefault,
    TextPrimary,
    Accent,
    Intelligence,
    Error
};

QColor color(ColorRole role) const;
int spacing(SpacingRole role) const;
int radius(RadiusRole role) const;
int duration(MotionRole role) const;
```

禁止：

- 业务组件硬编码颜色和圆角。
- 每个页面维护独立完整 QSS。
- 使用控件类型选择器意外影响全局第三方控件。
- 主题切换时只更新背景。

### 27.4 ThemeManager

`ThemeManager` 负责：

- Light、Dark、System。
- 系统主题变化监听。
- 主题 Token 查询。
- 发布 `themeChanged`。
- 统一 Palette 和 PrismStyle 刷新。
- 保存用户选择。

ThemeManager 不负责业务状态和 PDF 阅读模式。

### 27.5 PrismStyle

使用 `QProxyStyle` 集中定义：

- 控件高度和边距。
- Focus、Hover、Pressed、Disabled 指标。
- 菜单、Tab、Splitter 和滚动条绘制。
- 高 DPI 图标尺寸。
- 系统无障碍兼容。

QSS 仅用于简单局部表面，复杂状态通过自绘和 Style 实现。

## 28. Liquid Glass 性能边界

- PDF 页面绝不使用实时背景模糊。
- 默认 Glass 可以只用半透明填充、边框和阴影模拟。
- 不为列表中的每个 Item 创建 `QGraphicsDropShadowEffect`。
- 阴影和圆角背景优先缓存。
- 同时存在的透明叠加层数量应可测量。
- UI 动画和 PDF 渲染使用独立任务路径。
- 低性能或远程桌面环境自动降级透明和动画。
- 主题切换不得清空 PDF 渲染缓存。

建议预算：

| 指标 | 目标 |
|---|---:|
| 常规交互帧时间 | ≤16.7ms |
| 面板动画 P95 | ≤20ms/帧 |
| 主题切换 UI 停顿 | ≤100ms |
| 普通窗口闲置 CPU | 接近 0% |
| 透明材质额外显存/内存 | 可测且有上限 |

## 29. Prism Studio

建立独立设计系统展示程序 `Folixa Prism Studio`，用于在不依赖 PDFium 和真实 AI 的情况下验证组件。

页面：

- Foundations：颜色、字体、尺寸、圆角和阴影。
- Buttons：变体和全部状态。
- Inputs：输入、搜索、缩放和聊天输入。
- Navigation：Rail、Tab、缩略图和书签。
- Materials：Foundation、Glass、Paper、Solid、Overlay。
- Document：页面、选区、搜索和引用高亮。
- Intelligence：消息、引用卡片和操作预览。
- Feedback：Toast、Dialog、Progress 和错误。
- Motion：动画和减少动画模式。
- Themes：Light、Dark 和高对比度预览。

Prism Studio 支持：

- 切换主题、DPI 和控件状态。
- 调整窗口宽度验证响应式行为。
- 开关动画和透明效果。
- 显示 Token 名称和值。
- 生成视觉回归截图。

它是研发工具，不随正式安装包默认发布。

## 30. 视觉测试

### 30.1 截图基线

至少覆盖：

- 欢迎页。
- Document Mode。
- Focus Mode。
- Intelligence Mode。
- 搜索结果与匹配高亮。
- 文本选择、批注和 AI 引用高亮。
- AI Empty、Streaming、Failed 和 Completed。
- AI 操作预览。
- 设置页。
- Light、Dark、高对比度。
- 100%、150%、200%、300% DPI。
- 1024×680 和 1440×900。

### 30.2 人工评审

- 主操作是否能在 3 秒内被发现。
- 常用阅读操作是否在两次交互内完成。
- PDF 是否始终保持主要视觉面积。
- 面板最小尺寸是否仍然可用。
- 中文和英文是否无截断、重叠和跳动。
- 键盘焦点是否连续、清晰。
- 深色主题是否避免低对比度和过亮边界。

### 30.3 自动检查

- Token 禁止硬编码扫描。
- 截图像素差异阈值。
- AccessibleName 缺失检查。
- 键盘 Tab 顺序测试。
- 最小窗口布局测试。
- 动画关闭模式测试。
- 各主题文字对比度检查。

## 31. 交付阶段

### D1 — Foundation

- Token、Light/Dark、字体、图标接口和 ThemeManager。
- PrismStyle 基础指标。
- Prism Studio Foundations 页面。

### D2 — Component System

- Button、Input、Navigation、Feedback 和全部状态。
- Tooltip、键盘焦点和无障碍基础。
- 视觉回归框架。

### D3 — Document Workspace

- 欢迎页、主窗口、标签、导航和画布材质。
- Focus/Document/Intelligence 模式。
- 页面、选区、搜索、批注和引用视觉层。

### D4 — Intelligence Layer

- AI Panel、Message、Citation Card 和 Action Bar。
- AI 操作预览。
- Citation Focus 动效和减少动画替代。

## 32. Prism Document V1.0 验收

- [ ] Light、Dark 和 System 主题完整。
- [ ] 所有基础颜色由 Token 管理。
- [ ] 字体、间距、圆角、边框、阴影和动效统一。
- [ ] Button 与 Input 具备完整状态。
- [ ] 导航支持折叠、选中、拖拽和错误状态。
- [ ] PDF Paper 与应用材质明确分离。
- [ ] 页面图层具有稳定视觉语义。
- [ ] AI 消息和引用卡片支持全部状态。
- [ ] AI 操作具备预览、确认和取消。
- [ ] Citation Focus 完成并支持减少动画。
- [ ] 所有非必要动画可关闭。
- [ ] 100%–300% DPI 测试通过。
- [ ] 1024×680 最小窗口测试通过。
- [ ] 关键界面键盘和屏幕阅读器测试通过。
- [ ] Prism Studio 可以独立构建运行。
- [ ] 视觉回归测试纳入 CI。

## 33. 版本规划建议

根据项目“一项主要任务对应一个软件版本”的规则，设计系统应成为独立任务版本，并位于可复现构建环境之后、PDFium 接入之前。推荐调整为：

```text
v0.1.0  Qt + AI Worker 工程骨架
v0.2.0  可复现 Windows 构建环境
v0.3.0  Prism Document 设计系统与 Prism Studio
v0.4.0  PDFium 最小阅读闭环
后续现有功能版本依次顺延
```

是否调整现有版本文档需要单独执行版本规划变更；本文不直接修改已建立的版本编号。

## 34. 最终设计决策

| 维度 | 决策 |
|---|---|
| 设计语言 | Prism Document |
| 品牌风格 | Minimal + Controlled Liquid Glass |
| 主色 | Prism Blue |
| AI 色 | Intelligence Violet |
| 默认主题 | Light |
| 材质 | Foundation / Glass / Paper / Solid / Overlay |
| 主布局 | Navigation / Document Workspace / Intelligence |
| UI 技术 | Qt 6 Widgets |
| 绘制 | QPainter + QProxyStyle + 受控 QSS |
| 动效 | QPropertyAnimation，可关闭 |
| 设计验证 | Folixa Prism Studio + 视觉回归 |

最终约束：PDF 文档永远是视觉中心；AI 是文档能力而不是聊天插件；设计系统独立演进；任何视觉效果都不能牺牲准确性、性能、可访问性和用户控制。

