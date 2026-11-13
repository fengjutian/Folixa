#include "ai/CredentialStore.h"

#include <QString>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif

namespace aipdf::ai {

namespace {

#ifdef Q_OS_WIN
// 凭据管理器只吃宽字符与裸字节。
std::wstring toWide(const QString& text) {
    return text.toStdWString();
}
#endif

}  // namespace

bool CredentialStore::isSupported() {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

QString CredentialStore::targetNameFor(const QString& providerId) {
    // 名字里带上服务商：不同服务商的密钥各存各的。
    // 名字本身不含任何秘密。
    return QStringLiteral("Folixa/ai/%1/api-key").arg(providerId);
}

bool CredentialStore::store(const QString& targetName,
                             const QString& secret,
                             QString* errorMessage) {
#ifdef Q_OS_WIN
    if (targetName.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("凭据名称为空");
        }
        return false;
    }

    // 凭据管理器存的是字节串。这里用 UTF-16 存，
    // 大小要按字节算，并且带上结尾的 0。
    //
    // 两个宽字符串都必须是有名字的局部变量：
    // 写成 const_cast<LPWSTR>(toWide(x).c_str()) 的话，临时对象在
    // 这一行结束就没了，CredWriteW 拿到的是野指针。
    const std::wstring wideTarget = toWide(targetName);
    const std::wstring wideSecret = secret.toStdWString();
    const DWORD blobSize =
        static_cast<DWORD>((wideSecret.size() + 1) * sizeof(wchar_t));

    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(wideTarget.c_str());
    credential.CredentialBlobSize = blobSize;
    credential.CredentialBlob =
        reinterpret_cast<LPBYTE>(const_cast<wchar_t*>(wideSecret.c_str()));
    // 只留在本机：不上漫游、不进域。
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(L"folixa");

    if (!CredWriteW(&credential, 0)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("写入 Windows 凭据管理器失败（错误码 %1）")
                               .arg(static_cast<quint32>(GetLastError()));
        }
        return false;
    }
    return true;
#else
    Q_UNUSED(targetName);
    Q_UNUSED(secret);
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("当前系统不支持 Windows 凭据管理器");
    }
    return false;
#endif
}

QString CredentialStore::load(const QString& targetName) {
#ifdef Q_OS_WIN
    if (targetName.isEmpty()) {
        return QString();
    }
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(toWide(targetName).c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
        return QString();
    }
    if (credential == nullptr) {
        return QString();
    }
    const int characters =
        static_cast<int>(credential->CredentialBlobSize / sizeof(wchar_t));
    QString secret;
    if (characters > 0) {
        const auto* text = reinterpret_cast<const wchar_t*>(credential->CredentialBlob);
        secret = QString::fromWCharArray(text, characters - 1);
    }
    CredFree(credential);
    return secret;
#else
    Q_UNUSED(targetName);
    return QString();
#endif
}

bool CredentialStore::remove(const QString& targetName, QString* errorMessage) {
#ifdef Q_OS_WIN
    if (targetName.isEmpty()) {
        return true;
    }
    if (CredDeleteW(toWide(targetName).c_str(), CRED_TYPE_GENERIC, 0)) {
        return true;
    }
    const DWORD code = GetLastError();
    // 本来就没有：也算删成功了。
    if (code == ERROR_NOT_FOUND) {
        return true;
    }
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("删除 Windows 凭据失败（错误码 %1）")
                           .arg(static_cast<quint32>(code));
    }
    return false;
#else
    Q_UNUSED(targetName);
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("当前系统不支持 Windows 凭据管理器");
    }
    return false;
#endif
}

bool CredentialStore::exists(const QString& targetName) {
#ifdef Q_OS_WIN
    if (targetName.isEmpty()) {
        return false;
    }
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(toWide(targetName).c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
        return false;
    }
    if (credential != nullptr) {
        CredFree(credential);
    }
    return true;
#else
    Q_UNUSED(targetName);
    return false;
#endif
}

}  // namespace aipdf::ai
