# qpdf 引入决定

**状态：** **已采纳，且为必需依赖**（`AIPDF_WITH_QPDF=OFF` 在 CMake 配置阶段直接失败）  
**决策：** 2026-11-19，按 [依赖与许可证政策](DEPENDENCY_POLICY.md) 登记为生产依赖  
**决策人：** 项目所有者

## 1. 为什么现在提

v1.3.0 先落地了一个刻意做窄的增量写入器（`src/pdf/IncrementalWriter.cpp`）。它够用，但边界是硬的：

| 现在能处理 | 现在处理不了 |
|---|---|
| 经典 xref 表 + 直接对象 | 交叉引用流（PDF 1.5+ 常见） |
| PDFium 刚写出来的字节 | 对象流（`/ObjStm`） |
| 页面内联注释、独立注释对象 | 加密文件、增量链里的自由对象 |

后续版本没有一项能绕开这些：1.5 表单扁平化要删对象、1.6 加密与遮盖要重写内容流、
1.7 签名要在已有增量链上追加、1.11 内容编辑要操作任意真实文件。**继续自研等于把 qpdf 重写一遍**，
而且是拿用户文件当测试集。

## 2. 候选对比

| 方案 | 能力 | 许可证 | 体积/构建 | 结论 |
|---|---|---|---|---|
| **qpdf 11.x** | 对象级读写、增量更新、对象流、加密、页面操作 | Apache-2.0 | 源码构建，`qpdf.dll` 约 2–4 MB | **建议** |
| PDFHummus | 擅长从零生成，改写现有文档会重建结构 | Apache-2.0 | 中等 | 会丢原始对象、破坏已有签名 |
| 商业 SDK（Foxit/Adobe/Solid） | 最全 | 商业授权 | 大 | 违反"不将第三方商业应用二进制作为产品构建依赖" |
| 自研完整对象层 | 可控 | — | 极大 | 边界情况无穷，没有语料与时间 |

## 3. 影响面

| 面 | 变化 | 工作量 |
|---|---|---|
| 依赖声明 | `vcpkg.json` 增加 `qpdf`，固定 `builtin-baseline` | 小 |
| 构建 | CMake 直接 `find_package(qpdf CONFIG QUIET)` + 找不到即 `FATAL_ERROR`；`AIPDF_WITH_QPDF=OFF` 也直接失败，不再保留手写回落路径 | 小 |
| 打包 | 随包分发 `qpdf.dll`；许可证清单增加 Apache-2.0 与 qpdf 的 NOTICE | 小 |
| SBOM | 新增条目（版本、来源、SHA-256） | 小 |
| CI | `windows-2022` 需要 vcpkg 缓存（首次构建约 10–20 分钟） | 中 |
| 体积 | 实测 `qpdf30.dll` 9.2 MB，安装包约 +9–10 MB（当前 20 MB 量级，仍在 <80 MB 预算内） | — |
| 启动 | 不主动加载就无影响；按需延迟加载 | 无 |

## 4. 落地步骤

1. ✅ `scripts/bootstrap-qpdf.ps1`：固定版本 + SHA-256，风格对齐 `bootstrap-pdfium.ps1`；
   下载失败不写半成品。已实测（12.4.2 mingw64，哈希与官方一致）。
2. ✅ CMake：`find_package(qpdf CONFIG QUIET)`，找不到直接 `FATAL_ERROR` 并打印获取命令；
   `AIPDF_WITH_QPDF=OFF` 同样直接失败——**不再有「无 qpdf 构建」这条降级路径**；
   `tools/qpdf/lib/cmake/qpdf` 自动定位，运行时自动把 `qpdf30.dll` 复制到 `bin`。
3. ✅ 写入路径暂不换后端：qpdf 目前只做**落盘前的结构校验**（`src/pdf/QpdfBackend`），
   序列化与增量写入仍走手写实现。**接口未变**，调用方一行没改。
   等 1.6 加密 / 1.7 签名需要对象级写入时再切换。
4. ✅ 回归：全量 34 条测试通过（qpdf 开启时，保存路径的 10 条测试全部经过 qpdf 校验）。
5. ✅ CI 覆盖：流水线启动时自动获取 qpdf（GitHub Actions 按版本缓存 `tools/qpdf`），
   拿不到就**中止流水线**，不再降级为"无 qpdf"构建。
6. ✅ 打包与 SBOM 已纳入：`package.ps1` **要求** `bin` 里必须有 `qpdf30.dll`，缺了直接中止打包；
   `LicenseMap.ps1` 把 `qpdf*.dll` 判为 Apache-2.0，`generate-sbom.ps1` 增加 qpdf 组件，
   `licenses/Apache-2.0.txt` 进 `CHECKSUMS.txt`。实测 0.15.0 包：许可证门、SBOM 门、无 SDK 启动门全绿。

