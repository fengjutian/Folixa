# 第三方许可证全文

安装包需要随附每个随包分发的第三方组件的许可证全文。以下文件由
`scripts/fetch-licenses.ps1` 取得并按 `CHECKSUMS.txt` 校验。

| 文件 | 适用组件 | 来源 |
|---|---|---|
| `LGPL-3.0.txt` | Qt 6.8.3（LGPL v3） | https://www.gnu.org/licenses/lgpl-3.0.txt |
| `GPL-3.0.txt` | LGPL-3.0 引用的 GPLv3 全文；MinGW-w64 运行库的 GPLv3 部分 | https://www.gnu.org/licenses/gpl-3.0.txt |
| `GCC-RUNTIME-LIBRARY-EXCEPTION.txt` | MinGW-w64 运行库（libgcc / libstdc++ / libwinpthread）的 GCC 运行时例外 | 随 MinGW-w64 GCC 13.1.0 工具包分发的 `licenses/gcc/COPYING.RUNTIME` |

## PDFium 许可证材料清单

`pdfium-CHECKSUMS.txt` 是 PDFium 及其第三方组件（19 份：abseil、agg23、dragonbox、
fast_float、freetype、harfbuzz、highway、icu、lcms、libjpeg-turbo、libopenjpeg、libpng、
llvm-libc、pdfium、simdutf、zlib 等）的许可证材料清单，格式为
`<SHA-256>  <包内文件名>  <仓库内来源路径>`，包内位置为 `licenses/pdfium/`。

它同时是三道门的输入：

- `scripts/bootstrap-pdfium.ps1` 核对清单与固定版本 PDFium 的实际材料是否一致，不一致直接报错。
- `scripts/package.ps1` 按清单逐份复制并核对哈希，缺一份或哈希不符都在打包阶段中止。
- `scripts/verify-licenses.ps1` 核对安装包内的文件集合与哈希，不少也不多。

清单可由固定来源重新生成：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\bootstrap-pdfium.ps1 -RegenerateLicenseManifest
```

## 校验

```powershell
powershell -ExecutionPolicy Bypass -File scripts\fetch-licenses.ps1 -VerifyOnly
```

`-VerifyOnly` 只校验本目录内的文件与 `CHECKSUMS.txt` 是否一致；
不带参数则从上表来源重新下载并校验。

### 哈希按规范化文本计算

上游来源的行尾并不统一：gnu.org 提供的 LGPL / GPL 是 LF，MinGW 工具包里的
`COPYING.RUNTIME` 是 CRLF；而仓库按 `.gitattributes`（`* text=auto eol=lf`）
以 LF 存对象库，检出时也按 LF 落地。

如果直接对文件字节算哈希，同一份许可证会因行尾不同而得到两个哈希：
本地工作区可能是 CRLF，CI runner 检出的是 LF，清单里却只能记一个 ——
远端流水线会必然失败（这是本仓库实际踩过的坑）。

因此 `CHECKSUMS.txt` 记录的是**规范化文本**（CRLF 与孤立 CR 全部折成 LF）
的 SHA-256，由 `scripts/LicenseText.ps1` 统一实现，三处行为一致：

- 取件时写入规范化后的内容，工作区文件与仓库对象库保持字节一致；
- 校验时先归一化再算哈希，与检出方式、机器行尾策略无关；
- 打包时写入规范化后的内容，安装包在任意机器上字节可复现。

`scripts/test-gates.ps1` 的 I2 / I4 用例分别守住两件事：
「内容被篡改必须失败」和「仅行尾不同必须仍然通过」。

PDFium 及其第三方组件的许可证原文不走这套规则：它们来自固定 SHA-256 的
官方 tarball，字节完全确定，用原始字节哈希才是更强的保证。
