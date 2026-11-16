# 签名夹具

「验签 UI」底座（`src/pdf/SignatureInspector.*`）的测试夹具。
**这些文件是提交进仓库的**：CI 上不需要 pyHanko、不需要 Python 加密库，
直接就能读到一份真签过名的 PDF。

| 文件 | 说明 |
| --- | --- |
| `signed-sample.pdf` | 真签名文档：PKCS#7 detached，`/SubFilter /adbe.pkcs7.detached` |
| `signed-sample-tampered.pdf` | 篡改样本：把 `signed-sample.pdf` 正文里的一个字节 `A` 改成 `B` |
| `signed-sample.p7` | `signed-sample.pdf` 里签名域 `/Contents` 的原始 PKCS#7 DER（去掉签名器预留的 0x00 填充） |
| `unsigned-sample.pdf` | 未签名文档：用来验证「无签名」不会被误报成「有签名」 |

## 篡改样本为什么能被抓到

`/ByteRange` 记的是**绝对偏移**。同长度替换一个字节不会挪动任何偏移，
所以「空洞位置对不对」这种结构检查照样通过——只有把覆盖到的字节重新算一遍
摘要、跟签名里的 `messageDigest` 对比，才能发现文件被改过。
`SignatureInspector` 的覆盖检查就做这件事。

## 怎么重新生成

夹具是**一次性**产物：私钥用完即弃，不进仓库（仓库的 `.gitignore` 也拦 `*.pem`）。
重新生成会得到新的公钥/指纹，但用例只断言格式与结构，不断言具体指纹值，
所以重新生成不会让测试挂掉（序列号 `0x4A3B2C1D0E9F` 是脚本里写死的，会保持不变）。

依赖：`tools/pyhanko/pyhanko.exe`（仓库自带，见 `scripts/bootstrap-pyhanko.ps1`）
与 Python 的 `cryptography` 包（只用于生成一次性的自签证书）。

```powershell
# 1) 生成自签证书 + 私钥，并造一份单页未签名 PDF
python build/sig-fixtures-scratch/make_fixtures.py

# 2) pyHanko 实际签名的那一步（脚本内部就是这条命令）
tools\pyhanko\pyhanko.exe sign addsig `
    --field "1/60,660,300,720/Signature1" `
    --name "AI PDF Editor Test Signer" `
    --reason "AI PDF Editor signature fixture" `
    --location "Local Test" `
    pemder --key signer-key.pem --cert signer-cert.pem --no-pass `
    unsigned-sample.pdf signed-sample.pdf
```

注意 pyHanko 的命令行是分组的两段：`sign addsig <选项> pemder <密钥选项> INFILE OUTFILE`。

正文里那行文字（`AI PDF Editor signature fixture`）是故意保留的：
测试要靠它定位篡改位置，内容流也故意不压缩，
改一个字节不会让解压失败，测的才是「摘要对不上」而不是「文件坏了」。
