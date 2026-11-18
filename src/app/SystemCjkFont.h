#pragma once

#include "doc/UniversalConverter.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace aipdf::app {

// 在本机找一份能显示中文的 TrueType 字体。
//
// 为什么找得到就自动用：标准 14 字体没有中文字形，中文会变成问号——
// 那是**静默丢字**：文件看着排好了，内容却没了。仓库里没随附中文字体
// （tools/ 与 assets/ 下都没有），只能用系统里的那份。
//
// 为什么只认 .ttf 不认 .ttc：doc::UniversalConverter 的字体解析只支持单字体的
// TrueType 文件，ttc 集合会被当成无效字体拒掉。
//
// 为什么放在头文件里（inline）：这是一段到处都要用的小逻辑
// （剪贴板、网页、任意文件 → PDF 三条路都要），单独开一个 .cpp
// 只会让构建文件多一处登记；函数本身也没有状态。
//
// 环境变量 AIPDF_CJK_FONT 的用法和 pdf/PdfiumAdapter 里的一致：
// 指定一个路径就优先用它。
inline bool loadSystemCjkFont(doc::FontResource* resource, QString* usedPath) {
    if (resource == nullptr) {
        return false;
    }
    QStringList candidates;
    const QString configured = qEnvironmentVariable("AIPDF_CJK_FONT");
    if (!configured.isEmpty()) {
        candidates.append(configured);
    }
    candidates.append({
        QStringLiteral("C:/Windows/Fonts/simhei.ttf"),
        QStringLiteral("C:/Windows/Fonts/Deng.ttf"),
        QStringLiteral("C:/Windows/Fonts/simsunb.ttf"),
        QStringLiteral("C:/Windows/Fonts/STSONG.TTF"),
    });
    for (const QString& path : candidates) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QByteArray bytes = file.readAll();
        file.close();
        if (bytes.isEmpty()) {
            continue;
        }
        resource->bytes = bytes;
        resource->postScriptName = QFileInfo(path).completeBaseName();
        if (usedPath != nullptr) {
            *usedPath = path;
        }
        return true;
    }
    return false;
}

}  // namespace aipdf::app
