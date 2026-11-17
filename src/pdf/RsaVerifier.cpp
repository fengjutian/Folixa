// RSA 公钥验签（PKCS#1 v1.5）的自实现版本。
//
// 为什么自己写大整数：见 RsaVerifier.h 的注释——
// BCrypt / Crypt32 的验签 API 在 MinGW 头文件里是旧参数表，
// 与系统 DLL 的导出对不上，按头文件调会静默错位。
// 这里只依赖 QtCore，任何平台都能编译，行为完全可预期。
//
// 算法：
//   1. m = s^e mod n（模幂，用平方-乘的二进制展开）
//   2. m 必须形如 00 01 FF..FF 00 || DigestInfo
//   3. DigestInfo 必须等于 DER(AlgorithmIdentifier(hashOid, NULL), OCTET STRING(digest))
//
// 性能说明：2048 位模数下模幂约 18 次模乘，
// 每次模乘是一次 64×64 limb 的乘法加一次逐位长除法（4096 轮 × 约 128 limb），
// 实测在毫秒级，对验签这种交互频率完全够用。

#include "pdf/RsaVerifier.h"

#include <QVector>

namespace aipdf::pdf {
namespace {

// 一个够用的大整数：32 位 limb，小端存放（limbs_[0] 是最低位）。
class BigUInt {
public:
    BigUInt() = default;

    // 大端字节 → 大整数。
    static BigUInt fromBytes(const QByteArray& bytes) {
        QVector<quint32> limbs;
        const int count = (bytes.size() + 3) / 4;
        limbs.resize(count);
        for (int i = 0; i < bytes.size(); ++i) {
            const int byteIndex = bytes.size() - 1 - i;  // 大端 → 小端
            limbs[i / 4] |= static_cast<quint32>(static_cast<quint8>(bytes.at(byteIndex)))
                            << ((i % 4) * 8);
        }
        BigUInt value;
        value.limbs_ = limbs;
        value.trim();
        return value;
    }

    // 大整数 → 定长大端字节（高位补 0）。
    QByteArray toBytes(int size) const {
        QByteArray out(size, '\0');
        for (int i = 0; i < size; ++i) {
            const int byteIndex = size - 1 - i;
            const quint32 limb = (i / 4) < limbs_.size() ? limbs_.at(i / 4) : 0u;
            out[byteIndex] = static_cast<char>((limb >> ((i % 4) * 8)) & 0xff);
        }
        return out;
    }

    bool isZero() const { return limbs_.isEmpty(); }

    // 小整数（模幂的初值 1 用它）。
    static BigUInt fromUInt(quint32 value) {
        BigUInt result;
        if (value != 0) {
            result.limbs_.append(value);
        }
        return result;
    }

    int bitLength() const {
        if (limbs_.isEmpty()) {
            return 0;
        }
        int bits = (limbs_.size() - 1) * 32;
        quint32 top = limbs_.last();
        while (top != 0) {
            ++bits;
            top >>= 1;
        }
        return bits;
    }

    bool bitAt(int index) const {
        const int limbIndex = index / 32;
        if (limbIndex >= limbs_.size()) {
            return false;
        }
        return ((limbs_.at(limbIndex) >> (index % 32)) & 1u) != 0u;
    }

    // a - b（要求 a >= b）。
    static BigUInt subtract(const BigUInt& a, const BigUInt& b) {
        BigUInt result;
        result.limbs_.resize(a.limbs_.size());
        quint64 borrow = 0;
        for (int i = 0; i < a.limbs_.size(); ++i) {
            const quint64 left = a.limbs_.at(i);
            const quint64 right = i < b.limbs_.size() ? b.limbs_.at(i) : 0u;
            const quint64 diff = left - right - borrow;
            result.limbs_[i] = static_cast<quint32>(diff & 0xffffffffULL);
            borrow = (left < right + borrow) ? 1u : 0u;
        }
        result.trim();
        return result;
    }

    // 余数左移一位再加一个比特。
    static BigUInt shiftLeftOneAddBit(const BigUInt& value, bool bit) {
        BigUInt result;
        result.limbs_.resize(value.limbs_.size() + 1);
        quint32 carry = bit ? 1u : 0u;
        for (int i = 0; i < value.limbs_.size(); ++i) {
            const quint32 current = value.limbs_.at(i);
            result.limbs_[i] = (current << 1) | carry;
            carry = (current >> 31) & 1u;
        }
        result.limbs_[value.limbs_.size()] = carry;
        result.trim();
        return result;
    }

