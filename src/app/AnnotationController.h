#pragma once

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVector>

#include "app/PageTransform.h"
#include "doc/AnnotationModel.h"
#include "doc/UndoStack.h"
#include "pdf/Annotation.h"
#include "pdf/PdfiumAdapter.h"

#include <memory>

namespace aipdf::app {

// 便签图标的大小（点）：便签批注的矩形就按这个尺寸生成。
inline constexpr double kNoteAnnotationSize = 16.0;

// 批注控制器：把批注模型、撤销栈与 PDFium 文档串在一起。
//
// 职责分工：
// - 模型（doc::AnnotationModel）是「当前有哪些批注」；
// - 撤销栈（doc::UndoStack）是「怎么走到这个状态的」；
// - 文档（pdf::PdfDocument）保存真正的批注对象，
//   写入是「模型 → 文档」的单向投影，改一次就重新投影那一页。
//
// 为什么显示由应用层画：PDFium 只渲染自带外观流（/AP）的批注，
// 而 159 的公开 API 无法为新建批注生成合规的 /AP
// （FPDFAnnot_SetAP 只写字典、不建 XObject），
// 所以批注的显示由画布按模型绘制（PageCanvas::paintAnnotations）。
// 写进文档是为了让批注成为文档里真实的对象，存盘时能直接带走。
class AnnotationController : public QObject {
    Q_OBJECT

public:
    // 画布上的批注工具。None 表示不接管鼠标。
    enum class Tool {
        None,    // 不接管鼠标：文字拖选与链接点击照常
        Note,    // 便签：点一下放一个
        Square,  // 矩形：拖出一块
        Ink,     // 墨迹：拖出一笔
    };
    Q_ENUM(Tool)

    explicit AnnotationController(pdf::PdfDocument document, QObject* parent = nullptr);
    ~AnnotationController() override;

    // 用户额外旋转（0/90/180/270）。只影响坐标换算，不改批注数据。
    void setUserRotation(int degrees);
    int userRotation() const;

    void setTool(Tool tool);
    Tool tool() const;

    // 基于选区创建标记类批注（高亮 / 下划线 / 删除线）。
    // quads 是逐行矩形（PDF user space）。成功返回新批注的 id，失败返回 0。
    quint64 addMarkAnnotation(pdf::AnnotationType type,
                              int pageIndex,
                              const QVector<QRectF>& quads);

    // 便签（rect 是 PDF user space 矩形）。
    quint64 addNote(int pageIndex, const QRectF& rect, const QString& contents);
    // 矩形。
    quint64 addSquare(int pageIndex, const QRectF& rect);
    // 墨迹：一笔（user space 的一串点）。
    quint64 addInk(int pageIndex, const QVector<QPointF>& stroke);

    // 删除一条批注。
    bool removeAnnotation(quint64 id);
    // 修改批注（颜色、正文）。
    bool updateAnnotation(quint64 id, const QColor& color, const QString& contents);

    // 撤销 / 重做。
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;

    // 批注列表。
    QVector<pdf::Annotation> annotationsForPage(int pageIndex) const;
    QVector<pdf::Annotation> allAnnotations() const;
    int annotationCount() const;
    int annotationCountForPage(int pageIndex) const;

    // 该页的显示空间变换（页面 /Rotate + 用户旋转）。取不到返回 false。
    bool transformForPage(int pageIndex, PageTransform* transform) const;

    // 显示空间矩形 → 用户空间矩形（旋转页上会换向，逐个角点换算后取包围盒）。
    QVector<QRectF> toUserSpaceRects(int pageIndex, const QVector<QRectF>& displayRects) const;

    const doc::AnnotationModel* model() const;

signals:
    // 某页的批注有变化。
    void annotationsChanged(int pageIndex);
    // 撤销 / 重做的可用性变了。
    void undoStateChanged();
    // 工具切换了。
    void toolChanged();

private:
    // 一页的几何：内容盒与页面自带的旋转。
    struct PageGeometry {
        pdf::MediaBox contentBox;
        int rotationDegrees = 0;
    };

    // 把某页的批注投影到文档（写进去，或撤掉）。
    void syncPageToDocument(int pageIndex);
    // 页面几何（按页缓存，取一次就不再问 PDFium）。
    bool pageGeometry(int pageIndex, PageGeometry* geometry) const;
    // 命令执行后同步：模型变了 → 文档跟着变。
    void handleModelChanged(int pageIndex);

    pdf::PdfDocument document_;
    doc::AnnotationModel model_;
    doc::UndoStack undoStack_;
    // 每页几何缓存。
    mutable QHash<int, PageGeometry> geometry_;
    // 已经在文档里写过批注的页：撤销或清空时要把它们都撤掉。
    QSet<int> syncedPages_;
    int userRotation_ = 0;
    Tool tool_ = Tool::None;
};

}  // namespace aipdf::app
