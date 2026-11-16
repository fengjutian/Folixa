#include "pdf/SignatureInspector.h"

#include <QDateTime>
#include <QTimeZone>

#include <algorithm>
#include <cstring>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#endif

#ifdef AIPDF_WITH_QPDF
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFObjectHandle.hh>
#endif

namespace aipdf::pdf {

namespace {

// ------------------------------------------------------------------ DER ----
//
// 为什么自己写一个小解析器：
// 覆盖范围校验需要两样 Crypt32 不给的东西——
//   1) SignedAttributes 里的 messageDigest（被签内容的摘要）；
//   2) 签名时间（PKCS#7 里的 signingTime 优先于 PDF 的 /M）。
// Crypt32 的 CertOpenStore 只把证书摊平给你，看不到这两项。
// 所以这里只实现验签需要的那几层 TLV，不做通用 ASN.1。

constexpr quint8 kTagInteger = 0x02;
constexpr quint8 kTagOctetString = 0x04;
constexpr quint8 kTagOid = 0x06;
constexpr quint8 kTagUtcTime = 0x17;
constexpr quint8 kTagGeneralizedTime = 0x18;
constexpr quint8 kTagSequence = 0x30;
constexpr quint8 kTagSet = 0x31;
constexpr quint8 kTagContext0 = 0xa0;

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

// PDF 日期串（D:YYYYMMDDHHmmSSOHH'mm'）。
QString formatPdfDate(const QString& raw) {
    QString text = raw.trimmed();
    if (text.startsWith(QLatin1String("D:"))) {
        text = text.mid(2);
    }
    QString digits;
    int index = 0;
    while (index < text.size() && text.at(index).isDigit()) {
        digits.append(text.at(index));
        ++index;
    }
    if (digits.size() < 8) {
        return QString();
    }
    const QDate date(digits.mid(0, 4).toInt(), digits.mid(4, 2).toInt(), digits.mid(6, 2).toInt());
    const QTime time(digits.size() >= 10 ? digits.mid(8, 2).toInt() : 0,
                     digits.size() >= 12 ? digits.mid(10, 2).toInt() : 0,
                     digits.size() >= 14 ? digits.mid(12, 2).toInt() : 0);
    if (!date.isValid() || !time.isValid()) {
        return QString();
    }
    // 尾部时区：+HH'mm' / -HH'mm' / Z。没有偏移量时按 PDF 规范当本地时间。
    const QString zone = text.mid(index);
    int offsetSeconds = 0;
    bool hasOffset = false;
    if (!zone.isEmpty() && (zone.at(0) == QLatin1Char('+') || zone.at(0) == QLatin1Char('-'))) {
        QString zoneDigits;
        for (int i = 1; i < zone.size(); ++i) {
            const QChar ch = zone.at(i);
            if (ch.isDigit()) {
                zoneDigits.append(ch);
            } else if (ch != QLatin1Char('\'')) {
                break;
            }
        }
        if (zoneDigits.size() >= 2) {
            const int offsetHours = zoneDigits.mid(0, 2).toInt();
            const int offsetMinutes = zoneDigits.size() >= 4 ? zoneDigits.mid(2, 2).toInt() : 0;
            offsetSeconds = (offsetHours * 3600 + offsetMinutes * 60) *
                            (zone.at(0) == QLatin1Char('+') ? 1 : -1);
            hasOffset = true;
        }
    }
    const QDateTime dateTime = hasOffset
                                   ? QDateTime(date, time,
                                               QTimeZone::fromSecondsAheadOfUtc(offsetSeconds))
                                   : QDateTime(date, time);
    if (!dateTime.isValid()) {
        return QString();
    }
    return dateTime.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

// --------------------------------------------------------------- PKCS#7 ----

struct Pkcs7Signer {
    QString digestAlgorithmOid;   // 例如 2.16.840.1.101.3.4.2.1（SHA-256）
    QByteArray messageDigest;     // SignedAttributes 里的 messageDigest
    QString signingTime;          // SignedAttributes 里的 signingTime
    QByteArray issuerDer;         // SignerInfo 的 issuer（Name 的完整 TLV）
    QByteArray serialNumber;      // SignerInfo 的 serialNumber（整数字节）
    bool hasSignedAttributes = false;
};

// 解析 SignedAttributes 里的两个属性：messageDigest 与 signingTime。
void parseSignedAttributes(const QByteArray& attributes, Pkcs7Signer& signer) {
    int pos = 0;
    while (pos < attributes.size()) {
        DerNode attribute;
        if (!readDerTlv(attributes, pos, attributes.size(), attribute) || attribute.tag != kTagSequence) {
            return;
        }
        int inner = 0;
        DerNode type;
        if (!readDerTlv(attribute.value, inner, attribute.value.size(), type) || type.tag != kTagOid) {
            continue;
        }
        const QString oid = decodeDerOid(type.value);
        DerNode values;
        if (!readDerTlv(attribute.value, inner, attribute.value.size(), values) || values.tag != kTagSet) {
            continue;
        }
        int valuePos = 0;
        DerNode single;
        if (!readDerTlv(values.value, valuePos, values.value.size(), single)) {
            continue;
        }
        if (oid == QLatin1String("1.2.840.113549.1.9.4") && single.tag == kTagOctetString) {
            signer.messageDigest = single.value;
        } else if (oid == QLatin1String("1.2.840.113549.1.9.5")) {
            signer.signingTime = formatDerTime(single.tag, single.value);
        }
    }
}

// 解析一份 PKCS#7（SignedData），只取第一个签名者。
bool parsePkcs7(const QByteArray& der, Pkcs7Signer& signer) {
    int pos = 0;
    DerNode top;
    if (!readDerTlv(der, pos, der.size(), top) || top.tag != kTagSequence) {
        return false;
    }
    int body = 0;
    DerNode contentType;
    if (!readDerTlv(top.value, body, top.value.size(), contentType) || contentType.tag != kTagOid) {
        return false;
    }
    // 1.2.840.113549.1.7.2 = signedData
    if (decodeDerOid(contentType.value) != QLatin1String("1.2.840.113549.1.7.2")) {
        return false;
    }
    DerNode explicitContent;
    if (!readDerTlv(top.value, body, top.value.size(), explicitContent) ||
        explicitContent.tag != kTagContext0) {
        return false;
    }
    int signedPos = 0;
    DerNode signedData;
    if (!readDerTlv(explicitContent.value, signedPos, explicitContent.value.size(), signedData) ||
        signedData.tag != kTagSequence) {
        return false;
    }

    // SignedData: version, digestAlgorithms, contentInfo, [0] certs, [1] crls, signerInfos
    int dataPos = 0;
    const int dataEnd = signedData.value.size();
    DerNode cursor;
    for (int i = 0; i < 3; ++i) {
        if (!readDerTlv(signedData.value, dataPos, dataEnd, cursor)) {
            return false;
        }
    }
    DerNode signerInfos;
    while (dataPos < dataEnd) {
        if (!readDerTlv(signedData.value, dataPos, dataEnd, cursor)) {
            return false;
        }
        if (cursor.tag == kTagSet) {
            signerInfos = cursor;
            break;
        }
    }
    if (signerInfos.tag != kTagSet) {
        return false;
    }

    int signerPos = 0;
    DerNode signerInfo;
    if (!readDerTlv(signerInfos.value, signerPos, signerInfos.value.size(), signerInfo) ||
        signerInfo.tag != kTagSequence) {
        return false;
    }
    int fieldPos = 0;
    const int fieldEnd = signerInfo.value.size();
    DerNode field;
    // version
    if (!readDerTlv(signerInfo.value, fieldPos, fieldEnd, field)) {
        return false;
    }
    // issuerAndSerialNumber
    if (!readDerTlv(signerInfo.value, fieldPos, fieldEnd, field)) {
        return false;
    }
    if (field.tag == kTagSequence) {
        int namePos = 0;
        DerNode issuer;
        if (!readDerTlv(field.value, namePos, field.value.size(), issuer)) {
            return false;
        }
        DerNode serial;
        if (!readDerTlv(field.value, namePos, field.value.size(), serial)) {
            return false;
        }
        const int issuerLength = issuer.valueStart + issuer.value.size() - issuer.start;
        signer.issuerDer = field.value.mid(issuer.start, issuerLength);
        signer.serialNumber = serial.value;
    }
    // digestAlgorithm
    if (!readDerTlv(signerInfo.value, fieldPos, fieldEnd, field)) {
        return false;
    }
    if (field.tag == kTagSequence) {
        int algPos = 0;
        DerNode algOid;
        if (readDerTlv(field.value, algPos, field.value.size(), algOid) && algOid.tag == kTagOid) {
            signer.digestAlgorithmOid = decodeDerOid(algOid.value);
        }
    }
    // signedAttrs（可选，tag 是 [0] IMPLICIT）
    if (!readDerTlv(signerInfo.value, fieldPos, fieldEnd, field)) {
        return false;
    }
    if (field.tag == kTagContext0) {
        signer.hasSignedAttributes = true;
        parseSignedAttributes(field.value, signer);
    }
    return true;
}

// /Contents 的十六进制串可能比真正的 DER 长：签名器会预留固定长度，
// 后面用 0x00 补齐（Adobe 一族与 pyHanko 都这么干）。
//
// 但不能一刀切地砍尾巴：DER 自己的最后一个字节也可能是 0x00
// （PKCS#1 v1.5 的填充结尾就是 0x00）。所以按顶层 TLV 声明的长度裁，
// 裁完再确认剩下的确实全是 0x00。
QByteArray trimSignaturePadding(const QByteArray& contents) {
    int pos = 0;
    DerNode top;
    if (!readDerTlv(contents, pos, contents.size(), top) || top.tag != kTagSequence) {
        return contents;
    }
    const int declared = top.valueStart + top.value.size() - top.start;
    if (declared <= 0 || declared >= contents.size()) {
        return contents;
    }
    for (int i = declared; i < contents.size(); ++i) {
        if (contents.at(i) != '\0') {
            return contents;  // 后面不是填充，别动
        }
    }
    return contents.left(declared);
}

#ifdef Q_OS_WIN

// ---------------------------------------------------------------- 摘要 ----

// 用 BCrypt 算摘要。选 BCrypt 而不是 CryptHashCertificate2：
// 后者在部分 MinGW 版本上没有声明，而且只能对证书算，算不了文件字节。
QByteArray hashWithBcrypt(const QByteArray& data, const wchar_t* algorithm, bool* ok) {
    *ok = false;
    BCRYPT_ALG_HANDLE algorithmHandle = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithmHandle, algorithm, nullptr, 0) != 0) {
        return QByteArray();
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
        return QByteArray();
    }
    QByteArray objectBuffer(static_cast<int>(objectLength), '\0');
    BCRYPT_HASH_HANDLE hashHandle = nullptr;
    if (BCryptCreateHash(algorithmHandle, &hashHandle,
                         reinterpret_cast<PUCHAR>(objectBuffer.data()), objectLength,
                         nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(algorithmHandle, 0);
        return QByteArray();
    }
    bool success = true;
    if (!data.isEmpty()) {
        success = BCryptHashData(hashHandle, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                                 static_cast<ULONG>(data.size()), 0) == 0;
    }
    QByteArray digest;
    if (success) {
        digest = QByteArray(static_cast<int>(hashLength), '\0');
        success = BCryptFinishHash(hashHandle, reinterpret_cast<PUCHAR>(digest.data()),
                                   hashLength, 0) == 0;
    }
    BCryptDestroyHash(hashHandle);
    BCryptCloseAlgorithmProvider(algorithmHandle, 0);
    if (!success) {
        return QByteArray();
    }
    *ok = true;
    return digest;
}

// ------------------------------------------------------------ 证书链 ----

QByteArray trimLeadingZeros(const QByteArray& value) {
    int index = 0;
    while (index < value.size() && value.at(index) == '\0') {
        ++index;
    }
    return value.mid(index);
}

QString nameBlobToString(const CERT_NAME_BLOB* blob) {
    // 先问需要多长，再分配。csz 是按值传的（不是指针）。
    // 用 CERT_X500_NAME_STR 而不是 CERT_X509_NAME_STR：后者只有新 SDK 才有，
    // 且数值不同（4 = SIMPLE_DISPLAY），MinGW 上没有。
    // CertNameToStrW 的第二个参数是非 const 指针，这里只能 const_cast 一下。
    PCERT_NAME_BLOB mutableBlob = const_cast<PCERT_NAME_BLOB>(blob);
    const DWORD needed = CertNameToStrW(X509_ASN_ENCODING, mutableBlob, CERT_X500_NAME_STR, nullptr, 0);
    if (needed == 0) {
        return QString();
    }
    QByteArray buffer(static_cast<int>(needed + 1) * static_cast<int>(sizeof(wchar_t)), '\0');
    wchar_t* text = reinterpret_cast<wchar_t*>(buffer.data());
    const DWORD written = CertNameToStrW(X509_ASN_ENCODING, mutableBlob, CERT_X500_NAME_STR, text, needed);
    return QString::fromWCharArray(text, static_cast<int>(written));
}

// 从 Name 里取 CN（OID 2.5.4.3）。
//
// 不用「从 CertNameToStr 的输出里正则抠 CN=」：主体名里一旦出现转义逗号，
// 字符串切分就会认错字段。走 DER 才是准的。
QString commonNameFromNameBlob(const CERT_NAME_BLOB* blob) {
    if (!blob || blob->cbData == 0 || !blob->pbData) {
        return QString();
    }
    const QByteArray der(reinterpret_cast<const char*>(blob->pbData),
                          static_cast<int>(blob->cbData));
    int pos = 0;
    DerNode name;
    if (!readDerTlv(der, pos, der.size(), name) || name.tag != kTagSequence) {
        return QString();
    }
    int rdnPos = 0;
    while (rdnPos < name.value.size()) {
        DerNode rdn;
        if (!readDerTlv(name.value, rdnPos, name.value.size(), rdn) || rdn.tag != kTagSet) {
            return QString();
        }
        int attrPos = 0;
        while (attrPos < rdn.value.size()) {
            DerNode attribute;
            if (!readDerTlv(rdn.value, attrPos, rdn.value.size(), attribute) ||
                attribute.tag != kTagSequence) {
                break;
            }
            int pairPos = 0;
            DerNode type;
            if (!readDerTlv(attribute.value, pairPos, attribute.value.size(), type) ||
                type.tag != kTagOid) {
                break;
            }
            DerNode value;
            if (!readDerTlv(attribute.value, pairPos, attribute.value.size(), value)) {
                break;
            }
            if (decodeDerOid(type.value) != QLatin1String("2.5.4.3")) {
                continue;
            }
            switch (value.tag) {
            case 0x0c:  // UTF8String
                return QString::fromUtf8(value.value);
            case 0x13:  // PrintableString
            case 0x16:  // IA5String
                return QString::fromLatin1(value.value);
            case 0x1e:  // BMPString
                return QString::fromUtf16(reinterpret_cast<const char16_t*>(value.value.constData()),
                                          value.value.size() / 2);
            default:
                return QString();
            }
        }
    }
    return QString();
}

QString commonNameOf(PCCERT_CONTEXT context) {
    const DWORD needed =
        CertGetNameStringW(context, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, nullptr, 0);
    if (needed == 0) {
        return QString();
    }
    QByteArray buffer(static_cast<int>(needed + 1) * static_cast<int>(sizeof(wchar_t)), '\0');
    wchar_t* text = reinterpret_cast<wchar_t*>(buffer.data());
    const DWORD written =
        CertGetNameStringW(context, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, text, needed);
    return QString::fromWCharArray(text, static_cast<int>(written));
}

// Crypt32 的 CERT_INFO.SerialNumber 是**小端**存的（历史遗留），
// 与 DER 里的字节序正好相反。展示与比对都要先翻回来。
QByteArray reverseBytes(const QByteArray& value) {
    QByteArray result = value;
    std::reverse(result.begin(), result.end());
    return result;
}

QString serialToHex(const CERT_INFO* info) {
    const QByteArray raw(reinterpret_cast<const char*>(info->SerialNumber.pbData),
                         static_cast<int>(info->SerialNumber.cbData));
    return trimLeadingZeros(reverseBytes(raw)).toHex().toUpper();
}

QString fileTimeToString(const FILETIME& value) {
    ULARGE_INTEGER ticks;
    ticks.LowPart = value.dwLowDateTime;
    ticks.HighPart = value.dwHighDateTime;
    // FILETIME 从 1601-01-01 起算 100ns；换成 Unix 毫秒。
    const qint64 milliseconds = static_cast<qint64>(ticks.QuadPart / 10000ULL) - 11644473600000LL;
    const QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(milliseconds);
    if (!dateTime.isValid()) {
        return QString();
    }
    return dateTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString thumbprintOf(PCCERT_CONTEXT context) {
    bool ok = false;
    const QByteArray digest =
        hashWithBcrypt(QByteArray(reinterpret_cast<const char*>(context->pbCertEncoded),
                                  static_cast<int>(context->cbCertEncoded)),
                       BCRYPT_SHA1_ALGORITHM, &ok);
    if (!ok) {
        return QString();
    }
    return digest.toHex().toUpper();
}

SignerCertificate describeCertificate(PCCERT_CONTEXT context, bool trusted) {
    SignerCertificate result;
    const CERT_INFO* info = context->pCertInfo;
    result.subject = nameBlobToString(&info->Subject);
    result.issuer = nameBlobToString(&info->Issuer);
    result.commonName = commonNameOf(context);
    result.issuerCommonName = commonNameFromNameBlob(&info->Issuer);
    result.serialNumber = serialToHex(info);
    result.thumbprintSha1 = thumbprintOf(context);
    result.notBefore = fileTimeToString(info->NotBefore);
    result.notAfter = fileTimeToString(info->NotAfter);
    result.selfSigned = info->Subject.cbData == info->Issuer.cbData &&
                        std::memcmp(info->Subject.pbData, info->Issuer.pbData,
                                    info->Subject.cbData) == 0;
    result.trusted = trusted;
    return result;
}

// 证书是否就是 PKCS#7 里那个签名者：签发者 DER 与序列号都要对上。
bool matchesSigner(const CERT_INFO* info, const Pkcs7Signer& signer) {
    if (signer.issuerDer.isEmpty() || signer.serialNumber.isEmpty()) {
        return false;
    }
    const QByteArray issuer(reinterpret_cast<const char*>(info->Issuer.pbData),
                            static_cast<int>(info->Issuer.cbData));
    if (issuer.size() != signer.issuerDer.size() ||
        std::memcmp(issuer.constData(), signer.issuerDer.constData(), issuer.size()) != 0) {
        return false;
    }
    // 序列号：Crypt32 里是小端存的，要翻回 DER 的字节序再比。
    const QByteArray serial(reinterpret_cast<const char*>(info->SerialNumber.pbData),
                             static_cast<int>(info->SerialNumber.cbData));
    return trimLeadingZeros(reverseBytes(serial)) == trimLeadingZeros(signer.serialNumber);
}

// 按「上一张的签发者 == 下一张的主体」把 PKCS#7 里的证书串成一条链。
//
// 为什么自己排而不直接用 CertGetCertificateChain 返回的链元素：
// MinGW 的 wincrypt.h 把 CERT_SIMPLE_CHAIN::rgpElement 声明成了
// PCERT_CHAIN_ELEMENT*，而 Windows 实际给的是 PCERT_CONTEXT**——
// 照着声明去解会读到错位的内存。排序规则本身也不复杂，自己排更稳。
QVector<PCCERT_CONTEXT> orderCertificates(const QVector<PCCERT_CONTEXT>& certificates,
                                           PCCERT_CONTEXT leaf) {
    QVector<PCCERT_CONTEXT> ordered;
    QVector<bool> used(certificates.size(), false);
    for (int i = 0; i < certificates.size(); ++i) {
        if (certificates.at(i) == leaf) {
            used[i] = true;
            ordered.append(certificates.at(i));
            break;
        }
    }
    bool progressed = true;
    while (progressed && !ordered.isEmpty()) {
        progressed = false;
        const CERT_INFO* previous = ordered.last()->pCertInfo;
        for (int i = 0; i < certificates.size(); ++i) {
            if (used.at(i)) {
                continue;
            }
            const CERT_INFO* candidate = certificates.at(i)->pCertInfo;
            if (candidate->Subject.cbData == previous->Issuer.cbData &&
                std::memcmp(candidate->Subject.pbData, previous->Issuer.pbData,
                            candidate->Subject.cbData) == 0) {
                used[i] = true;
                ordered.append(certificates.at(i));
                progressed = true;
                break;
            }
        }
    }
    // 没接上的（缺中间证书、交叉签名等）按原顺序补在后面，别丢证书。
    for (int i = 0; i < certificates.size(); ++i) {
        if (!used.at(i)) {
            ordered.append(certificates.at(i));
        }
    }
    return ordered;
}

// 链上下文的类型在 MinGW 与 MS SDK 上不一致（一个带 const 一个不带），
// 这里统一成按编译器分支，调用点用 reinterpret_cast 对齐。
#if defined(__MINGW32__)
using ChainContextHandle = const CERT_CHAIN_CONTEXT*;
#else
using ChainContextHandle = CERT_CHAIN_CONTEXT*;
#endif

// 从 PKCS#7 里取出证书链：叶子在前，后面是中间证书与根证书。
//
// 叶子不靠「第一张就是叶子」这种假设，而是按签发者+序列号去认 SignerInfo；
// 认不出时退回到「PKCS#7 里带的全部证书」，此时不报信任。
QVector<SignerCertificate> buildChain(const QByteArray& pkcs7, const Pkcs7Signer& signer,
                                      bool* trusted) {
    QVector<SignerCertificate> result;
    *trusted = false;

    // CERT_STORE_PROV_PKCS7 这个 provider 不会自己去读文件：
    // PKCS#7 的字节必须通过 pvPara 传进去（传 nullptr 会在 crypt32 内部空指针崩溃）。
    CRYPT_DATA_BLOB blob;
    blob.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(pkcs7.constData()));
    blob.cbData = static_cast<DWORD>(pkcs7.size());
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_PKCS7,
                                     X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, &blob);
    if (!store) {
        return result;
    }
    // 枚举会把上一个句柄释放掉，所以每张都复制一份留着。
    QVector<PCCERT_CONTEXT> collected;
    PCCERT_CONTEXT previous = nullptr;
    PCCERT_CONTEXT found = nullptr;
    while ((found = CertFindCertificateInStore(store, X509_ASN_ENCODING, 0, CERT_FIND_ANY,
                                               nullptr, previous)) != nullptr) {
        collected.append(CertDuplicateCertificateContext(found));
        previous = found;
    }
    if (collected.isEmpty()) {
        CertCloseStore(store, 0);
        return result;
    }

