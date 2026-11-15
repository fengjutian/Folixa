# 福昕功能截图档案

**截图来源**：本机安装的 Foxit PDF Editor 2026（Continuous，`2026.1.2.36547`）。
只用它做功能对标，不把它的二进制或资源用作产品依赖；截图不随安装包分发。


| 标签页 | 文件 | 尺寸 | SHA-256 |
|---|---|---:|---|
| 文件 | `01-file-tab.png` | 1920×1010 | `500104f6116a91bd77155d3039f0d88fd7bfcd75e385711151ec34e56e1fc7cb` |
| 主页 | `02-home.png` | 1920×1020 | `29a6b75208fc3dee48efa1360734b337b6907ccc1e021b65278e2147e47bbda7` |
| 转换 | `03-convert.png` | 1920×1015 | `365d966a698a09468bd4cf9fbe3252f1e771a33a309a6a2560cab6636a393389` |
| 编辑 | `04-edit.png` | 1920×1016 | `27b5aa9f108ad712d7707b3120faf6e34cf95397622d163d6a518556a6966838` |
| 页面管理 | `05-page-management.png` | 1920×195 | `40075c9c43a5254ec09b7e2ef65ae7d0271d2f8cc7333585baf4e155691211fa` |
| 注释 | `06-comment.png` | 1920×195 | `5b4fb536d414e83ce1bfc10bbebc6018a0c0b1ab3e88434550877d5750465a20` |
| 视图 | `07-view.png` | 1920×195 | `28b31ca45771b2b5a8cf9a50a554764fcbd8e76ebe74512779e93390b2fcbf18` |
| 表单 | `08-form.png` | 1920×195 | `fd3eb8ef65cab6d5b487a50fb1d266f2fc1a26868f989bdcd6a5fb3b16dc9a87` |
| 保护 | `09-protect.png` | 1920×195 | `3ebf246e505abb8f957341f58818ce8805f8febdfc968be3d9a51281f26bc5ca` |
| 电子签章 | `10-sign.png` | 1920×195 | `129f1a60296a2f27a193873c945dffc668afe471d45118337f2bff090c1d867d` |
| 共享 | `11-share.png` | 1920×195 | `5655d0646b365ba442a09c7b3066decf2e1dcb1849c581dd6e5b812e9079aad0` |
| 辅助工具 | `12-accessibility.png` | 1920×195 | `e04a6a0d4a4841e2e08e8778587ccce999a33b5ee06ce026f617984a1271d546` |
| 论文工具 | `13-thesis.png` | 1920×195 | `7cadcb60de17201a377d6db2f07b53a595cde661fcd06ad094730742586aa7f6` |
| 文档管理 | `14-doc-management.png` | 1920×195 | `ad386e856eb8aaa507d5cac15e47a65fe33702ea3dd3f17f54adfecdf5a7e371` |
| 录制 | `15-record.png` | 1920×195 | `661e494a7cbc075df3703c4f5582aaf29318ba986f496e5d3e8984e0adc45349` |
| 帮助 | `16-help.png` | 1920×195 | `1ca90e921048c4e50c4491655a3a423f74a239a968d15cb4e52cb9ed182267b0` |
| AI助手 | `17-ai-assistant.png` | 1920×195 | `ff6e0baac94d4260de5eae36411a0a94a76a7b103946fa2ad7872130372421c6` |

## 拍法

1. 打开福昕，切到目标标签页，收起与功能无关的侧栏。
2. 用 `Win+Shift+S` 只截 Ribbon 本身（高 195px 左右），不要截整屏。
3. 按标签页顺序命名（`01-file-tab` … `17-ai-assistant`）。
4. 改完截图重新跑 `python scripts/build-foxit-matrix.py`，哈希会一起更新。

## 约束

- 截图只用于功能对标，不随安装包发布，也不进 `dist/`。
- 不要把福昕的图标、皮肤或文案抄进产品资源；命令名只作为对标术语出现在文档里。
