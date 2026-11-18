#include "pdf/security/SignatureVerifier.h"

#include "pdf/security/RsaVerifier.h"
#include "pdf/security/SignatureInspector.h"

#include <QDateTime>
#include <QTimeZone>

#include <algorithm>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#endif

namespace aipdf::pdf {

namespace {

// ------------------------------------------------------------------ DER ----
//
// 为什么自己写一个 DER 解析器：
// 验签要拿到的东西 Crypt32 一个都不给——
//   1) SignedAttributes 的**原始字节**（签名就是签在它上面的）；
//   2) signatureValue 的原始字节；
//   3) 证书里的 RSA modulus/exponent（要拼成 BCRYPT_RSAKEY_BLOB）。
//
// 另外，CertGetPublicKey 给的是 CryptoAPI 那套 blob，模数在里头是**小端**，
// 而 BCrypt 要的是**大端**，转换时非常容易搞反（搞反了不会崩，只会永远验不过）。
// DER 里的整数本来就是大端，所以这里直接自己解 DER，顺带绕开大小端陷阱。

constexpr quint8 kTagInteger = 0x02;
constexpr quint8 kTagBitString = 0x03;
constexpr quint8 kTagOctetString = 0x04;
constexpr quint8 kTagOid = 0x06;
constexpr quint8 kTagEnumerated = 0x0a;
constexpr quint8 kTagUtf8String = 0x0c;
constexpr quint8 kTagSequence = 0x30;
constexpr quint8 kTagSet = 0x31;
constexpr quint8 kTagUtcTime = 0x17;
constexpr quint8 kTagGeneralizedTime = 0x18;
constexpr quint8 kTagContext0 = 0xa0;
constexpr quint8 kTagContext1 = 0xa1;
constexpr quint8 kTagContext2 = 0xa2;
constexpr quint8 kTagContext4 = 0xa4;
constexpr quint8 kTagContext6 = 0x86;

struct DerNode {
    quint8 tag = 0;
    int start = 0;       // TLV 起始偏移（tag 字节位置）
    int valueStart = 0;  // 值部分起始偏移
    QByteArray value;   // 值部分
};

// 读一个 TLV。失败返回 false，pos 不保证前进（调用方要能安全中断）。
bool readDerTlv(const QByteArray& buf, int& pos, int end, DerNode& node) {
    if (pos + 2 > end) {
        return false;
    }
    const int start = pos;
    const quint8 tag = static_cast<quint8>(buf.at(pos++));
    if ((tag & 0x1f) == 0x1f) {
        // 高位 tag 号形式：本模块用不到，判失败比猜语义安全。
        return false;
    }
    if (pos >= end) {
        return false;
    }
    const quint8 lengthByte = static_cast<quint8>(buf.at(pos++));
    qint64 length = 0;
    if (lengthByte & 0x80) {
        const int count = lengthByte & 0x7f;
        if (count == 0 || count > 4 || pos + count > end) {
            return false;
        }
        for (int i = 0; i < count; ++i) {
            length = (length << 8) | static_cast<quint8>(buf.at(pos++));
        }
    } else {
        length = lengthByte;
    }
    if (length < 0 || pos + length > end) {
        return false;
    }
    node.tag = tag;
    node.start = start;
    node.valueStart = pos;
    node.value = buf.mid(pos, static_cast<int>(length));
    pos += static_cast<int>(length);
    return true;
}

// OID 内容字节 → 点分十进制。
QString decodeDerOid(const QByteArray& value) {
    if (value.isEmpty()) {
        return QString();
    }
    const quint8 first = static_cast<quint8>(value.at(0));
    QStringList parts;
    // 第一个字节编码的是**两个**弧：40*X + Y。
    // 阈值是 40/80（不是 80/160）——写错会把 1.2.840.… 解成 0.42.840.…。
    if (first < 40) {
        parts << QString::number(0) << QString::number(first);
    } else if (first < 80) {
        parts << QString::number(1) << QString::number(first - 40);
    } else {
        parts << QString::number(2) << QString::number(first - 80);
    }
    quint64 accumulator = 0;
    for (int i = 1; i < value.size(); ++i) {
        const quint8 byte = static_cast<quint8>(value.at(i));
        accumulator = (accumulator << 7) | (byte & 0x7f);
        if ((byte & 0x80) == 0) {
            parts << QString::number(accumulator);
            accumulator = 0;
        }
    }
    return parts.join(QLatin1Char('.'));
}

// 点分十进制 OID → 内容字节。
QByteArray encodeDerOidContent(const QString& oid) {
    const QStringList parts = oid.split(QLatin1Char('.'));
    if (parts.size() < 2) {
        return QByteArray();
    }
    bool ok = false;
    const int first = parts.at(0).toInt(&ok);
    if (!ok) {
        return QByteArray();
    }
    const int second = parts.at(1).toInt(&ok);
    if (!ok) {
        return QByteArray();
    }
    QByteArray content;
    content.append(static_cast<char>(first * 40 + second));
    for (int i = 2; i < parts.size(); ++i) {
        const int arc = parts.at(i).toInt(&ok);
        if (!ok || arc < 0) {
            return QByteArray();
        }
        quint64 value = static_cast<quint64>(arc);
        quint8 chunk[10];
        int count = 0;
        do {
            chunk[count++] = static_cast<quint8>(value & 0x7f);
            value >>= 7;
        } while (value != 0);
        for (int j = count - 1; j >= 0; --j) {
            const quint8 byte = chunk[j];
            content.append(static_cast<char>(j == 0 ? byte : (byte | 0x80)));
        }
    }
    return content;
}

// DER 长度域（短格式 / 长格式）。
QByteArray encodeDerLength(int length) {
    QByteArray out;
    if (length < 0x80) {
        out.append(static_cast<char>(length));
        return out;
    }
    quint8 buffer[4];
    int count = 0;
    quint32 value = static_cast<quint32>(length);
    while (value != 0) {
        buffer[count++] = static_cast<quint8>(value & 0xff);
        value >>= 8;
    }
    out.append(static_cast<char>(0x80 | count));
    for (int i = count - 1; i >= 0; --i) {
        out.append(static_cast<char>(buffer[i]));
    }
    return out;
}

// 拼一个 TLV。
QByteArray derTlv(quint8 tag, const QByteArray& value) {
    QByteArray out;
    out.append(static_cast<char>(tag));
    out.append(encodeDerLength(value.size()));
    out.append(value);
    return out;
}

// 大端无符号字节 → INTEGER 的 TLV。
//
// DER 的整数是正数时最高位为 1 就必须补一个 0x00，否则会被读成负数。
QByteArray derInteger(const QByteArray& magnitude) {
    int index = 0;
    while (index < magnitude.size() - 1 && magnitude.at(index) == '\0') {
        ++index;
    }
    QByteArray trimmed = magnitude.mid(index);
    if (trimmed.isEmpty()) {
        trimmed.append('\0');
    }
    if (static_cast<quint8>(trimmed.at(0)) & 0x80) {
        trimmed.prepend('\0');
    }
    return derTlv(kTagInteger, trimmed);
}

// 时间统一转成「本地时区的 yyyy-MM-dd HH:mm:ss」。
QString formatDerTime(quint8 tag, const QByteArray& value) {
    const QString text = QString::fromLatin1(value);
    QDateTime dateTime;
    if (tag == kTagUtcTime && text.size() >= 11) {
        const int twoDigitYear = text.mid(0, 2).toInt();
        const int year = twoDigitYear >= 50 ? 1900 + twoDigitYear : 2000 + twoDigitYear;
        dateTime = QDateTime(QDate(year, text.mid(2, 2).toInt(), text.mid(4, 2).toInt()),
                             QTime(text.mid(6, 2).toInt(), text.mid(8, 2).toInt(),
                                   text.mid(10, 2).toInt()),
                             QTimeZone::UTC);
    } else if (tag == kTagGeneralizedTime && text.size() >= 13) {
        dateTime = QDateTime(QDate(text.mid(0, 4).toInt(), text.mid(4, 2).toInt(),
                                   text.mid(6, 2).toInt()),
                             QTime(text.mid(8, 2).toInt(), text.mid(10, 2).toInt(),
                                   text.mid(12, 2).toInt()),
                             QTimeZone::UTC);
    } else {
        return QString();
    }
    if (!dateTime.isValid()) {
        return QString();
    }
    return dateTime.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

// ---------------------------------------------------------------- X.509 ----

// 一张证书里验签要用到的字段。
struct CertificateFields {
    QByteArray subjectDer;        // subject Name 的完整 TLV
    QByteArray issuerDer;         // issuer Name 的完整 TLV
    QByteArray serialNumberDer;   // serialNumber INTEGER 的完整 TLV
    QByteArray serialNumberValue; // serialNumber INTEGER 的值部分（去掉 TLV 头）
    QByteArray publicKeyBits;     // subjectPublicKey 的 BIT STRING 内容（含未用位计数字节）
    // subjectPublicKeyInfo 的完整 TLV：
    // Crypt32 的 CryptImportPublicKeyInfo 直接吃这个 DER，
    // 不用自己拼 PUBLICKEYBLOB（那套格式的模数是小端，方向搞反不会崩、只会永远验不过）。
    QByteArray subjectPublicKeyInfoDer;
    QByteArray rsaModulus;        // 大端
    QByteArray rsaExponent;       // 大端
    bool hasRsaKey = false;
};

// 去掉整数前导的 0x00（DER 用来保证正数的那一个）。
QByteArray stripLeadingZeros(const QByteArray& value) {
    int index = 0;
    while (index < value.size() - 1 && value.at(index) == '\0') {
        ++index;
    }
    return value.mid(index);
}

// 取出一个 TLV 的完整字节（含 tag 与长度域）。
QByteArray tlvBytes(const QByteArray& buffer, const DerNode& node) {
    return buffer.mid(node.start, node.valueStart + node.value.size() - node.start);
}

// 解一张证书：只取验签要用的那几个字段。
//
// 只做浅层解析：不校验证书本身的签名，也不看有效期——
// 信任与否由上层的证书链面板负责，这里只回答「这个签名在数学上成立吗」。
bool parseCertificate(const QByteArray& der, CertificateFields& out, QString* error) {
    const auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    int pos = 0;
    DerNode certificate;
    if (!readDerTlv(der, pos, der.size(), certificate) || certificate.tag != kTagSequence) {
        return fail(QStringLiteral("证书数据不是合法的 DER 结构"));
    }
    int body = 0;
    DerNode tbs;
    if (!readDerTlv(certificate.value, body, certificate.value.size(), tbs) ||
        tbs.tag != kTagSequence) {
        return fail(QStringLiteral("证书里没有 tbsCertificate"));
    }

    int fieldPos = 0;
    const int fieldEnd = tbs.value.size();
    DerNode field;
    if (!readDerTlv(tbs.value, fieldPos, fieldEnd, field)) {
        return fail(QStringLiteral("证书的 tbsCertificate 是空的"));
    }
    if (field.tag == kTagContext0) {
        // version [0] EXPLICIT（可选）
        if (!readDerTlv(tbs.value, fieldPos, fieldEnd, field)) {
            return fail(QStringLiteral("证书的 version 字段不完整"));
        }
    }
    if (field.tag != kTagInteger) {
        return fail(QStringLiteral("证书里没有 serialNumber"));
    }
    out.serialNumberDer = tlvBytes(tbs.value, field);
    out.serialNumberValue = field.value;
    // signature AlgorithmIdentifier：跳过。
    if (!readDerTlv(tbs.value, fieldPos, fieldEnd, field)) {
        return fail(QStringLiteral("证书缺少签名算法字段"));
    }
    // issuer Name
    if (!readDerTlv(tbs.value, fieldPos, fieldEnd, field) || field.tag != kTagSequence) {
        return fail(QStringLiteral("证书的 issuer 不是合法的 Name"));
    }
    out.issuerDer = tlvBytes(tbs.value, field);
    // validity：跳过。
    if (!readDerTlv(tbs.value, fieldPos, fieldEnd, field)) {
        return fail(QStringLiteral("证书缺少有效期字段"));
    }
    // subject Name
    if (!readDerTlv(tbs.value, fieldPos, fieldEnd, field) || field.tag != kTagSequence) {
        return fail(QStringLiteral("证书的 subject 不是合法的 Name"));
    }
    out.subjectDer = tlvBytes(tbs.value, field);
    // subjectPublicKeyInfo
    if (!readDerTlv(tbs.value, fieldPos, fieldEnd, field) || field.tag != kTagSequence) {
        return fail(QStringLiteral("证书里没有 subjectPublicKeyInfo"));
    }
    int spkiPos = 0;
    DerNode spkiField;
    if (!readDerTlv(field.value, spkiPos, field.value.size(), spkiField) ||
        spkiField.tag != kTagSequence) {
        return fail(QStringLiteral("证书的 subjectPublicKeyInfo 不完整"));
    }
    if (!readDerTlv(field.value, spkiPos, field.value.size(), spkiField) ||
        spkiField.tag != kTagBitString) {
        return fail(QStringLiteral("证书里没有公钥"));
    }
    out.publicKeyBits = spkiField.value;
    // SPKI 本身在 subjectPublicKeyInfo 这个 SEQUENCE 里，往上再取一层。
    out.subjectPublicKeyInfoDer = tlvBytes(field.value, spkiField);

    // 公钥是不是 RSA：是就把 modulus/exponent 取出来（DER 里就是大端）。
    // 不是 RSA（比如 ECDSA）不算解析失败，只是这个模块验不了，
    // 由调用方给出「签名者证书不是 RSA 公钥」的结论。
    if (out.publicKeyBits.size() < 2 || static_cast<quint8>(out.publicKeyBits.at(0)) != 0) {
        return true;
    }
    const QByteArray keyDer = out.publicKeyBits.mid(1);
    int keyPos = 0;
    DerNode rsaPublicKey;
    if (!readDerTlv(keyDer, keyPos, keyDer.size(), rsaPublicKey) ||
        rsaPublicKey.tag != kTagSequence) {
        return true;
    }
    int rsaPos = 0;
    DerNode modulus;
    DerNode exponent;
    if (readDerTlv(rsaPublicKey.value, rsaPos, rsaPublicKey.value.size(), modulus) &&
        modulus.tag == kTagInteger &&
        readDerTlv(rsaPublicKey.value, rsaPos, rsaPublicKey.value.size(), exponent) &&
        exponent.tag == kTagInteger) {
        out.rsaModulus = stripLeadingZeros(modulus.value);
        out.rsaExponent = stripLeadingZeros(exponent.value);
        out.hasRsaKey = !out.rsaModulus.isEmpty() && !out.rsaExponent.isEmpty();
    }
    return true;
}

#ifdef Q_OS_WIN

// 把 Name 的 DER 转成可读字符串。
//
// 用 CertNameToStrW 而不是自己拼 RDN：主体名里可能有转义逗号，
// 按字符串切分会认错字段。第二个参数按 MS SDK 是非 const 指针，这里传本地变量。
QString nameDerToString(const QByteArray& nameDer) {
    if (nameDer.isEmpty()) {
        return QString();
    }
    CERT_NAME_BLOB blob;
    blob.cbData = static_cast<DWORD>(nameDer.size());
    blob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(nameDer.constData()));
    // CERT_X500_NAME_STR 而不是 CERT_X509_NAME_STR：后者只有新 SDK 才有，MinGW 上没有。
    const DWORD needed = CertNameToStrW(X509_ASN_ENCODING, &blob, CERT_X500_NAME_STR, nullptr, 0);
    if (needed == 0) {
        return QString();
    }
    QByteArray buffer(static_cast<int>(needed + 1) * static_cast<int>(sizeof(wchar_t)), '\0');
    wchar_t* text = reinterpret_cast<wchar_t*>(buffer.data());
    const DWORD written = CertNameToStrW(X509_ASN_ENCODING, &blob, CERT_X500_NAME_STR, text, needed);
    return QString::fromWCharArray(text, static_cast<int>(written));
}

#else  // !Q_OS_WIN

QString nameDerToString(const QByteArray& nameDer) {
    Q_UNUSED(nameDer);
    return QString();
}

#endif  // Q_OS_WIN

// 用证书里的 RSA 公钥验 PKCS#1 v1.5 签名。
//
// 这一步交给 RsaVerifier：模幂与填充校验都在那边实现，
// 不依赖 BCrypt / Crypt32 的验签 API（那两个在 MinGW 头文件里是旧参数表，
// 与系统 DLL 的导出对不上，调用会静默错位，表现为「永远验不过」）。
bool verifyRsaSignature(const CertificateFields& certificate,
                         const QByteArray& digest,
                         const QByteArray& signature,
                         const QString& digestAlgorithmOid,
                         QString* reason) {
    if (!certificate.hasRsaKey) {
        if (reason) {
            *reason = QStringLiteral("签名者证书里没有可用的 RSA 公钥");
        }
        return false;
    }
    return verifyRsaPkcs1Signature(certificate.rsaModulus, certificate.rsaExponent, digest,
                                   digestAlgorithmOid, signature, reason);
}

// ---------------------------------------------------------------- PKCS#7 ----

// 一份 PKCS#7（SignedData）里验签要用到的东西。
struct Pkcs7Signature {
    QString digestAlgorithmOid;
    bool hasSignedAttributes = false;
    QByteArray signedAttributesAsSet;  // 真正参与签名的字节（tag 换成 SET OF）
    QByteArray messageDigest;          // SignedAttributes 里的 messageDigest
    QByteArray signatureValue;
    QByteArray signerIssuerDer;        // SignerInfo 的 issuer（Name 的完整 TLV）
    QByteArray signerSerialNumber;     // SignerInfo 的 serialNumber（整数字节）
    QVector<QByteArray> certificates;  // PKCS#7 里带的证书
    QByteArray timestampToken;         // RFC 3161 时间戳令牌（没有则空）
};

// 解析 SignedAttributes：取 messageDigest 与签名时间。
void parseSignedAttributes(const QByteArray& attributes, Pkcs7Signature& signer) {
    int pos = 0;
    while (pos < attributes.size()) {
        DerNode attribute;
        if (!readDerTlv(attributes, pos, attributes.size(), attribute) ||
            attribute.tag != kTagSequence) {
            return;
        }
        int inner = 0;
        DerNode type;
        if (!readDerTlv(attribute.value, inner, attribute.value.size(), type) ||
            type.tag != kTagOid) {
            continue;
        }
        const QString oid = decodeDerOid(type.value);
        DerNode values;
        if (!readDerTlv(attribute.value, inner, attribute.value.size(), values) ||
            values.tag != kTagSet) {
            continue;
        }
        int valuePos = 0;
        DerNode single;
        if (!readDerTlv(values.value, valuePos, values.value.size(), single)) {
            continue;
        }
        if (oid == QLatin1String("1.2.840.113549.1.9.4") && single.tag == kTagOctetString) {
            signer.messageDigest = single.value;
        }
    }
}

// 解析 UnsignedAttributes：找 RFC 3161 的时间戳令牌（id-aa-timeStampToken）。
void parseUnsignedAttributes(const QByteArray& attributes, Pkcs7Signature& signer) {
    int pos = 0;
    while (pos < attributes.size()) {
        DerNode attribute;
        if (!readDerTlv(attributes, pos, attributes.size(), attribute) ||
            attribute.tag != kTagSequence) {
            return;
        }
        int inner = 0;
        DerNode type;
        if (!readDerTlv(attribute.value, inner, attribute.value.size(), type) ||
            type.tag != kTagOid) {
            continue;
        }
        const QString oid = decodeDerOid(type.value);
        DerNode values;
        if (!readDerTlv(attribute.value, inner, attribute.value.size(), values) ||
            values.tag != kTagSet) {
            continue;
        }
        int valuePos = 0;
        DerNode single;
        if (!readDerTlv(values.value, valuePos, values.value.size(), single)) {
            continue;
        }
        // 1.2.840.113549.1.9.16.2.14 = id-aa-timeStampToken
        if (oid == QLatin1String("1.2.840.113549.1.9.16.2.14") &&
            single.tag == kTagOctetString) {
            signer.timestampToken = single.value;
        }
    }
}

// 解析一份 PKCS#7（SignedData），只取第一个签名者。
bool parsePkcs7Signature(const QByteArray& der, Pkcs7Signature& out, QString* error) {
    const auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    int pos = 0;
    DerNode top;
    if (!readDerTlv(der, pos, der.size(), top) || top.tag != kTagSequence) {
        return fail(QStringLiteral("签名数据不是合法的 PKCS#7 结构"));
    }
    int body = 0;
    DerNode contentType;
    if (!readDerTlv(top.value, body, top.value.size(), contentType) ||
        contentType.tag != kTagOid) {
        return fail(QStringLiteral("签名数据缺少 contentType"));
    }
    // 1.2.840.113549.1.7.2 = signedData
    if (decodeDerOid(contentType.value) != QLatin1String("1.2.840.113549.1.7.2")) {
        return fail(QStringLiteral("签名数据不是 PKCS#7 SignedData"));
    }
    DerNode explicitContent;
    if (!readDerTlv(top.value, body, top.value.size(), explicitContent) ||
        explicitContent.tag != kTagContext0) {
        return fail(QStringLiteral("签名数据缺少 SignedData 内容"));
    }
    int signedPos = 0;
    DerNode signedData;
    if (!readDerTlv(explicitContent.value, signedPos, explicitContent.value.size(), signedData) ||
        signedData.tag != kTagSequence) {
        return fail(QStringLiteral("签名数据里的 SignedData 不完整"));
    }

    // SignedData: version, digestAlgorithms, contentInfo, [0] certs, [1] crls, signerInfos
    int dataPos = 0;
    const int dataEnd = signedData.value.size();
    DerNode cursor;
    for (int i = 0; i < 3; ++i) {
        if (!readDerTlv(signedData.value, dataPos, dataEnd, cursor)) {
            return fail(QStringLiteral("签名数据里的 SignedData 头部不完整"));
        }
    }
    DerNode signerInfos;
    while (dataPos < dataEnd) {
        if (!readDerTlv(signedData.value, dataPos, dataEnd, cursor)) {
            return fail(QStringLiteral("签名数据里的 SignerInfo 不完整"));
        }
        if (cursor.tag == kTagContext0) {
            // certificates [0] IMPLICIT：内容是若干个 Certificate 挨在一起。
            int certPos = 0;
            while (certPos < cursor.value.size()) {
                DerNode certificate;
                if (!readDerTlv(cursor.value, certPos, cursor.value.size(), certificate)) {
                    break;
                }
                if (certificate.tag == kTagSequence) {
                    out.certificates.append(tlvBytes(cursor.value, certificate));
                }
            }
            continue;
        }
        if (cursor.tag == kTagSet) {
            signerInfos = cursor;
            break;
        }
    }
    if (signerInfos.tag != kTagSet) {
        return fail(QStringLiteral("签名数据里没有签名者信息"));
    }

    int signerPos = 0;
    DerNode signerInfo;
    if (!readDerTlv(signerInfos.value, signerPos, signerInfos.value.size(), signerInfo) ||
        signerInfo.tag != kTagSequence) {
        return fail(QStringLiteral("签名数据里的 SignerInfo 不完整"));
    }
    int fieldPos = 0;
    const int fieldEnd = signerInfo.value.size();
    DerNode field;
    // version
    if (!readDerTlv(signerInfo.value, fieldPos, fieldEnd, field)) {
        return fail(QStringLiteral("SignerInfo 是空的"));
    }
    // issuerAndSerialNumber
    if (!readDerTlv(signerInfo.value, fieldPos, fieldEnd, field)) {
        return fail(QStringLiteral("SignerInfo 缺少签名者标识"));
    }
    if (field.tag == kTagSequence) {
        int namePos = 0;
        DerNode issuer;
        DerNode serial;
        if (readDerTlv(field.value, namePos, field.value.size(), issuer) &&
            readDerTlv(field.value, namePos, field.value.size(), serial) &&
            serial.tag == kTagInteger) {
            out.signerIssuerDer = tlvBytes(field.value, issuer);
            out.signerSerialNumber = serial.value;
        }
    }
    // digestAlgorithm
    if (!readDerTlv(signerInfo.value, fieldPos, fieldEnd, field)) {
        return fail(QStringLiteral("SignerInfo 缺少摘要算法"));
    }
    if (field.tag == kTagSequence) {
        int algPos = 0;
        DerNode algOid;
        if (readDerTlv(field.value, algPos, field.value.size(), algOid) && algOid.tag == kTagOid) {
            out.digestAlgorithmOid = decodeDerOid(algOid.value);
        }
    }
    // signedAttrs [0] IMPLICIT
    if (!readDerTlv(signerInfo.value, fieldPos, fieldEnd, field)) {
        return fail(QStringLiteral("SignerInfo 缺少签名字段"));
    }
    if (field.tag == kTagContext0) {
        out.hasSignedAttributes = true;
        // 参与签名的是这段字节，但 tag 必须是 SET OF（0x31）——CMS 的老规矩：
        // 签名器算哈希时用的是 SET 的编码，文件里存的却是 [0] IMPLICIT。
        // 长度域不动，只改第一个字节，这样字节级完全对得上签名器的输入。
        QByteArray signedBytes = tlvBytes(signerInfo.value, field);
        signedBytes[0] = static_cast<char>(kTagSet);
        out.signedAttributesAsSet = signedBytes;
        parseSignedAttributes(field.value, out);
    }
    // signatureAlgorithm
    if (!readDerTlv(signerInfo.value, fieldPos, fieldEnd, field)) {
        return fail(QStringLiteral("SignerInfo 缺少签名算法"));
    }
    // signature OCTET STRING
    if (!readDerTlv(signerInfo.value, fieldPos, fieldEnd, field)) {
        return fail(QStringLiteral("SignerInfo 缺少签名值"));
    }
    if (field.tag == kTagOctetString) {
        out.signatureValue = field.value;
    }
    // unsignedAttrs [1] IMPLICIT（可选）
    if (fieldPos < fieldEnd) {
        DerNode unsignedAttrs;
        if (readDerTlv(signerInfo.value, fieldPos, fieldEnd, unsignedAttrs) &&
            unsignedAttrs.tag == kTagContext1) {
            parseUnsignedAttributes(unsignedAttrs.value, out);
        }
    }
    if (out.signatureValue.isEmpty()) {
        return fail(QStringLiteral("签名数据里没有签名值"));
    }
    return true;
}

// ---------------------------------------------------------------- 摘要 ----

// 摘要算法 OID → 名字。
// 不认得的 OID 返回空字符串——空字符串意味着「不能判」，不是「判成通过」。
QString digestAlgorithmNameForOid(const QString& oid) {
    if (oid == QLatin1String("1.3.14.3.2.26")) {
        return QStringLiteral("SHA-1");
    }
    if (oid == QLatin1String("2.16.840.1.101.3.4.2.1")) {
        return QStringLiteral("SHA-256");
    }
    if (oid == QLatin1String("2.16.840.1.101.3.4.2.2")) {
        return QStringLiteral("SHA-384");
    }
    if (oid == QLatin1String("2.16.840.1.101.3.4.2.3")) {
        return QStringLiteral("SHA-512");
    }
    if (oid == QLatin1String("2.16.840.1.101.3.4.2.4")) {
        return QStringLiteral("SHA-224");
    }
    return QString();
}

#ifdef Q_OS_WIN

// 摘要算法 OID → BCrypt 的算法名。
const wchar_t* bcryptHashAlgorithmForOid(const QString& oid) {
    if (oid == QLatin1String("1.3.14.3.2.26")) {
        return BCRYPT_SHA1_ALGORITHM;
    }
    if (oid == QLatin1String("2.16.840.1.101.3.4.2.1")) {
        return BCRYPT_SHA256_ALGORITHM;
    }
    if (oid == QLatin1String("2.16.840.1.101.3.4.2.2")) {
        return BCRYPT_SHA384_ALGORITHM;
    }
    if (oid == QLatin1String("2.16.840.1.101.3.4.2.3")) {
        return BCRYPT_SHA512_ALGORITHM;
    }
    if (oid == QLatin1String("2.16.840.1.101.3.4.2.4")) {
        // MinGW 的 bcrypt.h 里没有 BCRYPT_SHA224_ALGORITHM 这个常量，
        // 但 BCrypt 的算法名本身就是字符串，直接写 "SHA224"。
        // 万一系统不提供这个算法，BCryptOpenAlgorithmProvider 会失败，
        // 上面那一步就会把失败原因写清楚，不会静默当成验签通过。
        return L"SHA224";
    }
    return nullptr;
}

// 用 BCrypt 算摘要。
//
// 选 BCrypt 而不是 CryptHashCertificate2：后者在部分 MinGW 版本上没有声明，
// 而且只能对证书算，算不了文件字节。
bool hashWithBcrypt(const QByteArray& data, const wchar_t* algorithm, QByteArray* digest) {
    if (!algorithm || !digest) {
        return false;
    }
    BCRYPT_ALG_HANDLE algorithmHandle = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithmHandle, algorithm, nullptr, 0) != 0) {
        return false;
    }
    DWORD objectLength = 0;
    DWORD hashLength = 0;
    DWORD returned = 0;
    if (BCryptGetProperty(algorithmHandle, BCRYPT_OBJECT_LENGTH,
                           reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
                           &returned, 0) != 0 ||
        BCryptGetProperty(algorithmHandle, BCRYPT_HASH_LENGTH,
                           reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength),
                           &returned, 0) != 0) {
        BCryptCloseAlgorithmProvider(algorithmHandle, 0);
        return false;
    }
    QByteArray objectBuffer(static_cast<int>(objectLength), '\0');
    BCRYPT_HASH_HANDLE hashHandle = nullptr;
    if (BCryptCreateHash(algorithmHandle, &hashHandle,
                         reinterpret_cast<PUCHAR>(objectBuffer.data()), objectLength,
                         nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(algorithmHandle, 0);
        return false;
    }
    bool success = true;
    if (!data.isEmpty()) {
        success = BCryptHashData(hashHandle, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                                 static_cast<ULONG>(data.size()), 0) == 0;
    }
    QByteArray result;
    if (success) {
        result = QByteArray(static_cast<int>(hashLength), '\0');
        success = BCryptFinishHash(hashHandle, reinterpret_cast<PUCHAR>(result.data()),
                                   hashLength, 0) == 0;
    }
    BCryptDestroyHash(hashHandle);
    BCryptCloseAlgorithmProvider(algorithmHandle, 0);
    if (!success) {
        return false;
    }
    *digest = result;
    return true;
}

