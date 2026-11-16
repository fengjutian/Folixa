#include "app/ShareUtils.h"

#include <QFileInfo>
#include <QUrlQuery>

namespace aipdf::app::share {

QString fileLink(const QString& filePath) {
    if (filePath.trimmed().isEmpty()) {
        return {};
    }
    return QUrl::fromLocalFile(QFileInfo(filePath).absoluteFilePath()).toString();
}

QUrl emailUrl(const QString& filePath, const QString& subjectPrefix,
              const QString& bodyPrefix) {
    const QFileInfo file(filePath);
    QUrl url(QStringLiteral("mailto:"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("subject"), subjectPrefix + file.fileName());
    query.addQueryItem(QStringLiteral("body"), bodyPrefix + fileLink(filePath));
    url.setQuery(query);
    return url;
}

}  // namespace aipdf::app::share
