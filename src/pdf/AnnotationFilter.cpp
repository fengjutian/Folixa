#include "pdf/AnnotationFilter.h"

namespace aipdf::pdf {

bool AnnotationFilter::isEmpty() const {
    // includeReplies 默认是 true：只有显式关掉它时才算一个条件。
    return types.isEmpty() && keyword.trimmed().isEmpty() && pageIndex < 0 && !onlyReplies &&
           !requireContents && includeReplies;
}

bool matchesAnnotationFilter(const Annotation& annotation, const AnnotationFilter& filter) {
    const bool isReply = annotation.replyToId != 0;
    if (filter.onlyReplies) {
        // 只看回复：不是回复的一律不要。
        if (!isReply) {
            return false;
        }
    } else if (isReply && !filter.includeReplies) {
        return false;
    }
    if (!filter.types.isEmpty() && !filter.types.contains(annotation.type)) {
        return false;
    }
    if (filter.pageIndex >= 0 && annotation.pageIndex != filter.pageIndex) {
        return false;
    }
    if (filter.requireContents && annotation.contents.trimmed().isEmpty()) {
        return false;
    }
    const QString keyword = filter.keyword.trimmed();
    if (!keyword.isEmpty()) {
        const bool inContents = annotation.contents.contains(keyword, Qt::CaseInsensitive);
        const bool inAuthor = annotation.author.contains(keyword, Qt::CaseInsensitive);
        if (!inContents && !inAuthor) {
            return false;
        }
    }
    return true;
}

QVector<Annotation> filterAnnotations(const QVector<Annotation>& annotations,
                                       const AnnotationFilter& filter) {
    QVector<Annotation> result;
    if (filter.isEmpty()) {
        return annotations;
    }
    result.reserve(annotations.size());
    for (const Annotation& annotation : annotations) {
        if (matchesAnnotationFilter(annotation, filter)) {
            result.append(annotation);
        }
    }
    return result;
}

}  // namespace aipdf::pdf