    PCCERT_CONTEXT leaf = nullptr;
    for (PCCERT_CONTEXT context : collected) {
        if (matchesSigner(context->pCertInfo, signer)) {
            leaf = context;
            break;
        }
    }

    if (leaf) {
        // 让系统链引擎判断这张证书能不能被信任（额外的 store 里是 PKCS#7 自带的证书）。
        ChainContextHandle chainContext = nullptr;
        if (CertGetCertificateChain(nullptr, leaf, nullptr, store, nullptr, 0, nullptr,
                                    reinterpret_cast<ChainContextHandle*>(&chainContext)) &&
            chainContext) {
            CERT_CHAIN_POLICY_STATUS status;
            status.dwError = 0;
            if (CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_BASE, chainContext, nullptr,
                                                 &status)) {
                // 系统里没装根证书时这里会失败——那不是错误，只是「不受信任」。
                *trusted = status.dwError == 0;
            }
            CertFreeCertificateChain(chainContext);
        }
    }

    const QVector<PCCERT_CONTEXT> ordered = orderCertificates(collected, leaf);
    for (PCCERT_CONTEXT context : ordered) {
        result.append(describeCertificate(context, *trusted));
    }
    for (PCCERT_CONTEXT context : collected) {
        CertFreeCertificateContext(context);
    }
    CertCloseStore(store, 0);
    return result;
}