## 5. 验收

- [x] 全量测试通过（当前 34 条，qpdf 开启）。
- [x] `qpdf --check` 对写出的增量文件通过（12.4.2 实测：`No syntax or stream encoding errors`）。
- [ ] 同一份产物在 PDFium、Windows 渲染器、福昕三处外观一致。
- [x] 安装包体积增量 < 12 MB（实测 DLL 9.2 MB / 压缩后约 +1.5 MB，0.15.0 包 21.23 MB），
      冷启动无可测量回归（无 SDK 启动验收通过）。
- [ ] SBOM 与许可证清单一致（发布门 `verify-sbom.ps1` / `verify-licenses.ps1` 仍绿）。

## 6. 风险与回滚

- **风险一**：qpdf 是 C++ 库，异常与线程模型需要包一层，避免异常穿透到 UI。
  对策：所有入口 `try/catch` 转成 `error` 字符串，和现有 `SaveOutcome` 对齐。
- **风险二**：vcpkg 构建在 CI 上不稳定（首次编译耗时长、缓存失效）。
  对策：预编译 release 包 + 固定 SHA 作为主路径，vcpkg 只用于本地开发。
- **风险三**：多一个 DLL 增加供应链面。
  对策：来源固定、哈希校验、纳入 SBOM 与依赖扫描。
- **回滚**：删除 `AIPDF_WITH_QPDF` 定义即回到手写实现，产物与行为与今天一致；
  不需要改调用方。

## 7. 需要批准的三件事

1. 允许把 qpdf（Apache-2.0）作为**源码构建或预编译**依赖进入发布包；
2. 版本策略：跟随 vcpkg `builtin-baseline`，还是固定到具体 release 并自己维护 SHA；
3. 由谁负责许可证与 NOTICE 的复核（当前 `licenses/CHECKSUMS.txt` 是逐份哈希校验的，
   qpdf 的 NOTICE 需要新增一份）。

批准后我会先落第 1–3 步（脚本 + 构建开关 + 后端骨架），再动迁移。

## 8. qpdf 的加密与权限位语义（R6 / AES-256 实测）

这一节记的是**用 qpdf 12.4.2 探针实测出来**的行为，不是照文档转述 ——
三条都与直觉相反，写在这里是为了下次别再按直觉写代码。

### 8.1 权限的权威来源是 `/Perms`，不是 `/P`

R6（AES-256）下，权限位写在加密字典的 `/Perms` 里；`/P` 只是**兼容项**。
只改 `/P` 的后果不是「权限没变」，而是**被 qpdf 当成损坏**：

```
qpdf: /Perms field ... doesn't match expected value
```

而且**有效权限根本没变**（读权限位与加密前的文档完全一致）。
所以「事后单独改权限位」这件事，语义只能是
**用所有者口令解密 → 按新权限重新加密**，不是原地打补丁。

### 8.2 重新加密时哪些项要一起同步

`reencryptWithPermissions()`（`src/pdf/security/PdfSecurity.cpp`）一次性同步六项：
`/P`、`/Perms`、`/O`、`/OE`、`/U`、`/UE`。
少同步一项的后果各不相同：少 `/Perms` → qpdf 报上面的错；少 `/U` / `/UE` →
读回时所有者口令对不上；少 `/O` / `/OE` → 所有者解锁链路断。

### 8.3 比对权限位时只看 8 位掩码

qpdf 在生成 `/Perms` 时会把**保留位置 1**，并把**无障碍提取位恒置 1**
（PDF 2.0 规定始终允许，不能关闭）。所以直接逐位比对 `/P` 与期望值永远对不上。
本仓库用 `kManagedPermissionBits` 掩码只比**管理的 8 位**，无障碍位另行处理
（界面勾上且禁用）。

### 8.4 打开口令不能等于所有者口令

R6 下两者相同 = 「用户即所有者」，权限限制形同虚设。
领域层与权限管理面板都拦这一条（`reencryptWithPermissions()` 拒绝，
`PermissionPanelDialog::validate()` 也拒绝）。

### 8.5 写盘链路上的其它两条

- **落盘前后的校验必须用同一把钥匙**：结构校验、重新打开的自检都要吃
  「最终字节实际使用的那个密码」，不能让各层各自默认空值 ——
  否则保存一个加密文档会因为「按空密码开」而报 `invalid password`，
  用户看到的是一句莫名其妙的结构错误。
- **清理类功能对加密文档要原样保留加密**（qpdf 侧 `setPreserveEncryption(true)`），
  且写盘后用同一个密码复验。qpdf 默认保留加密，但预过滤、内容归一化、qdf 模式会悄悄关掉它。