    // a * b（不取模）。
    static BigUInt multiply(const BigUInt& a, const BigUInt& b) {
        BigUInt result;
        result.limbs_.assign(a.limbs_.size() + b.limbs_.size(), 0u);
        for (int i = 0; i < a.limbs_.size(); ++i) {
            quint64 carry = 0;
            const quint64 multiplier = a.limbs_.at(i);
            for (int j = 0; j < b.limbs_.size(); ++j) {
                const quint64 current = result.limbs_.at(i + j) + multiplier * b.limbs_.at(j) + carry;
                result.limbs_[i + j] = static_cast<quint32>(current & 0xffffffffULL);
                carry = current >> 32;
            }
            int k = i + b.limbs_.size();
            while (carry != 0 && k < static_cast<int>(result.limbs_.size())) {
                const quint64 current = result.limbs_.at(k) + carry;
                result.limbs_[k] = static_cast<quint32>(current & 0xffffffffULL);
                carry = current >> 32;
                ++k;
            }
        }
        result.trim();
        return result;
    }

    // value % modulus：逐位长除法。
    //
    // 为什么不用 Barrett/Montgomery：这里的位数只有 2048 bit，
    // 逐位长除法虽然朴素，但一次模乘才 4000 多轮、每轮几十次 limb 运算，
    // 整次验签不到百万次整数运算，毫秒级足够。
    static BigUInt modulo(const BigUInt& value, const BigUInt& modulus) {
        BigUInt remainder;
        const int bits = value.bitLength();
        for (int i = bits - 1; i >= 0; --i) {
            remainder = shiftLeftOneAddBit(remainder, value.bitAt(i));
            if (compare(remainder, modulus) >= 0) {
                remainder = subtract(remainder, modulus);
            }
        }
        return remainder;
    }

    static int compare(const BigUInt& a, const BigUInt& b) {
        if (a.limbs_.size() != b.limbs_.size()) {
            return a.limbs_.size() < b.limbs_.size() ? -1 : 1;
        }
        for (int i = a.limbs_.size() - 1; i >= 0; --i) {
            if (a.limbs_.at(i) != b.limbs_.at(i)) {
                return a.limbs_.at(i) < b.limbs_.at(i) ? -1 : 1;
            }
        }
        return 0;
    }

private:
    void trim() {
        while (!limbs_.isEmpty() && limbs_.last() == 0u) {
            limbs_.removeLast();
        }
    }

