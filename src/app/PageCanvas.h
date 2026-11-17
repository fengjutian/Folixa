#pragma once

#include <QAbstractScrollArea>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <limits>

#include "app/PageLayout.h"
#include "app/ReadingScheme.h"
#include "app/TileCache.h"
#include "app/MeasureGeometry.h"
#include "app/ViewModes.h"
#include "pdf/Annotation.h"
#include "i18n/Translate.h"

class QMouseEvent;
class QKeyEvent;
class QPainter;
class QPlainTextEdit;
class QEvent;
class QTimer;
class QElapsedTimer;

namespace aipdf::app {

class RenderThread;
class TextController;
class NavigationController;
class AnnotationController;
class PageTransform;

// 连续页面画布：把所有页面画在一张虚拟的长文档上，按视口裁剪。
//
// 用 QAbstractScrollArea 而不是「一个大 QWidget 放进 QScrollArea」：
// 500 页文档的高度会到几十万像素，超大控件在不同平台上会被截断。
// 这里自己管滚动条范围，只画视口内的那几页。
class PageCanvas : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit PageCanvas(QWidget* parent = nullptr);

    void setLayout(const PageLayout& layout);
    const PageLayout& layout() const;

    // 阅读配色：只影响阅读区底色与页面描边，页面像素不变。
    void setReadingScheme(theme::ReadingScheme scheme);
    theme::ReadingScheme readingScheme() const;

    void setTileCache(TileCache* cache);
    void setRenderThread(RenderThread* thread);

    // 绑定文本控制器：鼠标选择与高亮绘制都靠它。
    // 没有文本层时（扫描件）鼠标拖选不会有任何反应。
    void setTextController(TextController* controller);
    TextController* textController() const;

    // 绑定导航控制器：链接热区判定与点击跳转都靠它。
    void setNavigationController(NavigationController* controller);
    NavigationController* navigationController() const;

    // 绑定批注控制器：批注绘制与画布上的批注工具都靠它。
    void setAnnotationController(AnnotationController* controller);
    AnnotationController* annotationController() const;

    // 高亮某页的一块（显示空间矩形）：索引检索结果定位用。
    //
    // 传 pageIndex = -1 或空列表就是取消高亮。
    void setIndexHighlight(int pageIndex, const QVector<QRectF>& displayRects);

    // 高亮朗读中的那个词（显示空间矩形）。
    //
    // 为什么单独一套：朗读高亮与搜索高亮、选区高亮同时存在时不能互相覆盖——
    // 用户一边朗读一边选中另一段文字是常事。
    // 传 pageIndex = -1 或空列表就是取消高亮。
    void setSpeechHighlight(int pageIndex, const QVector<QRectF>& displayRects);

    // 当前视口顶页（从 0 开始，供界面显示）。
    int currentPage() const;

    // 视口内可见的页区间（含预取邻页）。
    void visiblePageRange(int* first, int* last) const;

    // ---------------------------------------------------------- 视图模式 ----
    //
    // 这一组全是「怎么显示」的事：标尺、拖到边缘自动滚动、翻页过渡、逆序镜像。
    // 它们只改画面，不改文档，也不进撤销栈 —— 用户按 Ctrl+Z 不该把标尺撤掉。

    // 标尺开关。刻度网格锚在页面的绝对坐标上：
    // 页面上的 10mm 永远显示 10mm，标尺滚到哪儿都不影响数字本身。
    void setRulerVisible(bool visible);
    bool rulerVisible() const;
    // 标尺单位（点 / 毫米 / 英寸）。
    void setRulerUnit(viewmodes::RulerUnit unit);
    viewmodes::RulerUnit rulerUnit() const;

    // 拖动批注或选区时把指针拖到视口边缘，视口跟着滚。
    void setAutoScrollEnabled(bool enabled);
    bool autoScrollEnabled() const;

    // 翻页过渡动画。None 表示不过渡。
    void setPageTransition(viewmodes::PageTransition transition);
    viewmodes::PageTransition pageTransition() const;

