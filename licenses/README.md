# 第三方许可证全文

安装包需要随附每个随包分发的第三方组件的许可证全文。以下文件由
`scripts/fetch-licenses.ps1` 取得并按 `CHECKSUMS.txt` 校验。

| 文件 | 适用组件 | 来源 |
|---|---|---|
| `LGPL-3.0.txt` | Qt 6.8.3（LGPL v3） | https://www.gnu.org/licenses/lgpl-3.0.txt |
| `GPL-3.0.txt` | LGPL-3.0 引用的 GPLv3 全文；MinGW-w64 运行库的 GPLv3 部分 | https://www.gnu.org/licenses/gpl-3.0.txt |
| `GCC-RUNTIME-LIBRARY-EXCEPTION.txt` | MinGW-w64 运行库（libgcc / libstdc++ / libwinpthread）的 GCC 运行时例外 | 随 MinGW-w64 GCC 13.1.0 工具包分发的 `licenses/gcc/COPYING.RUNTIME` |

## 校验

```powershell
powershell -ExecutionPolicy Bypass -File scripts\fetch-licenses.ps1 -VerifyOnly
```

`-VerifyOnly` 只校验本目录内的文件与 `CHECKSUMS.txt` 是否一致；
不带参数则从上表来源重新下载并校验。