#else  // !Q_OS_WIN

bool hashWithBcrypt(const QByteArray& data, const wchar_t* algorithm, QByteArray* digest) {
    Q_UNUSED(data);
    Q_UNUSED(algorithm);
    Q_UNUSED(digest);
    return false;
}

#endif  // Q_OS_WIN

}  // namespace

QString digestAlgorithmNameFromOid(const QString& oid) {
    return digestAlgorithmNameForOid(oid);
}

// 从 PKCS#7 里认出签名者证书：按 issuer + serial 跟 SignerInfo 对上。
//
// 为什么不用「第一张证书就是签名者」这种假设：
// 交叉签名、缺中间证书时第一张可能是 CA 证书，认错了公钥就会验签失败，
// 而且失败原因会误导人（看起来像签名本身坏了）。
QByteArray signerCertificateDerFromPkcs7(const QByteArray& pkcs7Bytes, QString* error) {
    Pkcs7Signature signer;
    QString parseError;
    if (!parsePkcs7Signature(pkcs7Bytes, signer, &parseError)) {
        if (error) {
            *error = parseError;
        }
        return QByteArray();
    }
    if (signer.certificates.isEmpty()) {
        if (error) {
            *error = QStringLiteral("签名里没有带证书");
        }
        return QByteArray();
    }
    if (signer.signerIssuerDer.isEmpty() || signer.signerSerialNumber.isEmpty()) {
        if (error) {
            *error = QStringLiteral("签名里没有签名者标识");
        }
        return QByteArray();
    }
    for (const QByteArray& certificateDer : signer.certificates) {
        CertificateFields fields;
        QString certificateError;
        if (!parseCertificate(certificateDer, fields, &certificateError)) {
            continue;
        }
        if (fields.issuerDer != signer.signerIssuerDer) {
            continue;
        }
        if (stripLeadingZeros(fields.serialNumberValue) !=
            stripLeadingZeros(signer.signerSerialNumber)) {
            continue;
        }
        return certificateDer;
    }
    if (error) {
        *error = QStringLiteral("签名里没有与签名者标识匹配的证书");
    }
    return QByteArray();
}

