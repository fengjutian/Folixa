#include "doc/AnnotationModel.h"

namespace aipdf::doc {

AnnotationModel::AnnotationModel(QObject* parent) : QObject(parent) {}

void AnnotationModel::clear() {
    if (annotations_.isEmpty()) {
        return;
    }
    annotations_.clear();
    order_.clear();
    emit changed(-1);
}

quint64 AnnotationModel::allocateId() const {
    // 自增即可：删除批注后不复用 id，
    // 免得撤销栈里的旧命令在重做时指向一条已经被复用的新批注。
    return nextId_++;
}

bool AnnotationModel::add(const pdf::Annotation& annotation) {
    if (annotation.id == 0 || annotations_.contains(annotation.id)) {
        return false;
    }
    annotations_.insert(annotation.id, annotation);
    order_.append(annotation.id);
    emit changed(annotation.pageIndex);
    return true;
}

bool AnnotationModel::remove(quint64 id) {
    const auto it = annotations_.constFind(id);
    if (it == annotations_.constEnd()) {
        return false;
    }
    const int pageIndex = it.value().pageIndex;
    annotations_.remove(id);
    order_.removeAll(id);
    emit changed(pageIndex);
    return true;
}

bool AnnotationModel::update(const pdf::Annotation& annotation) {
    const auto it = annotations_.constFind(annotation.id);
    if (it == annotations_.constEnd()) {
        return false;
    }
    // 先取出旧页码：insert 之后迭代器就失效了。
    const int previousPage = it.value().pageIndex;
    annotations_.insert(annotation.id, annotation);
    // 页码可能被改过：两页都要通知。
    emit changed(previousPage);
    if (previousPage != annotation.pageIndex) {
        emit changed(annotation.pageIndex);
    }
    return true;
}

const pdf::Annotation* AnnotationModel::find(quint64 id) const {
    const auto it = annotations_.constFind(id);
    if (it == annotations_.constEnd()) {
        return nullptr;
    }
    return &it.value();
}

QVector<pdf::Annotation> AnnotationModel::forPage(int pageIndex) const {
    QVector<pdf::Annotation> result;
    for (quint64 id : order_) {
        const auto it = annotations_.constFind(id);
        if (it != annotations_.constEnd() && it.value().pageIndex == pageIndex) {
            result.append(it.value());
        }
    }
    return result;
}

QVector<pdf::Annotation> AnnotationModel::all() const {
    QVector<pdf::Annotation> result;
    result.reserve(order_.size());
    for (quint64 id : order_) {
        const auto it = annotations_.constFind(id);
        if (it != annotations_.constEnd()) {
            result.append(it.value());
        }
    }
    return result;
}

int AnnotationModel::count() const {
    return static_cast<int>(annotations_.size());
}

int AnnotationModel::countForPage(int pageIndex) const {
    int total = 0;
    for (quint64 id : order_) {
        const auto it = annotations_.constFind(id);
        if (it != annotations_.constEnd() && it.value().pageIndex == pageIndex) {
            ++total;
        }
    }
    return total;
}

void AnnotationModel::remapPages(const QVector<int>& pageMapping) {
    if (pageMapping.isEmpty() || annotations_.isEmpty()) {
        return;
    }

    QVector<quint64> kept;
    kept.reserve(order_.size());
    for (quint64 id : order_) {
        const auto it = annotations_.constFind(id);
        if (it == annotations_.constEnd()) {
            continue;
        }
        pdf::Annotation annotation = it.value();
        const int oldPage = annotation.pageIndex;
        const int newPage = (oldPage >= 0 && oldPage < pageMapping.size())
                                ? pageMapping[oldPage]
                                : -1;
        if (newPage < 0) {
            // 页面被删了：这页上的批注没有意义，跟着删掉。
            annotations_.remove(id);
            continue;
        }
        if (newPage != oldPage) {
            annotation.pageIndex = newPage;
            annotations_.insert(id, annotation);
        }
        kept.append(id);
    }

    if (kept.size() != order_.size()) {
        order_ = kept;
    }
    // 页码变了，投影要整体重做：通知 -1 表示「全部重来」。
    emit changed(-1);
}

}  // namespace aipdf::doc
