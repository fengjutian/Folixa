#pragma once

#include <QString>
#include <QUrl>

namespace aipdf::app::share {

// 生成可交给系统默认邮件客户端的 mailto URL。附件参数没有跨客户端标准，
// 因而正文携带 file URL；调用方同时把本地路径放进剪贴板供添加附件。
QUrl emailUrl(const QString& filePath, const QString& subjectPrefix,
              const QString& bodyPrefix);

// 返回规范化、百分号编码后的本地 file URL；文件不必已经存在。
QString fileLink(const QString& filePath);

}  // namespace aipdf::app::share