#endif  // Q_OS_WIN

// ------------------------------------------------------------ 覆盖检查 ----

// 空洞里装的应当就是 /Contents 的十六进制串（PDF 里允许补空白）。
bool holeMatchesContents(const QByteArray& hole, const QByteArray& contents) {
    int begin = 0;
    int end = hole.size();
    const auto isBlank = [](char ch) {
        return ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t' || ch == '\f' || ch == '\0';
    };
    while (begin < end && isBlank(hole.at(begin))) {
        ++begin;
    }
    while (end > begin && isBlank(hole.at(end - 1))) {
        --end;
    }
    if (end <= begin) {
        return false;
    }
    if (hole.at(begin) == '<' && hole.at(end - 1) == '>') {
        QByteArray digits;
        digits.reserve(end - begin);
        for (int i = begin + 1; i < end - 1; ++i) {
            const char ch = hole.at(i);
            if (!isBlank(ch)) {
                digits.append(ch);
            }
        }
        return digits.toLower() == contents.toHex().toLower();
    }
    // 字面量字符串：直接在空洞里找 DER 字节。
    return hole.contains(contents);
}

#ifdef Q_OS_WIN

// 覆盖范围检查。
//
// 三件事，缺一不可：
//   1) /ByteRange 必须首尾相接地铺满整份文件，只留一个空洞；
//   2) 空洞里装的必须就是 /Contents 的十六进制串；
//   3) 用 PKCS#7 里的摘要算法对覆盖到的字节重算摘要，必须等于签名里的 messageDigest。
//
// 第 3 条才是抓「文件被改过」的那一条：同长度改一个字节不会挪动任何偏移，
// 前两条照样通过，只有摘要对不上才能发现。
void checkCoverage(const QByteArray& pdfBytes, SignatureInfo& info, const Pkcs7Signer& signer,
                   const QByteArray& rawContents) {
    const QVector<qint64>& range = info.byteRange;
    if (range.isEmpty() || range.size() % 2 != 0) {
        info.coverError = QStringLiteral("签名缺少有效的 /ByteRange，无法校验覆盖范围");
        return;
    }
    const qint64 fileSize = pdfBytes.size();
    qint64 expected = 0;
    int gapCount = 0;
    qint64 holeStart = 0;
    qint64 holeEnd = 0;
    for (int i = 0; i + 1 < range.size(); i += 2) {
        const qint64 offset = range.at(i);
        const qint64 length = range.at(i + 1);
        if (offset < 0 || length < 0 || offset > fileSize || length > fileSize - offset) {
            info.coverError = QStringLiteral("签名的 /ByteRange 超出了文件范围");
            return;
        }
        if (i == 0) {
            if (offset != 0) {
                info.coverError = QStringLiteral("签名的 /ByteRange 没有从文件开头开始");
                return;
            }
        } else if (offset > expected) {
            ++gapCount;
            holeStart = expected;
            holeEnd = offset;
        } else if (offset < expected) {
            info.coverError = QStringLiteral("签名的 /ByteRange 区间重叠");
            return;
        }
        expected = offset + length;
    }
    if (expected != fileSize) {
        info.coverError = QStringLiteral("签名的 /ByteRange 没有覆盖到文件末尾");
        return;
    }
    if (gapCount != 1) {
        info.coverError = QStringLiteral("签名的 /ByteRange 空洞数量异常");
        return;
    }

    const QByteArray hole = pdfBytes.mid(static_cast<int>(holeStart),
                                         static_cast<int>(holeEnd - holeStart));
    if (!holeMatchesContents(hole, rawContents)) {
        info.coverError = QStringLiteral("签名的 /ByteRange 空洞与 /Contents 内容不一致");
        return;
    }

    const wchar_t* algorithm = nullptr;
    if (signer.digestAlgorithmOid == QLatin1String("1.3.14.3.2.26")) {
        algorithm = BCRYPT_SHA1_ALGORITHM;
    } else if (signer.digestAlgorithmOid == QLatin1String("2.16.840.1.101.3.4.2.1")) {
        algorithm = BCRYPT_SHA256_ALGORITHM;
    } else if (signer.digestAlgorithmOid == QLatin1String("2.16.840.1.101.3.4.2.2")) {
        algorithm = BCRYPT_SHA384_ALGORITHM;
    } else if (signer.digestAlgorithmOid == QLatin1String("2.16.840.1.101.3.4.2.3")) {
        algorithm = BCRYPT_SHA512_ALGORITHM;
    }
    if (!algorithm) {
        info.coverError =
            QStringLiteral("暂不支持的签名摘要算法：%1").arg(signer.digestAlgorithmOid);
        return;
    }
    if (signer.messageDigest.isEmpty()) {
        info.coverError = QStringLiteral("签名里没有 messageDigest 属性，无法校验覆盖范围");
        return;
    }

    QByteArray covered;
    covered.reserve(static_cast<int>(expected - (holeEnd - holeStart)));
    for (int i = 0; i + 1 < range.size(); i += 2) {
        covered.append(pdfBytes.mid(static_cast<int>(range.at(i)), static_cast<int>(range.at(i + 1))));
    }
    bool hashed = false;
    const QByteArray digest = hashWithBcrypt(covered, algorithm, &hashed);
    if (!hashed) {
        info.coverError = QStringLiteral("计算签名摘要失败");
        return;
    }
    if (digest != signer.messageDigest) {
        info.coverError = QStringLiteral("签名覆盖范围与文件内容不一致，文件被改动过");
        return;
    }
    info.coversDocument = true;
}