bool verifyPkcs7Signature(const QByteArray& pkcs7Bytes,
                           const QByteArray& signedBytes,
                           const QByteArray& signerCertificateDer,
                           SignatureCryptoVerdict* verdict,
                           QString* error) {
    if (!verdict) {
        if (error) {
            *error = QStringLiteral("缺少验签结果参数");
        }
        return false;
    }
    *verdict = SignatureCryptoVerdict();
    const auto fail = [verdict, error](const QString& message) {
        verdict->failureReason = message;
        if (error) {
            *error = message;
        }
        return false;
    };
    if (pkcs7Bytes.isEmpty()) {
        return fail(QStringLiteral("签名数据为空，无法验签"));
    }
    Pkcs7Signature signer;
    QString parseError;
    if (!parsePkcs7Signature(pkcs7Bytes, signer, &parseError)) {
        return fail(parseError);
    }

    // 摘要算法：认不得就不能判——「认不出」必须判成不成立，不能当成通过。
    verdict->algorithm = digestAlgorithmNameForOid(signer.digestAlgorithmOid);
    verdict->hashAlgorithmKnown = !verdict->algorithm.isEmpty();
    if (!verdict->hashAlgorithmKnown) {
        return fail(QStringLiteral("不认识的摘要算法：%1，无法验签").arg(signer.digestAlgorithmOid));
    }

    // 签名者证书：调用方指定的优先（界面的「单独复验」会传别的证书来对照），
    // 没指定就从 PKCS#7 里按 issuer+serial 认。
    QByteArray certificateDer = signerCertificateDer;
    if (certificateDer.isEmpty()) {
        certificateDer = signerCertificateDerFromPkcs7(pkcs7Bytes, nullptr);
    }
    if (certificateDer.isEmpty()) {
        return fail(QStringLiteral("签名里找不到签名者证书，无法验签"));
    }
    CertificateFields certificate;
    QString certificateError;
    if (!parseCertificate(certificateDer, certificate, &certificateError)) {
        return fail(certificateError);
    }
    verdict->signerSubject = nameDerToString(certificate.subjectDer);

    // 时间戳（RFC 3161）：没有就是没有，不编。
    if (!signer.timestampToken.isEmpty()) {
        QString timestamp;
        QString authority;
        if (parseTimestampToken(signer.timestampToken, &timestamp, &authority, nullptr)) {
            verdict->timestamp = timestamp;
            verdict->timestampAuthority = authority;
        }
    }

#ifdef Q_OS_WIN
    const wchar_t* bcryptAlgorithm = bcryptHashAlgorithmForOid(signer.digestAlgorithmOid);
#else
    const wchar_t* bcryptAlgorithm = nullptr;
#endif
    if (!bcryptAlgorithm) {
        return fail(QStringLiteral("本构建没有 %1 的摘要实现，无法验签").arg(verdict->algorithm));
    }

    // ---- 第一步：RSA 验签 ----
    //
    // 验签验的是**签名覆盖的那一段字节**，不是整份文件：
    //   * 有 SignedAttributes 时，签名签在 SignedAttributes 的 DER 上
    //     （文件里存的是 [0] IMPLICIT，参与签名的是 SET OF 的编码），
    //     而被签名的那份内容（PDF 的字节）通过 messageDigest 属性间接绑定；
    //   * 没有 SignedAttributes 时，签名直接签的就是内容摘要本身。
    if (signer.hasSignedAttributes && signedBytes.isEmpty()) {
        return fail(QStringLiteral("没有拿到被签名的字节，无法比对内容摘要"));
    }
    const QByteArray signedPayload =
        signer.hasSignedAttributes ? signer.signedAttributesAsSet : signedBytes;
    QByteArray digest;
    if (!hashWithBcrypt(signedPayload, bcryptAlgorithm, &digest)) {
        return fail(QStringLiteral("计算签名摘要失败"));
    }
    QString rsaReason;
    const bool rsaOk = verifyRsaSignature(certificate, digest, signer.signatureValue,
                                           signer.digestAlgorithmOid, &rsaReason);

    // ---- 第二步：内容摘要比对 ----
    //
    // 这一步才抓「文件被改过」：同长度改一个字节不会挪动任何偏移，
    // 签名值本身照样验得过，只有重算摘要跟 messageDigest 对不上才看得出来。
    if (signer.hasSignedAttributes) {
        QByteArray contentDigest;
        if (signer.messageDigest.isEmpty()) {
            verdict->digestMatched = false;
        } else if (!hashWithBcrypt(signedBytes, bcryptAlgorithm, &contentDigest)) {
            return fail(QStringLiteral("计算内容摘要失败"));
        } else {
            verdict->digestMatched = (contentDigest == signer.messageDigest);
        }
    } else {
        // 没有 SignedAttributes 时签名签的就是内容摘要，两者是一回事。
        verdict->digestMatched = rsaOk;
    }

    verdict->signatureValid = rsaOk && verdict->digestMatched;
    if (verdict->signatureValid) {
        verdict->failureReason.clear();
        if (error) {
            error->clear();
        }
        return true;
    }
    // 失败原因：先说 RSA（签名本身对不上），再说内容摘要（文件被改过）。
    QString reason = rsaOk ? QString() : rsaReason;
    if (reason.isEmpty()) {
        reason = QStringLiteral("签名覆盖范围的摘要与签名里的 messageDigest 不一致，文件内容被改动过");
    }
    verdict->failureReason = reason;
    if (error) {
        *error = reason;
    }
    return false;
}

