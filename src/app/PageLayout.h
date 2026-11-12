#pragma once

#include <QSizeF>
#include <QVector>

namespace aipdf::app {

// 连续文档的页面布局：负责把「页序号 + 缩放 + 旋转」换算成视口坐标。
//
// 坐标单位是逻辑像素（滚动区域用的就是逻辑像素）；
// 渲染出来的图像按设备像素比给位图设置 devicePixelRatio，画出来正好对上。
//
// 这是一个纯计算服务，不碰 PDFium，也不碰 Qt 事件循环，
// 因此可以脱离界面直接做单元测试。
class PageLayout {
public:
    // 单页的静态信息（单位是点，1/72 英寸）。
    // widthPoints/heightPoints 是**显示尺寸**（页面自带 /Rotate 已经应用过）。
    struct PageInfo {
        double widthPoints = 0.0;
        double heightPoints = 0.0;
    };

    // 设置页面列表。rotationDegrees 必须是 0/90/180/270。
    void setPages(const QVector<PageInfo>& pages);

    // 设置缩放倍率。zoom=1 表示 72 DPI。
    void setScale(double zoom);

    // 用户额外旋转的角度（0/90/180/270），叠加在页面自带的旋转之上。
    void setUserRotation(int degrees);

    // 页与页之间的间距（逻辑像素）。
    void setPageSpacing(double spacing);

    void clear();

    int pageCount() const;
    bool isEmpty() const;

    double zoom() const;
    int userRotation() const;

    // 某一页在视口坐标系里的尺寸与上边缘位置（逻辑像素）。
    QSizeF pageSize(int index) const;
    double pageTop(int index) const;

    // 整篇文档的高度（逻辑像素），供滚动区域使用。
    double totalHeight() const;

    // 纵坐标落在哪一页上；超出范围返回 -1。
    int pageAt(double y) const;

    // 视口里占面积最大的那一页（视口纵向区间 [scrollTop, scrollTop+viewportHeight]）。
    //
    // 为什么不用 pageAt(scrollTop)：最后一页往往没法滚到视口顶部
    // （内容高度不够，滚动会被夹住），这时「顶部那一页」并不是用户正在看的那页。
    // 界面上的页码指示按可见面积最大的页来报，跟用户眼睛看到的一致。
    // 视口里一页都没露出（整段空白）时退回 pageAt 的结果。
    int dominantPage(double scrollTop, double viewportHeight) const;

    // 落在这一页上的纵向区间下边缘（逻辑像素）。
    double pageBottom(int index) const;

private:
    QVector<PageInfo> pages_;
    QVector<double> tops_;
    double zoom_ = 1.0;
    int userRotation_ = 0;
    double spacing_ = 12.0;
    double totalHeight_ = 0.0;

    void recomputeTops();
};

}  // namespace aipdf::app
