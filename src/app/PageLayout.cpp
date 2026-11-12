#include "app/PageLayout.h"

#include <QtGlobal>

namespace aipdf::app {

void PageLayout::setPages(const QVector<PageInfo>& pages) {
    pages_ = pages;
    recomputeTops();
}

void PageLayout::setScale(double zoom, double devicePixelRatio) {
    zoom_ = (zoom > 0.0) ? zoom : 1.0;
    devicePixelRatio_ = (devicePixelRatio > 0.0) ? devicePixelRatio : 1.0;
    recomputeTops();
}

void PageLayout::setPageSpacing(double spacing) {
    spacing_ = (spacing >= 0.0) ? spacing : 0.0;
    recomputeTops();
}

void PageLayout::setMargins(double left, double right) {
    marginLeft_ = (left >= 0.0) ? left : 0.0;
    marginRight_ = (right >= 0.0) ? right : 0.0;
    recomputeTops();
}

void PageLayout::clear() {
    pages_.clear();
    tops_.clear();
    totalHeight_ = 0.0;
}

int PageLayout::pageCount() const {
    return static_cast<int>(pages_.size());
}

bool PageLayout::isEmpty() const {
    return pages_.isEmpty();
}

double PageLayout::zoom() const {
    return zoom_;
}

double PageLayout::devicePixelRatio() const {
    return devicePixelRatio_;
}

QSizeF PageLayout::pageSize(int index) const {
    if (index < 0 || index >= pages_.size()) {
        return {};
    }
    const PageInfo& info = pages_.at(index);
    // 页面尺寸已经包含 PDF 自带的旋转（适配层返回的是旋转后的宽高）。
    const double scale = zoom_ * devicePixelRatio_;
    return {info.widthPoints * scale, info.heightPoints * scale};
}

double PageLayout::pageTop(int index) const {
    if (index < 0 || index >= tops_.size()) {
        return 0.0;
    }
    return tops_.at(index);
}

double PageLayout::pageLeft(int index) const {
    Q_UNUSED(index);
    return marginLeft_;
}

double PageLayout::totalHeight() const {
    return totalHeight_;
}

int PageLayout::pageAt(double y) const {
    if (tops_.isEmpty()) {
        return -1;
    }
    if (y < 0.0) {
        return 0;
    }
    // tops_ 是递增的，二分查找最后一页起点不超过 y 的页。
    int low = 0;
    int high = static_cast<int>(tops_.size()) - 1;
    int result = 0;
    while (low <= high) {
        const int mid = (low + high) / 2;
        if (tops_.at(mid) <= y) {
            result = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return result;
}

double PageLayout::pageBottom(int index) const {
    if (index < 0 || index >= tops_.size()) {
        return 0.0;
    }
    if (index + 1 < tops_.size()) {
        return tops_.at(index + 1) - spacing_;
    }
    return totalHeight_;
}

void PageLayout::recomputeTops() {
    tops_.clear();
    tops_.reserve(pages_.size());
    totalHeight_ = 0.0;

    double y = 0.0;
    for (int i = 0; i < pages_.size(); ++i) {
        tops_.append(y);
        const QSizeF size = pageSize(i);
        y += size.height() + spacing_;
    }
    if (!pages_.isEmpty()) {
        totalHeight_ = qMax(0.0, y - spacing_);
    }
}

}  // namespace aipdf::app
