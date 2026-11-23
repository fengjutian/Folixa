# Folixa Fluent 2 对齐与迁移规范

**文档版本：** 1.0

**适用产品：** Folixa AI PDF Editor

**技术基线：** C++20、Qt 6 Widgets、QPalette、受控 QSS

**上位规范：** [Folixa Prism Document UI 设计规范](UI_DESIGN_SYSTEM.md)

**状态：** 开发前基线

## 1. 目的

本文把 Fluent 2 的设计原则转换为 Folixa 可以实施、测试和逐步迁移的 Qt 规范。后续组件改造必须先在本文中明确 Token、状态、交互和验收标准，再进入代码实现。

本次对齐不是将网页组件源码移植到 Qt，也不是把界面简单改成微软蓝色。目标是建立一套稳定的基础组件语言：

- 使用语义 Token，避免业务组件散落色值和尺寸；
- 为 Rest、Hover、Pressed、Focus、Selected 和 Disabled 提供一致状态；
- 保持原生桌面应用的信息密度、键盘操作和高 DPI 表现；
- 同时支持浅色、深色和系统主题；
- 保留 Folixa 的文档优先布局、品牌识别和 AI 语义。

## 2. 规范关系与边界

### 2.1 Fluent 2 提供什么

- 中性色表面和内容层级；
- 控件尺寸、间距、圆角和描边节奏；
- 标准交互状态与键盘焦点；
- Button、Input、Dropdown、Menu、Dialog、Navigation 等基础组件行为；
- 可访问性、深色模式和高对比度设计方法。

### 2.2 Prism Document 保留什么

- Folixa 名称、品牌标记和产品文案；
- PDF 页面始终是视觉中心的 Reading First 原则；
- Prism Blue 品牌色，不强制替换成微软品牌蓝；
- Intelligence Violet 只用于 AI 能力；
- PDF 页面、用户批注、搜索结果和 AI 引用各自独立的语义色；
- Ribbon、文档标签、导航轨和阅读画布的产品结构。

### 2.3 明确不做

- 不引入 React、Fluent UI Web 或 WebView 作为桌面控件层；
- 不逐像素复制微软产品；
- 不为了视觉相似牺牲现有功能、快捷键或信息密度；
- 不把应用主题用于反色 PDF 页面；
- 不一次性重写全部界面；
- 不在业务页面维护独立的完整 QSS。

## 3. 设计 Token

代码和样式表应优先使用语义角色。原始色值只允许出现在主题定义和暗色映射中。

### 3.1 颜色角色

| Folixa Token | Light | 语义 |
|---|---|---|
| `colorNeutralBackground1` | `#FFFFFF` | 菜单、弹层、主卡片 |
| `colorNeutralBackground2` | `#FAFAFA` | 应用与面板次级背景 |
| `colorNeutralBackground3` | `#F5F5F5` | 输入背景、弱分组 |
| `colorNeutralBackgroundHover` | `#F0F0F0` | 中性 Hover |
| `colorNeutralBackgroundPressed` | `#E0E0E0` | 中性 Pressed |
| `colorNeutralForeground1` | `#242424` | 主要文字与图标 |
| `colorNeutralForeground2` | `#424242` | 次要文字与图标 |
| `colorNeutralForeground3` | `#616161` | 辅助文字 |
| `colorNeutralForegroundDisabled` | `#BDBDBD` | 禁用内容 |
| `colorNeutralStroke1` | `#D1D1D1` | 控件默认边框 |
| `colorNeutralStroke2` | `#E0E0E0` | 分隔线与弱边框 |
| `colorBrandBackground` | `#4169C1` | Folixa 主操作 |
| `colorBrandBackgroundHover` | `#365AA8` | 主操作 Hover |
| `colorBrandBackgroundPressed` | `#24499D` | 主操作 Pressed |
| `colorBrandBackgroundSelected` | `#E9F0FB` | 轻量选中背景 |
| `colorBrandForeground` | `#3159B8` | 品牌文字与图标 |
| `colorBrandStroke` | `#5277E8` | Focus 与品牌描边 |
| `colorDangerForeground` | `#C50F1F` | 危险操作 |
| `colorDangerBackground` | `#FDE7E9` | 危险操作弱背景 |