#else  // !Q_OS_WIN

// 非 Windows 没有 BCrypt，覆盖范围校验做不了。
void checkCoverage(const QByteArray& pdfBytes, SignatureInfo& info, const Pkcs7Signer& signer,
                   const QByteArray& rawContents) {
    Q_UNUSED(pdfBytes);
    Q_UNUSED(signer);
    Q_UNUSED(rawContents);
    info.coverError = QStringLiteral("覆盖范围校验需要 Windows 证书 API");
}

#endif  // Q_OS_WIN

}  // namespace

bool signatureInspectionAvailable() {
#ifdef AIPDF_WITH_QPDF
    return true;
#else
    return false;
#endif
}

#ifdef AIPDF_WITH_QPDF

namespace {

// qpdf 名字对象：qpdf 的 getName() 带前导 '/'
QString nameValue(const QPDFObjectHandle& handle) {
    if (!handle.isName()) {
        return QString();
    }
    QString name = QString::fromStdString(handle.getName());
    if (name.startsWith(QLatin1Char('/'))) {
        name.remove(0, 1);
    }
    return name;
}

// qpdf 文本串：getUTF8Value 会处理 UTF-16BE 与 PDFDocEncoding。
QString textValue(const QPDFObjectHandle& handle) {
    if (!handle.isString()) {
        return QString();
    }
    return QString::fromStdString(handle.getUTF8Value());
}

struct SignatureField {
    QString name;
    QPDFObjectHandle value;  // 签名域的 /V
};

// 递归扫 AcroForm 的域树，找 FT=/Sig 且带 /V 的签名域。
void collectSignatureFields(const QPDFObjectHandle& field, const QString& parentName,
                            QVector<SignatureField>& out) {
    if (!field.isDictionary()) {
        return;
    }
    QString name = parentName;
    const QPDFObjectHandle title = field.getKey("/T");
    if (title.isString()) {
        const QString part = QString::fromStdString(title.getUTF8Value());
        name = parentName.isEmpty() ? part : parentName + QLatin1Char('.') + part;
    }
    const QPDFObjectHandle fieldType = field.getKey("/FT");
    const QPDFObjectHandle value = field.getKey("/V");
    if (fieldType.isNameAndEquals("/Sig") && value.isDictionary()) {
        SignatureField entry;
        entry.name = name;
        entry.value = value;
        out.append(entry);
        return;
    }
    const QPDFObjectHandle kids = field.getKey("/Kids");
    if (kids.isArray()) {
        const std::vector<QPDFObjectHandle> children = kids.getArrayAsVector();
        for (const QPDFObjectHandle& kid : children) {
            collectSignatureFields(kid, name, out);
        }
    }
}

}  // namespace