    // 逆序镜像（日漫式从右往左读）：只改显示空间，页面本身不翻，
    // 文件里更不会被改 —— 改的是「怎么显示」，不是「文档是什么」。
    void setMirrorEnabled(bool enabled);
    bool mirrorEnabled() const;

public slots:
    // 滚到指定页（从 0 开始）。
    void goToPage(int index);
    // 下一次在页面上拖出的矩形用于创建表单字段；空字符串取消。
    void setFormFieldPlacementKind(const QString& kind);
    void beginObjectSelection();
    void setObjectSelection(int pageIndex, const QRectF& userSpaceRect);
    void setObjectSnapGuides(const QVector<QRectF>& displayRects);
    void clearObjectSelection();
    void beginInlineTextEdit(int pageIndex, int objectIndex, const QRectF& displayRect,
                             const QString& text, double fontSize);

signals:
    void currentPageChanged(int index);
    void renderRequested(int firstPage, int lastPage);
    // 链接被点击：内部跳转给出目标页（从 0 开始），外部链接给出地址。
    void linkActivated(int pageIndex, int targetPage, const QString& uri);
    // 便签工具：点在页面上，要放一个便签（坐标是 PDF user space）。
    void noteAnnotationRequested(int pageIndex, const QPointF& userSpacePoint);
    // 矩形工具：拖出一块（坐标是 PDF user space）。
    void squareAnnotationRequested(int pageIndex, const QRectF& userSpaceRect);
    // 椭圆工具：拖出一块外接矩形。
    void ellipseAnnotationRequested(int pageIndex, const QRectF& userSpaceRect);
    // 直线/箭头：两点（用户空间）。
    void lineAnnotationRequested(int pageIndex, int type, const QPointF& from, const QPointF& to);
    // 多边形/云线：顶点序列（用户空间）。
    void polygonAnnotationRequested(int pageIndex, int type, const QVector<QPointF>& vertices);
    // 区域高亮：顶点序列（用户空间）。收尾方式与多边形一致（双击 / 回车）。
    void regionHighlightRequested(int pageIndex, const QVector<QPointF>& userPoints);
    // callout 箭头框：注释框与箭头尖（用户空间）。
    void calloutRequested(int pageIndex, const QRectF& userSpaceBox, const QPointF& userSpaceTip);
    // 状态栏提示（箭头框的第二步、区域高亮被拒等）。
    void hintMessage(const QString& text);
    // 文本框工具：拖出一块，交给界面去问文字。
    void freeTextRequested(int pageIndex, const QRectF& userSpaceRect);
    // 图章工具：点在页面上，要放一个图章（中心点，用户空间）。
    void stampRequested(int pageIndex, const QPointF& userSpacePoint);
    // 图片图章 / 声音批注：点在页面上，素材已经在工具选择时准备好。
    void imageStampRequested(int pageIndex, const QPointF& userSpacePoint);
    void soundRequested(int pageIndex, const QPointF& userSpacePoint);
    // 附件工具：点在页面上，要挂一个文件（中心点，用户空间）。
    void attachmentRequested(int pageIndex, const QPointF& userSpacePoint);
    // 周长/面积：逐点输入后一次给出结果（mode 是 MeasureMode 的整数值）。
    void measureShapeRequested(int pageIndex, int mode, const QVector<QPointF>& vertices);
    // 墨迹工具：拖出一笔（坐标是 PDF user space）。
    void inkAnnotationRequested(int pageIndex, const QVector<QPointF>& userSpaceStroke);
    void formFieldRectRequested(const QString& kind, int pageIndex,
                                const QRectF& userSpaceRect);
    void objectRectSelected(int pageIndex, const QRectF& userSpaceRect);
    void objectPointSelected(int pageIndex, const QPointF& userSpacePoint, bool additive);
    void objectSelectionTransformed(int pageIndex, const QRectF& oldUserSpaceRect,
                                    const QRectF& newUserSpaceRect);
    void objectSelectionRotated(int pageIndex, double degrees);
    void objectDoubleClicked(int pageIndex, const QPointF& userSpacePoint);
    void inlineTextEditCommitted(int pageIndex, int objectIndex, const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // 当前阅读配色。缓存成成员是为了 paintEvent 里不读设置（每帧读设置太慢）。
    theme::ReadingScheme readingScheme_ = theme::ReadingScheme::Light;
    // 切换语言后就地重译画布上的常驻文案（未打开文档时的占位提示）。
    void retranslateUi();

    PageLayout layout_;
    TileCache* cache_ = nullptr;
    RenderThread* renderThread_ = nullptr;
    TextController* textController_ = nullptr;
    NavigationController* navigationController_ = nullptr;
    AnnotationController* annotationController_ = nullptr;
    // 索引结果定位的高亮：页序号 + 显示空间矩形。
    int indexHighlightPage_ = -1;
    QVector<QRectF> indexHighlightRects_;
    // 朗读高亮：正在读的那个词。
    int speechHighlightPage_ = -1;
    QVector<QRectF> speechHighlightRects_;
    int currentPage_ = 0;
    // 上一次设置滚动范围时的内容高度与视口高度，用来判断范围是否真的变了。
    int lastContentHeight_ = -1;
    int lastViewportHeight_ = -1;
    // 正在拖选：记录锚点页与锚点字符（阅读顺序下标）。
    bool selecting_ = false;
    // 指针当前是否停在链接上：用来在状态翻转时记一笔轨迹。
    bool overLink_ = false;
    int anchorPage_ = -1;
    int anchorOrder_ = -1;
    // 正在用批注工具拖拽（矩形 / 墨迹）：坐标都在显示空间。
    bool drawingSquare_ = false;
    bool drawingEllipse_ = false;
    bool drawingFreeText_ = false;
    bool drawingFormField_ = false;
    bool drawingObjectSelection_ = false;
    bool objectEditMode_ = false;
    bool objectSelectionAdditive_ = false;
    int objectSelectionPage_ = -1;
    QRectF objectSelectionRect_;
    bool draggingObjectSelection_ = false;
    int objectDragHandle_ = -1;
    QPointF objectDragStart_;
    QRectF objectDragStartRect_;
    double objectRotationStartAngle_ = 0.0;
    double objectRotationDegrees_ = 0.0;
    double objectGuideX_ = std::numeric_limits<double>::quiet_NaN();
    double objectGuideY_ = std::numeric_limits<double>::quiet_NaN();
    QPlainTextEdit* inlineEditor_ = nullptr;
    int inlineEditPage_ = -1;
    int inlineEditObject_ = -1;
    QRectF inlineEditDisplayRect_;
    QVector<QRectF> objectSnapRects_;
    QString formFieldKind_;
    bool drawingLine_ = false;
    pdf::AnnotationType lineType_ = pdf::AnnotationType::Line;
    // 多边形/云线的逐点输入：第一次点击记页号，双击结束。
    QVector<QPointF> polygonPoints_;
    int polygonPage_ = -1;
    pdf::AnnotationType polygonType_ = pdf::AnnotationType::Polygon;
    // 当前这串顶点是不是「区域高亮」收的：多边形与区域高亮共用同一套
    // 采集与收尾流程，只有收尾时发出的信号不一样。
    bool polygonIsRegionHighlight_ = false;
    // callout 的两段式交互：先拖出注释框，再点一下选箭头指向的位置。
    bool drawingCallout_ = false;
    bool awaitingCalloutTip_ = false;
    int calloutPage_ = -1;
    QRectF calloutBoxDisplay_;
    // 等箭头尖时鼠标所在的位置（显示空间），只用来画预览线。
    QPointF calloutTipPreviewDisplay_;
    MeasureMode measureMode_ = MeasureMode::Distance;
    bool drawingInk_ = false;
    // 橡皮正在擦：按住拖动期间持续命中判定。
    bool erasing_ = false;
    int drawPage_ = -1;
    QPointF drawAnchor_;
    QPointF drawCurrent_;
    QVector<QPointF> inkPoints_;

    void updateScrollRange();
    void requestVisiblePages();
    double pageLeft(int index) const;
    // 某页在视口里的矩形（逻辑像素）。
    QRectF pageRectInViewport(int index) const;
    // 擦掉 displayPoint 附近命中的墨迹（整笔）。
    void eraseAt(int pageIndex, const QPointF& displayPoint);
    // 视口坐标 → 页面序号 + 页面显示空间坐标（点）。
    bool pagePointAt(const QPoint& viewportPos, int* pageIndex, QPointF* displayPoint) const;
    // 页面显示空间矩形 → 视口矩形。
    QRectF toViewportRect(const QRectF& displayRect, const QRectF& pageRect, double zoom) const;
    // 画标尺（水平一根 + 垂直一根），锚在当前页的绝对坐标上。
    void paintRulers(QPainter& painter, int pageIndex);
    // 自动滚动：指针贴到视口边缘时算出滚动速度并交给定时器。
    void updateAutoScroll(int pointerViewportY);
    void startAutoScroll();
    void stopAutoScroll();
    void onAutoScrollTick();
    // 翻页过渡：先抓下旧画面，再按进度把它淡出 / 推走。
    void startPageTransition();
    void onTransitionTick();
    // 逐点输入的收尾（多边形 / 区域高亮共用）。
    void finishPolygonInput();
    // 画搜索高亮与选区高亮。
    void paintHighlights(QPainter& painter, int pageIndex, const QRectF& pageRect, double zoom) const;
    // 画批注（按模型绘制：PDFium 不会渲染没有外观流的批注）。
    void paintAnnotations(QPainter& painter,
                          int pageIndex,
                          const QRectF& pageRect,
                          double zoom) const;
    // 画 callout 箭头框：注释框描边 + 箭头折线 + 箭头尖。
    void paintCallout(QPainter& painter,
                      const pdf::Annotation& annotation,
                      const PageTransform& transform,
                      const QRectF& pageRect,
                      double zoom) const;
    // 正在拖的批注工具的预览。
    void paintAnnotationPreview(QPainter& painter,
                                const QRectF& pageRect,
                                double zoom) const;
    // 按指针位置更新光标：悬在链接上时给手型。
    void updateLinkCursor(const QPoint& viewportPos);
    // 显示空间点 → 用户空间点（批注的坐标一律用用户空间）。
    QPointF toUserSpacePoint(int pageIndex, const QPointF& displayPoint) const;
    // 批注工具激活时把光标换成十字，退出时恢复。
    void updateToolCursor();
    void repositionInlineEditor();
    void finishInlineTextEdit(bool commit);

    // 没有打开文档时画在画布中央的占位提示。
    QString emptyText_;
    // 一次设定、长期显示的界面文案：切换语言时就地重译。
    i18n::TextBindings texts_;

    // ------------------------------------------------------- 视图模式状态 ----
    bool rulerVisible_ = false;
    viewmodes::RulerUnit rulerUnit_ = viewmodes::RulerUnit::Millimeters;
    bool autoScrollEnabled_ = false;
    viewmodes::PageTransition pageTransition_ = viewmodes::PageTransition::None;
    bool mirrorEnabled_ = false;
    // 自动滚动：定时器 + 指针位置 + 速度（像素/秒，正 = 向下）。
    QTimer* autoScrollTimer_ = nullptr;
    double autoScrollPointerY_ = 0.0;
    double autoScrollVelocity_ = 0.0;
    // 翻页过渡：旧页截图 + 已经过去的毫秒数。
    QTimer* transitionTimer_ = nullptr;
    QPixmap transitionSnapshot_;
    qint64 transitionElapsedMs_ = 0;
};

}  // namespace aipdf::app