    QVector<quint32> limbs_;
};

// DER 长度域（短格式与长格式）。
QByteArray derLength(int length) {
    QByteArray out;
    if (length < 0x80) {
        out.append(static_cast<char>(length));
        return out;
    }
    QByteArray raw;
    int value = length;
    while (value > 0) {
        raw.prepend(static_cast<char>(value & 0xff));
        value >>= 8;
    }
    out.append(static_cast<char>(0x80 | raw.size()));
    out.append(raw);
    return out;
}

QByteArray derTlv(quint8 tag, const QByteArray& value) {
    QByteArray out;
    out.append(static_cast<char>(tag));
    out.append(derLength(value.size()));
    out.append(value);
    return out;
}

// OID 的内容字节（点分十进制 → DER）。
QByteArray encodeOidContent(const QString& dotted) {
    const QStringList parts = dotted.split(QLatin1Char('.'));
    QByteArray content;
    if (parts.size() >= 2) {
        const int first = parts.at(0).toInt();
        const int second = parts.at(1).toInt();
        content.append(static_cast<char>(40 * first + second));
        for (int i = 2; i < parts.size(); ++i) {
            int arc = parts.at(i).toInt();
            QByteArray arcBytes;
            arcBytes.append(static_cast<char>(arc & 0x7f));
            arc >>= 7;
            while (arc > 0) {
                arcBytes.prepend(static_cast<char>((arc & 0x7f) | 0x80));
                arc >>= 7;
            }
            content.append(arcBytes);
        }
    }
    return content;
}

// 拼出完整的 DigestInfo。
//
// DigestInfo ::= SEQUENCE { digestAlgorithm AlgorithmIdentifier, digest OCTET STRING }
// AlgorithmIdentifier ::= SEQUENCE { algorithm OBJECT IDENTIFIER, parameters NULL }
//
// 注意只有**一层** AlgorithmIdentifier：
// 早先写成 SEQUENCE{ SEQUENCE{ OID, NULL } } 是多包了一层，
// 字节数刚好一样（51 字节）但内容不同，比对必然失败——
// 这种错误不会崩、不会报长度错，只会安静地判「签名对不上」。
QByteArray buildDigestInfo(const QString& oid, const QByteArray& digest) {
    QByteArray algorithmIdentifier;
    algorithmIdentifier += derTlv(0x06, encodeOidContent(oid));
    algorithmIdentifier += derTlv(0x05, QByteArray());  // NULL 参数
    QByteArray body = derTlv(0x30, algorithmIdentifier);
    body += derTlv(0x04, digest);
    return derTlv(0x30, body);
}

}  // namespace

bool verifyRsaPkcs1Signature(const QByteArray& modulusBigEndian,
                              const QByteArray& exponentBigEndian,
                              const QByteArray& digest,
                              const QString& digestAlgorithmOid,
                              const QByteArray& signature,
                              QString* reason) {
    const auto fail = [reason](const QString& message) {
        if (reason) {
            *reason = message;
        }
        return false;
    };
    if (modulusBigEndian.isEmpty()) {
        return fail(QStringLiteral("证书里没有 RSA 模数，无法验签"));
    }
    if (exponentBigEndian.isEmpty()) {
        return fail(QStringLiteral("证书里没有 RSA 指数，无法验签"));
    }
    if (digest.isEmpty()) {
        return fail(QStringLiteral("摘要为空，无法验签"));
    }
    if (signature.isEmpty()) {
        return fail(QStringLiteral("签名值为空，无法验签"));
    }
    if (digestAlgorithmOid.isEmpty()) {
        return fail(QStringLiteral("不认识的摘要算法，无法验签"));
    }
    if (signature.size() != modulusBigEndian.size()) {
        return fail(QStringLiteral("签名值长度与证书公钥长度不一致，不是同一把密钥"));
    }

    const BigUInt modulus = BigUInt::fromBytes(modulusBigEndian);
    const BigUInt exponent = BigUInt::fromBytes(exponentBigEndian);

    // 模幂：m = s^e mod n（平方-乘）。
    BigUInt base = BigUInt::fromBytes(signature);
    if (BigUInt::compare(base, modulus) >= 0) {
        return fail(QStringLiteral("签名值不小于模数，不是合法的 RSA 签名"));
    }
    BigUInt result = BigUInt::fromUInt(1);
    const int exponentBits = exponent.bitLength();
    for (int i = exponentBits - 1; i >= 0; --i) {
        result = BigUInt::modulo(BigUInt::multiply(result, result), modulus);
        if (exponent.bitAt(i)) {
            result = BigUInt::modulo(BigUInt::multiply(result, base), modulus);
        }
    }

    const QByteArray block = result.toBytes(modulusBigEndian.size());
    // PKCS#1 v1.5：00 01 FF..FF 00 || DigestInfo
    if (block.size() < 11 || block.at(0) != '\0' || block.at(1) != '\1') {
        return fail(QStringLiteral("RSA 验签失败：签名值解出来的块结构不对"));
    }
    int separator = -1;
    for (int i = 2; i < block.size(); ++i) {
        if (block.at(i) == '\0') {
            separator = i;
            break;
        }
        if (block.at(i) != '\xff') {
            return fail(QStringLiteral("RSA 验签失败：填充不是 PKCS#1 v1.5"));
        }
    }
    if (separator < 0) {
        return fail(QStringLiteral("RSA 验签失败：填充里找不到分隔符"));
    }

    const QByteArray expected = buildDigestInfo(digestAlgorithmOid, digest);
    const QByteArray actual = block.mid(separator + 1);
    if (actual != expected) {
        return fail(QStringLiteral("RSA 验签失败：签名值与被签内容的摘要对不上"));
    }
    if (reason) {
        reason->clear();
    }
    return true;
}

}  // namespace aipdf::pdf