QVector<SignatureCryptoVerdict> verifyDocumentSignatures(const QByteArray& pdfBytes,
                                                           QString* error) {
    QVector<SignatureCryptoVerdict> verdicts;
    const auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return QVector<SignatureCryptoVerdict>();
    };
    if (pdfBytes.isEmpty()) {
        return fail(QStringLiteral("PDF 内容为空，无法验签"));
    }
    QString inspectError;
    const QVector<SignatureInfo> signatures = inspectSignatures(pdfBytes, &inspectError);
    if (signatures.isEmpty()) {
        return fail(inspectError.isEmpty() ? QStringLiteral("文档里没有可验签的签名")
                                           : inspectError);
    }
    for (const SignatureInfo& signature : signatures) {
        SignatureCryptoVerdict verdict;
        if (signature.pkcs7.isEmpty()) {
            verdict.failureReason = QStringLiteral("签名域里没有 /Contents 数据");
            verdicts.append(verdict);
            continue;
        }
        // 先看覆盖范围：/ByteRange 必须首尾相接地铺满整份文件（只留 /Contents
        // 那个空洞）。只对“覆盖到的字节”验签，等于允许攻击者在文件尾部追加
        // 任意增量更新，而这里仍然报“签名有效”。
        //
        // 验不出来（缺少证书 API、空洞对不上）一律判不成立——契约见头文件：
        // “不能把验不出来当成验签通过”。
        if (!signature.coversDocument) {
            verdict.failureReason = signature.coverError.isEmpty()
                                        ? QStringLiteral("签名没有覆盖整份文档，/ByteRange 不完整")
                                        : signature.coverError;
            verdicts.append(verdict);
            continue;
        }
        // 被签名的字节：/ByteRange 覆盖到的那些区间拼起来（签名空洞不算在内）。
        QByteArray covered;
        bool rangeOk = !signature.byteRange.isEmpty();
        for (int i = 0; rangeOk && i + 1 < signature.byteRange.size(); i += 2) {
            const qint64 offset = signature.byteRange.at(i);
            const qint64 length = signature.byteRange.at(i + 1);
            if (offset < 0 || length < 0 || offset > pdfBytes.size() ||
                length > pdfBytes.size() - offset) {
                rangeOk = false;
                break;
            }
            covered.append(pdfBytes.mid(static_cast<int>(offset), static_cast<int>(length)));
        }
        if (!rangeOk) {
            verdict.failureReason = QStringLiteral("签名的 /ByteRange 无效，无法确定被签名的字节");
            verdicts.append(verdict);
            continue;
        }
        QString verifyError;
        verifyPkcs7Signature(signature.pkcs7, covered, QByteArray(), &verdict, &verifyError);
        verdicts.append(verdict);
    }
    if (error) {
        error->clear();
    }
    return verdicts;
}