深色值继续由 `Theme.cpp` 的文字色与表面色映射生成。新增浅色色值时，必须在同一次变更中补全暗色映射并通过 `ThemeTests`。

颜色不是状态的唯一载体。选中、错误和焦点还必须有描边、字重、图标或文字提示。

### 3.2 间距

| Token | 值 |
|---|---:|
| `spacingNone` | 0 |
| `spacingXXS` | 2 |
| `spacingXS` | 4 |
| `spacingSNudge` | 6 |
| `spacingS` | 8 |
| `spacingMNudge` | 10 |
| `spacingM` | 12 |
| `spacingL` | 16 |
| `spacingXL` | 20 |
| `spacingXXL` | 24 |
| `spacingXXXL` | 32 |

布局优先使用 4px 网格；2、6、10px 仅用于图标和文字的视觉校准。

### 3.3 圆角和描边

| Token | 值 | 用途 |
|---|---:|---|
| `radiusNone` | 0 | PDF 页面、连续表格内部 |
| `radiusSmall` | 2 | 小型状态块 |
| `radiusMedium` | 4 | Button、Input、Dropdown |
| `radiusLarge` | 6 | Menu Item、列表项 |
| `radiusXLarge` | 8 | Menu、Popover、Card |
| `strokeThin` | 1 | 默认边框 |
| `strokeThick` | 2 | 键盘焦点、错误强调 |

旧规范中的 10–22px 大圆角不再作为基础控件默认值；大型欢迎页和特殊品牌容器可以保留，但必须有明确理由。

### 3.4 控件尺寸

| Token | 高度 | 用途 |
|---|---:|---|
| `controlSmall` | 24 | 极紧凑工具项，不承载长文字 |
| `controlMedium` | 32 | 默认 Button、Input、Dropdown |
| `controlLarge` | 40 | 强主操作和大型输入 |
| `listItemMedium` | 32 | 菜单、下拉选项 |
| `navigationItem` | 40 | 左侧导航项 |

文字控件不得用固定宽度满足单一语言。中文、英文和 125%–200% DPI 下都必须完整可用。

### 3.5 字体

Windows 字体栈：`Segoe UI Variable` → `Segoe UI` → `Microsoft YaHei UI` → 系统无衬线字体。

| Token | 字号/行高 | 字重 | 用途 |
|---|---|---:|---|
| `caption` | 12/16 | 400 | 状态、辅助信息 |
| `body` | 14/20 | 400 | 正文与标准控件 |
| `bodyStrong` | 14/20 | 600 | 选中项、重要标签 |
| `subtitle` | 20/28 | 600 | 面板或对话框标题 |
| `title` | 28/36 | 600 | 主要页面标题 |

中文界面不依赖全大写或过细字重表达层级。

### 3.6 动效

| Token | 时长 | 用途 |
|---|---:|---|
| `durationFast` | 100ms | Hover、Pressed |
| `durationNormal` | 167ms | Popup、轻量切换 |
| `durationSlow` | 250ms | 面板进入退出 |

Qt Style Sheet 不提供可靠的状态过渡。只有对理解状态变化有帮助时才使用 `QPropertyAnimation`；系统启用减少动画或远程桌面环境下允许关闭。

## 4. 统一组件状态

所有可交互组件至少定义以下状态：

| 状态 | 要求 |
|---|---|
| Rest | 中性表面与清晰边界 |
| Hover | 表面变化，不造成布局位移 |
| Pressed | 比 Hover 更强，反馈立即发生 |
| Focus | 可见的 2px 焦点指示，不只改变底色 |
| Selected/Checked | 与 Hover 可区分，保持文字对比度 |
| Disabled | 内容和边框同时降级，但仍可辨识 |

使用键盘导航时必须显示 Focus；鼠标点击不应留下干扰性的永久焦点装饰。若 QSS 无法准确区分，应通过组件子类或 `QProxyStyle` 实现。

## 5. 基础组件规范

### 5.1 Button

