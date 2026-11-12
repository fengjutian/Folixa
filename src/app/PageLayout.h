#pragma once

#include <QSizeF>
#include <QVector>

namespace aipdf::app {

// 连续文档的页面布局：负责把「页序号 + 缩放 + 旋转」换算成视口坐标。
//
// 这是一个纯计算服务，不碰 PDFium，也不碰 Qt 事件循环，
// 因此可以脱离界面直接做单元测试。
class PageLayout {
public:
    // 单页的静态信息（单位是点，1/72 英寸）。
    struct PageInfo {
        double widthPoints = 0.0;
        double heightPoints = 0.0;
        int rotationDegrees = 0;
    };

    // 设置页面列表。rotationDegrees 必须是 0/90/180/270。
    void setPages(const QVector<PageInfo>& pages);

    // 设置缩放倍率与设备像素比。zoom=1 表示 72 DPI。
    void setScale(double zoom, double devicePixelRatio);

    // 页与页之间的间距（设备像素）。
    void setPageSpacing(double spacing);

    // 左右留白（设备像素）。
    void setMargins(double left, double right);

    void clear();

    int pageCount() const;
    bool isEmpty() const;

    double zoom() const;
    double devicePixelRatio() const;

    // 某一页在视口坐标系里的尺寸与上边缘位置（设备像素）。
    QSizeF pageSize(int index) const;
    double pageTop(int index) const;
    double pageLeft(int index) const;

    // 整篇文档的尺寸（设备像素），供滚动区域使用。
    double totalHeight() const;

    // 纵坐标落在哪一页上；超出范围返回 -1。
    int pageAt(double y) const;

    // 落在这一页上的纵向区间（设备像素）。
    double pageBottom(int index) const;

private:
    QVector<PageInfo> pages_;
    QVector<double> tops_;
    double zoom_ = 1.0;
    double devicePixelRatio_ = 1.0;
    double spacing_ = 12.0;
    double marginLeft_ = 0.0;
    double marginRight_ = 0.0;
    double totalHeight_ = 0.0;

    void recomputeTops();
};

}  // namespace aipdf::app