#endif  // AIPDF_WITH_QPDF

QVector<SignatureInfo> inspectSignatures(const QByteArray& pdfBytes, QString* error) {
    QVector<SignatureInfo> result;
    const auto fail = [&result, error](const QString& message) {
        if (error) {
            *error = message;
        }
        return result;
    };

    // 空数据与「不是 PDF」两种判断放在 qpdf 分支之前：
    // 这两条与 qpdf 无关，两种构建下都该给出同样的结论。
    if (pdfBytes.isEmpty()) {
        return fail(QStringLiteral("PDF 内容为空，无法解析签名"));
    }
    if (!pdfBytes.startsWith("%PDF")) {
        return fail(QStringLiteral("数据不是 PDF 文件，无法解析签名"));
    }

#ifndef AIPDF_WITH_QPDF
    // 没启用 qpdf 时不能返回「无签名」——那是误导，真实原因是缺依赖。
    return fail(QStringLiteral("本构建未启用 qpdf（可选依赖），无法解析 PDF 签名"));
#else

    QPDF document;
    try {
        document.setSuppressWarnings(true);
        // 用 const 版本的 processMemoryFile：qpdf 不会回头改我们的字节，
        // 后面算覆盖范围还要用原始字节去切片。
        document.processMemoryFile("memory", pdfBytes.constData(), static_cast<size_t>(pdfBytes.size()));
    } catch (const QPDFExc& exception) {
        return fail(QStringLiteral("PDF 解析失败：%1").arg(QString::fromStdString(exception.what())));
    } catch (const std::exception& exception) {
        return fail(QStringLiteral("PDF 解析失败：%1").arg(QString::fromStdString(exception.what())));
    }

    const QPDFObjectHandle acroForm = document.getRoot().getKey("/AcroForm");
    if (!acroForm.isDictionary()) {
        return fail(QStringLiteral("无签名：文档没有 AcroForm 表单"));
    }
    const QPDFObjectHandle fields = acroForm.getKey("/Fields");
    if (!fields.isArray()) {
        return fail(QStringLiteral("无签名：AcroForm 里没有域"));
    }

    QVector<SignatureField> signatureFields;
    const std::vector<QPDFObjectHandle> roots = fields.getArrayAsVector();
    for (const QPDFObjectHandle& root : roots) {
        collectSignatureFields(root, QString(), signatureFields);
    }
    if (signatureFields.isEmpty()) {
        return fail(QStringLiteral("无签名：文档里没有签名域"));
    }

    for (const SignatureField& entry : signatureFields) {
        SignatureInfo info;
        info.fieldName = entry.name;
        info.subFilter = nameValue(entry.value.getKey("/SubFilter"));
        info.reason = textValue(entry.value.getKey("/Reason"));
        info.signerName = textValue(entry.value.getKey("/Name"));
        info.signingTime = formatPdfDate(textValue(entry.value.getKey("/M")));

        const QPDFObjectHandle byteRange = entry.value.getKey("/ByteRange");
        if (byteRange.isArray()) {
            const std::vector<QPDFObjectHandle> numbers = byteRange.getArrayAsVector();
            for (const QPDFObjectHandle& number : numbers) {
                if (number.isInteger()) {
                    info.byteRange.append(static_cast<qint64>(number.getIntValue()));
                }
            }
        }

        // 原始 /Contents（含签名器预留的 0x00 填充）要留着：
        // 空洞比对要拿它跟文件里的十六进制串逐字节对。
        QByteArray rawContents;
        const QPDFObjectHandle contents = entry.value.getKey("/Contents");
        if (contents.isString()) {
            rawContents = QByteArray::fromStdString(contents.getStringValue());
            info.pkcs7 = trimSignaturePadding(rawContents);
        }

        if (info.pkcs7.isEmpty()) {
            info.coverError = QStringLiteral("签名域里没有 /Contents 数据");
            result.append(info);
            continue;
        }

        Pkcs7Signer signer;
        if (!parsePkcs7(info.pkcs7, signer)) {
            info.coverError = QStringLiteral("无法解析签名里的 PKCS#7 数据");
            result.append(info);
            continue;
        }
        // PKCS#7 里的签名时间比 PDF 的 /M 更权威（/M 由签名器随手填）。
        if (!signer.signingTime.isEmpty()) {
            info.signingTime = signer.signingTime;
        }
#ifdef Q_OS_WIN
        bool trusted = false;
        info.chain = buildChain(info.pkcs7, signer, &trusted);
#else
        info.chain.clear();
#endif
        checkCoverage(pdfBytes, info, signer, rawContents);
        result.append(info);
    }

    if (error) {
        error->clear();
    }
    return result;

#endif  // AIPDF_WITH_QPDF
}

}  // namespace aipdf::pdf
