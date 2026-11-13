#pragma once

#include <QString>

namespace aipdf::ai {

// Windows 凭据管理器（Credential Manager）。
//
// 密钥只放这里：它底层是 DPAPI，加密后的数据与当前用户的 Windows 登录会话
// 绑定，拿到硬盘镜像也解不开。
//
// 明确不做的事：密钥不写进普通配置文件、不进日志、不进崩溃报告、
// 不随 IPC 帧传回主程序。
//
// 对标：Cherry Studio、Continue.dev 这类桌面 AI 客户端都把 API Key 放在
// 系统凭据库里，而不是自己的配置文件里。
class CredentialStore {
public:
    // 存一个密钥。会覆盖同名旧值。
    static bool store(const QString& targetName,
                      const QString& secret,
                      QString* errorMessage = nullptr);
    // 读一个密钥。没有或读不出来时返回空字符串。
    static QString load(const QString& targetName);
    // 删一个密钥。没有也算成功。
    static bool remove(const QString& targetName, QString* errorMessage = nullptr);
    // 有没有存过（不把密钥读出来）。
    static bool exists(const QString& targetName);

    // 某个 Provider 的密钥在凭据管理器里的名字。
    //
    // 名字里带上服务商，不同服务商的密钥不会互相覆盖。
    static QString targetNameFor(const QString& providerId);

    // 当前系统是否支持（Windows 支持，其它平台返回 false）。
    static bool isSupported();
};

}  // namespace aipdf::ai