- 默认高度 32px，左右内边距 12px，圆角 4px；
- 一个局部区域最多一个 Primary Button；
- 三个及以上同级操作全部使用中性样式；
- 危险操作使用文字和语义色，不只使用红色图标；
- 纯图标按钮必须提供 Tooltip 和 AccessibleName；
- Toggle Button 使用 Checked 状态，不以普通按钮模拟。

### 5.2 Input

- 默认高度 32px；
- Placeholder 只能提供示例或提示，不能代替标签；
- Focus 使用品牌描边；
- 错误态同时显示错误说明；
- Clear、Search 等尾部按钮使用 16px 图标并保留足够点击区域。

### 5.3 Dropdown 与 ComboBox

- 仅选择固定短列表时使用 Dropdown；需要输入过滤时才使用 ComboBox；
- 控件默认高度 32px，弹层圆角 8px；
- 弹层宽度不小于触发器，也不能小于最长必要选项；
- 选项默认高度 32px，最多显示 8–10 项后滚动；
- 当前项使用选中背景和字重，必要时增加勾选图标；
- 支持方向键、Home、End、Enter 和 Escape；
- 弹层不得被父容器裁切或超出可用屏幕。

### 5.4 Menu 与 Popup

- 内边距 4–6px，选项圆角 4–6px；
- 分隔线只用于语义分组；
- 阴影表达浮层关系，边框保证高对比和远程桌面可见性；
- 不给列表中每个 Item 创建阴影对象；
- Popup 关闭后焦点返回触发器。

### 5.5 Card

- 卡片不是默认布局容器；可用间距和分隔线解决时不增加卡片；
- 默认圆角 8px、1px 弱边框；
- 同层卡片不重复使用强阴影；
- 面板内避免多层卡片嵌套。

### 5.6 Dialog

- 标题说明任务，不使用模糊的“提示”；
- 主操作位于稳定位置，一个对话框只有一个 Primary；
- 关闭、取消和 Escape 行为一致；
- 删除、覆盖等不可逆操作明确说明对象和影响；
- 内容过长时滚动内容区，不让按钮区滚出窗口。

## 6. Qt 实现约束

当前实现以 `src/app/ui/Theme.{h,cpp}` 为主题入口：

- `lightStyleSheet()` 是基础样式的唯一事实来源；
- `darkTextColors()` 与 `darkSurfaceColors()`生成暗色样式；
- `QPalette` 负责原生弹层、提示框和 QSS 覆盖不到的区域；
- 阅读配色与应用主题继续保持独立；
- 简单视觉状态使用受控 QSS；
- 焦点、弹层定位、复杂选中标记和动画优先使用组件子类或 `QProxyStyle`；
- 业务组件不得新增完整页面级样式表；
- 局部选择器优先使用对象名或动态属性，避免污染所有同类控件。

第一阶段不新建与当前代码平行的第二套 ThemeManager。先从现有主题实现中提取 Token API，待调用点稳定后再评估目录拆分。

建议的目标结构：

```text
src/app/ui/
├─ Theme.h/.cpp
├─ FluentTokens.h/.cpp
├─ FluentStyle.h/.cpp          # 第二阶段评估 QProxyStyle
└─ components/
   ├─ FluentButton.*
   ├─ FluentComboBox.*
   └─ FluentPopup.*
```

## 7. 迁移顺序

### Phase 0：规范与清点

- [x] 建立本文档；
- [ ] 统计 `Theme.cpp` 中重复和近似色值；
- [ ] 统计业务代码内的 `setStyleSheet()`；
- [ ] 建立迁移前截图基线。

### Phase 1：Token 基础设施

- [x] 新增语义颜色、间距、圆角和尺寸 API；
- [ ] 保持现有 Light/Dark/System 设置兼容；
- [x] 增加 FluentComponentsTests，验证 Token、组件属性、尺寸和弹层配置；
- [x] 不改变业务功能和数据流。

### Phase 2：基础输入组件

- [x] Button（Secondary / Primary / Subtle / Danger）；
- [x] LineEdit；
- [x] ComboBox / Dropdown；
- [x] ComboBox Popup；
- [x] ToolButton / SearchBox；
- [x] Card；
- [ ] 独立 Menu / Popup；
- [ ] CheckBox / RadioButton / Switch；
- [ ] Slider / SpinBox。

### Phase 3：批注面板样板