bool parseTimestampToken(const QByteArray& tokenDer,
                          QString* timestamp,
                          QString* authority,
                          QString* error) {
    const auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    if (tokenDer.isEmpty()) {
        return fail(QStringLiteral("时间戳令牌为空"));
    }
    int pos = 0;
    DerNode top;
    if (!readDerTlv(tokenDer, pos, tokenDer.size(), top) || top.tag != kTagSequence) {
        return fail(QStringLiteral("时间戳令牌不是合法的 DER 结构"));
    }
    int body = 0;
    DerNode contentType;
    if (!readDerTlv(top.value, body, top.value.size(), contentType) || contentType.tag != kTagOid) {
        return fail(QStringLiteral("时间戳令牌缺少 contentType"));
    }
    // 1.2.840.113549.1.7.1 = id-ct-TSTInfo
    if (decodeDerOid(contentType.value) != QLatin1String("1.2.840.113549.1.7.1")) {
        return fail(QStringLiteral("时间戳令牌的内容类型不是 TSTInfo"));
    }
    DerNode explicitContent;
    if (!readDerTlv(top.value, body, top.value.size(), explicitContent) ||
        explicitContent.tag != kTagContext0) {
        return fail(QStringLiteral("时间戳令牌缺少内容"));
    }
    int inner = 0;
    DerNode octets;
    if (!readDerTlv(explicitContent.value, inner, explicitContent.value.size(), octets) ||
        octets.tag != kTagOctetString) {
        return fail(QStringLiteral("时间戳令牌的内容不是 OCTET STRING"));
    }
    int tbsPos = 0;
    DerNode tstInfo;
    if (!readDerTlv(octets.value, tbsPos, octets.value.size(), tstInfo) ||
        tstInfo.tag != kTagSequence) {
        return fail(QStringLiteral("时间戳令牌里的 TSTInfo 不完整"));
    }
    // TSTInfo: version, policy, messageImprint, serialNumber, genTime, ...
    int fieldPos = 0;
    const int fieldEnd = tstInfo.value.size();
    DerNode field;
    for (int i = 0; i < 5; ++i) {
        if (!readDerTlv(tstInfo.value, fieldPos, fieldEnd, field)) {
            return fail(QStringLiteral("时间戳令牌里的 TSTInfo 头部不完整"));
        }
        if (i == 4) {
            const QString time = formatDerTime(field.tag, field.value);
            if (time.isEmpty()) {
                return fail(QStringLiteral("时间戳令牌里的 genTime 不合法"));
            }
            if (timestamp) {
                *timestamp = time;
            }
        }
    }
    // tsa [0] GeneralName（可选）：directoryName [4] 是最常见的一种。
    while (fieldPos < fieldEnd) {
        if (!readDerTlv(tstInfo.value, fieldPos, fieldEnd, field)) {
            break;
        }
        if (field.tag != kTagContext0) {
            continue;
        }
        int namePos = 0;
        DerNode name;
        if (!readDerTlv(field.value, namePos, field.value.size(), name)) {
            break;
        }
        if (name.tag == kTagContext4) {
            // directoryName：内容要么是完整的 Name，要么只是 Name 的 RDN 部分，
            // 两种编码都遇到过——缺 SEQUENCE 头就补上。
            QByteArray nameDer = name.value;
            if (nameDer.isEmpty() || static_cast<quint8>(nameDer.at(0)) != kTagSequence) {
                nameDer = derTlv(kTagSequence, nameDer);
            }
            if (authority) {
                *authority = nameDerToString(nameDer);
            }
        } else if (name.tag == kTagContext6) {
            if (authority) {
                *authority = QString::fromLatin1(name.value);
            }
        }
        break;
    }
    return true;
}

