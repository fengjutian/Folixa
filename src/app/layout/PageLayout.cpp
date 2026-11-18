#include "app/layout/PageLayout.h"

#include <QtGlobal>

#include <algorithm>

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

void PageLayout::setLayoutMode(LayoutMode mode) {
    if (mode_ == mode) {
        return;
    }
    mode_ = mode;
    recomputeTops();
}

PageLayout::LayoutMode PageLayout::layoutMode() const {
    return mode_;
}

void PageLayout::setActivePage(int index) {
    if (index < 0 || index >= pages_.size()) {
        return;
    }
    if (activePage_ == index) {
        return;
    }
    activePage_ = index;
    if (mode_ == LayoutMode::SinglePage) {
        recomputeTops();
    }
}

int PageLayout::activePage() const {
    return activePage_;
}

void PageLayout::clear() {
    pages_.clear();
    tops_.clear();
    rows_.clear();
    totalHeight_ = 0.0;
    totalWidth_ = 0.0;
    columnWidth_ = 0.0;
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

QRectF PageLayout::pageRect(int index) const {
    if (index < 0 || index >= pages_.size()) {
        return {};
    }
    const int row = rowOfPage(index);
    if (row < 0) {
        return {};
    }
    const Row& current = rows_.at(row);
    const QSizeF size = pageSize(index);
    const double column = (index - current.firstPage) * (columnWidth_ + spacing_);
    return QRectF(column, current.top, size.width(), size.height());
}

double PageLayout::totalHeight() const {
    return totalHeight_;
}

double PageLayout::totalWidth() const {
    return totalWidth_;
}

double PageLayout::rowWidth(int index) const {
    const int row = rowOfPage(index);
    if (row < 0) {
        return 0.0;
    }
    return rows_.at(row).width;
}

int PageLayout::pageAt(double y) const {
    if (rows_.isEmpty()) {
        return -1;
    }
    if (y < 0.0) {
        return rows_.first().firstPage;
    }
    // rows_ 按 top 递增，二分找最后一个 top <= y 的行。
    int low = 0;
    int high = static_cast<int>(rows_.size()) - 1;
    int result = 0;
    while (low <= high) {
        const int mid = (low + high) / 2;
        if (rows_.at(mid).top <= y) {
            result = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return rows_.at(result).firstPage;
}

int PageLayout::pageAtPoint(const QPointF& point) const {
    if (rows_.isEmpty()) {
        return -1;
    }
    const int rowIndex = [&]() {
        int low = 0;
        int high = static_cast<int>(rows_.size()) - 1;
        int result = -1;
        while (low <= high) {
            const int mid = (low + high) / 2;
            if (rows_.at(mid).top <= point.y()) {
                if (point.y() <= rows_.at(mid).top + rows_.at(mid).height) {
                    return mid;
                }
                result = mid;
                low = mid + 1;
            } else {
                high = mid - 1;
            }
        }
        return result;
    }();
    if (rowIndex < 0) {
        return -1;
    }

    const Row& row = rows_.at(rowIndex);
    for (int offset = 0; offset < row.count; ++offset) {
        const int pageIndex = row.firstPage + offset;
        if (pageRect(pageIndex).contains(point)) {
            return pageIndex;
        }
    }
    return -1;  // 落在页间距或行内空白处
}

int PageLayout::dominantPage(double scrollTop, double viewportHeight) const {
    if (rows_.isEmpty()) {
        return -1;
    }
    if (viewportHeight <= 0.0) {
        return std::max(0, pageAt(scrollTop));
    }
    const double viewTop = scrollTop;
    const double viewBottom = scrollTop + viewportHeight;

    int best = -1;
    double bestHeight = 0.0;
    for (int pageIndex = 0; pageIndex < pages_.size(); ++pageIndex) {
        const double top = std::max(pageTop(pageIndex), viewTop);
        const double bottom = std::min(pageBottom(pageIndex), viewBottom);
        const double visible = bottom - top;
        if (visible > bestHeight) {
            bestHeight = visible;
            best = pageIndex;
        }
    }
    if (best < 0) {
        // 视口正好落在页间距里：一页都没露出，退回顶部那一页。
        return std::max(0, pageAt(scrollTop));
    }
    return best;
}

double PageLayout::pageBottom(int index) const {
    const int row = rowOfPage(index);
    if (row < 0) {
        return 0.0;
    }
    return rows_.at(row).top + rows_.at(row).height;
}

int PageLayout::rowOfPage(int index) const {
    if (index < 0 || index >= pages_.size() || rows_.isEmpty()) {
        return -1;
    }
    // 行数很少（页面数/2），线性扫描足够；二分会因为"行内两页"而需要额外换算。
    for (int row = 0; row < rows_.size(); ++row) {
        const Row& current = rows_.at(row);
        if (index >= current.firstPage && index < current.firstPage + current.count) {
            return row;
        }
    }
    return -1;
}

void PageLayout::recomputeTops() {
    tops_.clear();
    rows_.clear();
    totalHeight_ = 0.0;
    totalWidth_ = 0.0;
    columnWidth_ = 0.0;

    if (pages_.isEmpty()) {
        return;
    }

    tops_.resize(pages_.size());
    for (int i = 0; i < pages_.size(); ++i) {
        columnWidth_ = std::max(columnWidth_, pageSize(i).width());
    }

    // 翻页模式：只把当前页排进布局。其余页的矩形为空，
    // 画布不会预取也不会画到邻页——这正是「单页」和「连续」的区别。
    if (mode_ == LayoutMode::SinglePage) {
        activePage_ = std::clamp(activePage_, 0, static_cast<int>(pages_.size()) - 1);
        tops_.fill(0.0);
        Row row;
        row.firstPage = activePage_;
        row.count = 1;
        row.top = 0.0;
        row.height = pageSize(activePage_).height();
        row.width = pageSize(activePage_).width();
        rows_.append(row);
        totalHeight_ = row.height;
        totalWidth_ = row.width;
        columnWidth_ = row.width;
        return;
    }

    // 按排布方式切行：纵向长卷一页一行，双页两页一行，封面模式第一页单独一行。
    const int pairStart = (mode_ == LayoutMode::FacingCover) ? 1 : 0;
    const int perRow = (mode_ == LayoutMode::Continuous) ? 1 : 2;

    int pageIndex = 0;
    double y = 0.0;
    if (pairStart == 1) {
        Row cover;
        cover.firstPage = 0;
        cover.count = 1;
        cover.top = 0.0;
        cover.height = pageSize(0).height();
        cover.width = pageSize(0).width();
        rows_.append(cover);
        tops_[0] = 0.0;
        y = cover.height + spacing_;
        pageIndex = 1;
    }

    while (pageIndex < pages_.size()) {
        Row row;
        row.firstPage = pageIndex;
        row.count = std::min(perRow, static_cast<int>(pages_.size()) - pageIndex);
        row.top = y;
        double height = 0.0;
        for (int offset = 0; offset < row.count; ++offset) {
            height = std::max(height, pageSize(pageIndex + offset).height());
            tops_[pageIndex + offset] = y;
        }
        row.height = height;
        double width = 0.0;
        for (int offset = 0; offset < row.count; ++offset) {
            width = std::max(width, pageSize(pageIndex + offset).width());
        }
        row.width = width;
        rows_.append(row);
        y += height + spacing_;
        pageIndex += row.count;
    }

    totalHeight_ = std::max(0.0, y - spacing_);
    totalWidth_ = (perRow == 1) ? columnWidth_ : columnWidth_ * 2 + spacing_;
}

}  // namespace aipdf::app