- [ ] 将批注筛选、列表、批量操作迁移到统一 Token；
- [ ] 移除该面板的临时硬编码状态；
- [ ] 完成键盘、DPI、深色和中英文验收；
- [ ] 以此作为其他业务面板的参考实现。

### Phase 4：应用骨架

- [ ] Command Bar 与 Ribbon；
- [ ] 左侧导航轨与面板标题；
- [ ] 文档标签栏；
- [ ] 状态栏和页面控制；
- [ ] Dialog 与通知。

### Phase 5：业务面板和收口

- [ ] 搜索、附件、表单、AI、打印和设置；
- [ ] 清除废弃色值和重复 QSS；
- [ ] 更新截图基线与人工验收表；
- [ ] 检查许可证和第三方图标归属。

每个 Phase 应单独可构建、可回退，不能以“全部改完才一致”为前提。

## 8. 验收标准

### 8.1 自动化

- `ThemeTests`：浅色值都有暗色对应，文字对比符合要求；
- 组件测试：Rest、Focus、Selected、Disabled 属性和对象结构正确；
- 布局测试：控件不因语言切换、字体放大或 DPI 改变被裁切；
- 构建测试：Windows Debug 与 Release 均通过。

### 8.2 人工视觉验收

至少验证：

- Light、Dark、System 三种主题；
- 100%、125%、150%、200% DPI；
- 中文和英文；
- 键盘 Tab/Shift+Tab、方向键、Enter、Escape；
- 窄窗口、常规窗口和最大化窗口；
- 鼠标 Hover/Pressed 与键盘 Focus 的区别；
- Popup 在屏幕边缘和多显示器环境中的位置；
- 远程桌面或关闭透明效果时仍能分辨层级。

### 8.3 完成定义

一个组件只有同时满足以下条件才算完成：

1. 使用本文定义的语义 Token；
2. 六类交互状态齐全；
3. 浅色和深色均通过；
4. 键盘和读屏名称可用；
5. 高 DPI 与中英文不裁切；
6. 有自动化验证或明确记录的人工验收步骤；
7. 未引入新的页面级硬编码样式。

## 9. 首个开发切片

文档批准后，首个切片限定为 Phase 1：

1. 增加 `FluentTokens` 语义 API；
2. 将批注类型 Dropdown 作为第一个调用方，但不扩大到整个面板；
3. 为 Token 和 Dropdown 状态补测试；
4. 编译主程序并运行 ThemeTests；
5. 提交改动清单和迁移前后截图，再决定是否进入 Phase 2。

此切片不修改 PDF 引擎、文档模型、批注数据或保存逻辑。

### 9.1 首批实现记录（2026-11-23）

- 新增 `src/app/ui/FluentTokens.{h,cpp}`，提供语义颜色与尺寸查询；
- 新增 `src/app/ui/components/FluentControls.{h,cpp}`；
- 首批组件为 `FluentButton`、`FluentLineEdit`、`FluentComboBox`；
- 第二批组件为 `FluentToolButton`、`FluentSearchBox`、`FluentCard`；
- `FluentComboBox` 统一管理列表弹层、平滑滚动、可见项上限与最小宽度；
- 批注筛选区成为第一个真实调用方；
- 搜索面板、索引面板、批注操作卡片和状态栏缩放按钮已替换为 Fluent 组件；
- `FluentComponentsTests` 覆盖 Token、外观属性、三档高度和弹层配置；
- 未修改批注筛选逻辑、信号连接、数据模型或 PDF 保存路径。

## 10. 参考依据

- Fluent 2 Design Tokens：<https://fluent2.microsoft.design/design-tokens>
- Fluent 2 Color：<https://fluent2.microsoft.design/color/>
- Fluent 2 Typography：<https://fluent2.microsoft.design/typography>
- Fluent 2 Layout：<https://fluent2.microsoft.design/layout>
- Fluent 2 Combobox：<https://fluent2.microsoft.design/components/web/react/core/combobox/usage>
- Fluent 2 Button：<https://fluent2.microsoft.design/components/web/react/core/button/usage>

这些资料用于提取跨平台设计原则；最终 Qt 行为以本文和本项目测试为准。