namespace {

// ------------------------------------------------------------------ OCSP ----
//
// 这一段是给下面三个公开函数用的内部助手。
// 单独开一个匿名命名空间：上一段匿名命名空间在公开函数之前就闭合了
// （公开函数必须留在 aipdf::pdf 里，否则头文件声明的接口在链接期根本不存在）。
//
// RFC 6960 的结构（只列这里用到的部分）：
//
//   OCSPRequest ::= SEQUENCE { tbsRequest TBSRequest, ... }
//   TBSRequest  ::= SEQUENCE { version [0] OPTIONAL, requestorName [1] OPTIONAL,
//                              requestList SEQUENCE OF Request, ... }
//   Request     ::= SEQUENCE { reqCert CertID, ... }
//   CertID      ::= SEQUENCE { hashAlgorithm AlgorithmIdentifier,
//                              issuerNameHash OCTET STRING,
//                              issuerKeyHash OCTET STRING,
//                              serialNumber INTEGER }
//
//   OCSPResponse ::= SEQUENCE { responseStatus ENUMERATED,
//                                responseBytes [0] EXPLICIT ResponseBytes OPTIONAL }
//   ResponseBytes ::= SEQUENCE { responseType OBJECT IDENTIFIER, response OCTET STRING }
//   BasicOCSPResponse ::= SEQUENCE { tbsResponseData ResponseData, ... }
//   ResponseData ::= SEQUENCE { version [0] OPTIONAL, responderID ResponderID,
//                               producedAt GeneralizedTime,
//                               responses SEQUENCE OF SingleResponse, ... }
//   SingleResponse ::= SEQUENCE { certID CertID, certStatus CertStatus,
//                                 thisUpdate GeneralizedTime,
//                                 nextUpdate [0] EXPLICIT GeneralizedTime OPTIONAL, ... }
//   CertStatus ::= CHOICE { good [0] IMPLICIT NULL,
//                           revoked [1] IMPLICIT RevokedInfo,
//                           unknown [2] IMPLICIT UnknownInfo }

// CertStatus 用的是 IMPLICIT 标签：good / unknown 的 NULL 是**原始**上下文标签
//（0x80 / 0x82），revoked 的 RevokedInfo 是**构造**上下文标签（0xa1）。
// 这里两种写法都接受：不同实现（OpenSSL / Java / .NET）编码习惯并不完全一致。

constexpr quint8 kTagContext0Primitive = 0x80;
constexpr quint8 kTagContext2Primitive = 0x82;
constexpr quint8 kTagNull = 0x05;

// SHA-1：RFC 6960 的 CertID 固定用 SHA-1（不是更强的摘要）。
bool sha1Digest(const QByteArray& data, QByteArray* digest) {
#ifdef Q_OS_WIN
    return hashWithBcrypt(data, BCRYPT_SHA1_ALGORITHM, digest);
#else
    Q_UNUSED(data);
    Q_UNUSED(digest);
    return false;
#endif
}

// SHA-1 的 AlgorithmIdentifier（OID 1.3.14.3.2.26 + NULL 参数）。
QByteArray sha1AlgorithmIdentifier() {
    QByteArray body = derTlv(kTagOid, encodeDerOidContent(QStringLiteral("1.3.14.3.2.26")));
    body += derTlv(kTagNull, QByteArray());
    return derTlv(kTagSequence, body);
}

// 把一段 Name 的内容（不含外层 SEQUENCE 标签）包成完整 TLV。
// ResponderID 的 byName 是 IMPLICIT 标签，标签被 [1] 顶掉了，
// 拿不到完整 Name 就没法交给 CertNameToStrW。
QByteArray wrapNameSequence(const QByteArray& contents) {
    return derTlv(kTagSequence, contents);
}

}  // namespace

