#include "app/PageLayout.h"

#include <QtGlobal>

namespace aipdf::app {

void PageLayout::setPages(const QVector<PageInfo>& pages) {
    pages_ = pages;
    recomputeTops();
}

void PageLayout::setScale(double zoom) {
    zoom_ = (zoom > 0.0) ? zoom : 1.0;
    recomputeTops();
}

void PageLayout::setUserRotation(int degrees) {
    // 归一到 0/90/180/270。
    int normalized = ((degrees % 360) + 360) % 360;
    normalized = (normalized / 90) * 90;
    if (normalized == userRotation_) {
        return;
    }
    userRotation_ = normalized;
    recomputeTops();
}

void PageLayout::setPageSpacing(double spacing) {
    spacing_ = (spacing >= 0.0) ? spacing : 0.0;
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

int PageLayout::userRotation() const {
    return userRotation_;
}

QSizeF PageLayout::pageSize(int index) const {
    if (index < 0 || index >= pages_.size()) {
        return {};
    }
    const PageInfo& info = pages_.at(index);
    // 页面自带的旋转已经在显示尺寸里了，这里只叠加用户旋转。
    double width = info.widthPoints;
    double height = info.heightPoints;
    if (userRotation_ == 90 || userRotation_ == 270) {
        std::swap(width, height);
    }
    return {width * zoom_, height * zoom_};
}

double PageLayout::pageTop(int index) const {
    if (index < 0 || index >= tops_.size()) {
        return 0.0;
    }
    return tops_.at(index);
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
    // tops_ 递增，二分查找最后一页起点不超过 y 的页。
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