// 解析 OCSP 响应（DER）。
//
// 语义约定（和本文件其余部分一致）：
//   * 「解析不出结构」→ 返回 false，status = Malformed，error 写明中文原因；
//   * 「服务器明确说这次没查成」（responseStatus != successful）→ 返回 false，
//     status = Unknown。这**不是**「证书状态良好」，更不是「被吊销」——
//     只能理解成「查不到」，界面必须照原样显示。
bool parseOcspResponse(const QByteArray& der, OcspResponseInfo* info, QString* error) {
    const auto fail = [error, info](const QString& message, OcspStatus status) {
        if (info) {
            info->status = status;
        }
        if (error) {
            *error = message;
        }
        return false;
    };
    if (info == nullptr) {
        if (error) {
            *error = QStringLiteral("缺少输出参数");
        }
        return false;
    }
    *info = OcspResponseInfo();

    int pos = 0;
    DerNode response;
    if (!readDerTlv(der, pos, der.size(), response) || response.tag != kTagSequence) {
        return fail(QStringLiteral("OCSP 响应不是合法的 DER 结构"), OcspStatus::Malformed);
    }

    int body = 0;
    DerNode statusNode;
    if (!readDerTlv(response.value, body, response.value.size(), statusNode) ||
        statusNode.tag != kTagEnumerated) {
        return fail(QStringLiteral("OCSP 响应缺少响应状态字段"), OcspStatus::Malformed);
    }
    const int responseStatus =
        statusNode.value.isEmpty() ? 0 : static_cast<quint8>(statusNode.value.at(0));
    if (responseStatus != 0) {
        return fail(QStringLiteral("OCSP 服务器未返回证书状态（响应状态码 %1）").arg(responseStatus),
                    OcspStatus::Unknown);
    }

    // responseBytes [0] EXPLICIT ResponseBytes
    DerNode responseBytes;
    if (!readDerTlv(response.value, body, response.value.size(), responseBytes) ||
        responseBytes.tag != kTagContext0) {
        return fail(QStringLiteral("OCSP 响应里没有响应内容"), OcspStatus::Malformed);
    }
    int rbPos = 0;
    DerNode rbSeq;
    if (!readDerTlv(responseBytes.value, rbPos, responseBytes.value.size(), rbSeq) ||
        rbSeq.tag != kTagSequence) {
        return fail(QStringLiteral("OCSP 的响应内容结构不合法"), OcspStatus::Malformed);
    }
    int rbInner = 0;
    DerNode typeNode;
    if (!readDerTlv(rbSeq.value, rbInner, rbSeq.value.size(), typeNode) || typeNode.tag != kTagOid) {
        return fail(QStringLiteral("OCSP 的响应内容缺少响应类型"), OcspStatus::Malformed);
    }
    if (decodeDerOid(typeNode.value) != QLatin1String("1.3.6.1.5.5.7.48.1.1")) {
        return fail(QStringLiteral("只支持 id-pkix-ocsp-basic 类型的 OCSP 响应"),
                    OcspStatus::Malformed);
    }
    DerNode responseOctet;
    if (!readDerTlv(rbSeq.value, rbInner, rbSeq.value.size(), responseOctet) ||
        responseOctet.tag != kTagOctetString) {
        return fail(QStringLiteral("OCSP 的响应内容缺少 BasicOCSPResponse"), OcspStatus::Malformed);
    }

    // BasicOCSPResponse ::= SEQUENCE { tbsResponseData, signatureAlgorithm, signature, ... }
    int basicPos = 0;
    DerNode basic;
    if (!readDerTlv(responseOctet.value, basicPos, responseOctet.value.size(), basic) ||
        basic.tag != kTagSequence) {
        return fail(QStringLiteral("OCSP 的基本响应结构不合法"), OcspStatus::Malformed);
    }
    int basicInner = 0;
    DerNode responseData;
    if (!readDerTlv(basic.value, basicInner, basic.value.size(), responseData) ||
        responseData.tag != kTagSequence) {
        return fail(QStringLiteral("OCSP 响应里没有 responseData"), OcspStatus::Malformed);
    }

    int rdPos = 0;
    DerNode field;
    if (!readDerTlv(responseData.value, rdPos, responseData.value.size(), field)) {
        return fail(QStringLiteral("OCSP 的 responseData 是空的"), OcspStatus::Malformed);
    }
    if (field.tag == kTagContext0) {
        // version [0] EXPLICIT（可选）
        if (!readDerTlv(responseData.value, rdPos, responseData.value.size(), field)) {
            return fail(QStringLiteral("OCSP 的 responseData 版本字段不完整"), OcspStatus::Malformed);
        }
    }
    // ResponderID ::= CHOICE { byName [1] Name, byKey [2] KeyHash }
    if (field.tag == kTagContext1) {
        info->responderName = nameDerToString(wrapNameSequence(field.value));
    } else if (field.tag == kTagContext2) {
        info->responderName = QString::fromLatin1(field.value.toHex());
    } else {
        return fail(QStringLiteral("OCSP 响应里认不出响应者标识"), OcspStatus::Malformed);
    }

    // producedAt
    if (!readDerTlv(responseData.value, rdPos, responseData.value.size(), field) ||
        field.tag != kTagGeneralizedTime) {
        return fail(QStringLiteral("OCSP 响应缺少产生时间"), OcspStatus::Malformed);
    }
    info->producedAt = formatDerTime(field.tag, field.value);

    // responses SEQUENCE OF SingleResponse
    if (!readDerTlv(responseData.value, rdPos, responseData.value.size(), field) ||
        field.tag != kTagSequence) {
        return fail(QStringLiteral("OCSP 响应里没有证书状态列表"), OcspStatus::Malformed);
    }
    int singlePos = 0;
    DerNode single;
    if (!readDerTlv(field.value, singlePos, field.value.size(), single) ||
        single.tag != kTagSequence) {
        return fail(QStringLiteral("OCSP 的证书状态列表是空的"), OcspStatus::Malformed);
    }

    int si = 0;
    DerNode certId;
    if (!readDerTlv(single.value, si, single.value.size(), certId) ||
        certId.tag != kTagSequence) {
        return fail(QStringLiteral("OCSP 的单条状态缺少 certID"), OcspStatus::Malformed);
    }
    DerNode certStatus;
    if (!readDerTlv(single.value, si, single.value.size(), certStatus)) {
        return fail(QStringLiteral("OCSP 的单条状态缺少证书状态"), OcspStatus::Malformed);
    }
    if (certStatus.tag == kTagContext0Primitive || certStatus.tag == kTagContext0) {
        info->status = OcspStatus::Good;
    } else if (certStatus.tag == kTagContext2Primitive || certStatus.tag == kTagContext2) {
        info->status = OcspStatus::Unknown;
    } else if (certStatus.tag == kTagContext1) {
        info->status = OcspStatus::Revoked;
        // RevokedInfo ::= SEQUENCE { revocationTime GeneralizedTime,
        //                           revocationReason [0] EXPLICIT CRLReason OPTIONAL }
        int ri = 0;
        DerNode revocationTime;
        if (readDerTlv(certStatus.value, ri, certStatus.value.size(), revocationTime) &&
            revocationTime.tag == kTagGeneralizedTime) {
            info->revocationReason =
                QStringLiteral("吊销时间 ") + formatDerTime(revocationTime.tag, revocationTime.value);
        }
        DerNode reasonHolder;
        if (ri < certStatus.value.size() &&
            readDerTlv(certStatus.value, ri, certStatus.value.size(), reasonHolder) &&
            reasonHolder.tag == kTagContext0) {
            int en = 0;
            DerNode enumerated;
            if (readDerTlv(reasonHolder.value, en, reasonHolder.value.size(), enumerated) &&
                enumerated.tag == kTagEnumerated) {
                const int code =
                    enumerated.value.isEmpty() ? 0 : static_cast<quint8>(enumerated.value.at(0));
                info->revocationReason += QStringLiteral("，吊销原因码 %1").arg(code);
            }
        }
    } else {
        return fail(QStringLiteral("OCSP 的证书状态字段认不出"), OcspStatus::Malformed);
    }

    // thisUpdate
    DerNode timeNode;
    if (!readDerTlv(single.value, si, single.value.size(), timeNode) ||
        (timeNode.tag != kTagGeneralizedTime && timeNode.tag != kTagUtcTime)) {
        return fail(QStringLiteral("OCSP 的单条状态缺少 thisUpdate"), OcspStatus::Malformed);
    }
    info->thisUpdateValid = true;
    info->thisUpdate = formatDerTime(timeNode.tag, timeNode.value);

    // nextUpdate [0] EXPLICIT GeneralizedTime（可选）
    DerNode nextHolder;
    if (si < single.value.size() &&
        readDerTlv(single.value, si, single.value.size(), nextHolder) &&
        nextHolder.tag == kTagContext0) {
        int nt = 0;
        DerNode generalized;
        if (readDerTlv(nextHolder.value, nt, nextHolder.value.size(), generalized) &&
            generalized.tag == kTagGeneralizedTime) {
            info->nextUpdateValid = true;
            info->nextUpdate = formatDerTime(generalized.tag, generalized.value);
        }
    }

    if (error) {
        error->clear();
    }
    return true;
}

// 构造对一个证书的 OCSP 请求（DER）。
//
// CertID 的三个字段（RFC 6960 §4.1.1）：
//   issuerNameHash = 签发者 Name 的 DER 摘要；
//   issuerKeyHash  = 签发者公钥的摘要；
//   serialNumber   = 被查证书的序列号。
//
// 摘要算法固定 SHA-1（OID 1.3.14.3.2.26）——这是 RFC 写死的，不是因为 SHA-1 够安全，
// 而是因为所有 OCSP 响应方都按这个算，换算法就对不上。
QByteArray buildOcspRequest(const QByteArray& issuerCertificateDer,
                             const QByteArray& subjectCertificateDer,
                             QString* error) {
    const auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return QByteArray();
    };
    if (issuerCertificateDer.isEmpty()) {
        return fail(QStringLiteral("缺少签发者证书"));
    }
    if (subjectCertificateDer.isEmpty()) {
        return fail(QStringLiteral("缺少被查询的证书"));
    }

    CertificateFields issuer;
    CertificateFields subject;
    QString parseError;
    if (!parseCertificate(issuerCertificateDer, issuer, &parseError)) {
        return fail(QStringLiteral("签发者证书解析失败：") + parseError);
    }
    if (!parseCertificate(subjectCertificateDer, subject, &parseError)) {
        return fail(QStringLiteral("被查询证书解析失败：") + parseError);
    }

    // issuerNameHash：签发者的 Subject Name 的完整 DER 的 SHA-1。
    QByteArray nameHash;
    if (!sha1Digest(issuer.subjectDer, &nameHash)) {
        return fail(QStringLiteral("计算签发者名称摘要失败"));
    }

    // issuerKeyHash：签发者公钥的 SHA-1。
    // 注意要跳过 BIT STRING 内容的第一个字节——那是「未用位数」，不是密钥的一部分。
    if (issuer.publicKeyBits.size() < 2) {
        return fail(QStringLiteral("签发者公钥信息不完整"));
    }
    QByteArray keyHash;
    if (!sha1Digest(issuer.publicKeyBits.mid(1), &keyHash)) {
        return fail(QStringLiteral("计算签发者公钥摘要失败"));
    }

    // serialNumber：直接取证书里的那个 INTEGER 的值字节，
    // 不重新编码——重新编码可能把前导 0x00 去掉，值一样但字节不同。
    int serialPos = 0;
    DerNode serialNode;
    if (!readDerTlv(subject.serialNumberDer, serialPos, subject.serialNumberDer.size(), serialNode) ||
        serialNode.tag != kTagInteger) {
        return fail(QStringLiteral("被查询证书的序列号不是合法的整数"));
    }

    QByteArray certIdBody = sha1AlgorithmIdentifier();
    certIdBody += derTlv(kTagOctetString, nameHash);
    certIdBody += derTlv(kTagOctetString, keyHash);
    certIdBody += derTlv(kTagInteger, serialNode.value);

    const QByteArray request = derTlv(kTagSequence, derTlv(kTagSequence, certIdBody));
    const QByteArray requestList = derTlv(kTagSequence, request);
    const QByteArray tbsRequest = derTlv(kTagSequence, requestList);
    const QByteArray ocspRequest = derTlv(kTagSequence, tbsRequest);

    if (error) {
        error->clear();
    }
    return ocspRequest;
}

// 把一份 OCSP 请求解回结构（自检与调试用）。
bool parseOcspRequest(const QByteArray& der, OcspRequestInfo* info, QString* error) {
    const auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    if (info == nullptr) {
        if (error) {
            *error = QStringLiteral("缺少输出参数");
        }
        return false;
    }
    *info = OcspRequestInfo();

    int pos = 0;
    DerNode request;
    if (!readDerTlv(der, pos, der.size(), request) || request.tag != kTagSequence) {
        return fail(QStringLiteral("OCSP 请求不是合法的 DER 结构"));
    }
    int body = 0;
    DerNode tbs;
    if (!readDerTlv(request.value, body, request.value.size(), tbs) || tbs.tag != kTagSequence) {
        return fail(QStringLiteral("OCSP 请求里没有 tbsRequest"));
    }

    int tbsPos = 0;
    DerNode field;
    if (!readDerTlv(tbs.value, tbsPos, tbs.value.size(), field)) {
        return fail(QStringLiteral("OCSP 请求的 tbsRequest 是空的"));
    }
    if (field.tag == kTagContext0) {
        // version [0] EXPLICIT（可选）
        if (!readDerTlv(tbs.value, tbsPos, tbs.value.size(), field)) {
            return fail(QStringLiteral("OCSP 请求的版本字段不完整"));
        }
    }
    if (field.tag == kTagContext1) {
        // requestorName [1] EXPLICIT（可选）
        if (!readDerTlv(tbs.value, tbsPos, tbs.value.size(), field)) {
            return fail(QStringLiteral("OCSP 请求的请求者字段不完整"));
        }
    }
    if (field.tag != kTagSequence) {
        return fail(QStringLiteral("OCSP 请求里没有请求列表"));
    }
    int listPos = 0;
    DerNode single;
    if (!readDerTlv(field.value, listPos, field.value.size(), single) ||
        single.tag != kTagSequence) {
        return fail(QStringLiteral("OCSP 的请求列表是空的"));
    }
    int si = 0;
    DerNode certId;
    if (!readDerTlv(single.value, si, single.value.size(), certId) ||
        certId.tag != kTagSequence) {
        return fail(QStringLiteral("OCSP 请求里没有 CertID"));
    }
    int ci = 0;
    DerNode algorithm;
    if (!readDerTlv(certId.value, ci, certId.value.size(), algorithm) ||
        algorithm.tag != kTagSequence) {
        return fail(QStringLiteral("CertID 里没有摘要算法"));
    }
    int ai = 0;
    DerNode oid;
    if (!readDerTlv(algorithm.value, ai, algorithm.value.size(), oid) || oid.tag != kTagOid) {
        return fail(QStringLiteral("CertID 的摘要算法缺少 OID"));
    }
    const QString oidText = decodeDerOid(oid.value);
    info->hashAlgorithm = digestAlgorithmNameFromOid(oidText);
    if (info->hashAlgorithm.isEmpty()) {
        info->hashAlgorithm = oidText;
    }

    DerNode nameHash;
    if (!readDerTlv(certId.value, ci, certId.value.size(), nameHash) ||
        nameHash.tag != kTagOctetString) {
        return fail(QStringLiteral("CertID 里没有 issuerNameHash"));
    }
    info->issuerNameHash = nameHash.value;

    DerNode keyHash;
    if (!readDerTlv(certId.value, ci, certId.value.size(), keyHash) ||
        keyHash.tag != kTagOctetString) {
        return fail(QStringLiteral("CertID 里没有 issuerKeyHash"));
    }
    info->issuerKeyHash = keyHash.value;

    DerNode serial;
    if (!readDerTlv(certId.value, ci, certId.value.size(), serial) ||
        serial.tag != kTagInteger) {
        return fail(QStringLiteral("CertID 里没有序列号"));
    }
    info->serialNumber = serial.value;

    if (error) {
        error->clear();
    }
    return true;
}

}  // namespace aipdf::pdf
