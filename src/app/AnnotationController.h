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
#include "doc/EditContext.h"
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
//
// v0.8.0 起它还管**页面结构**（插入、删除、旋转、移动页）：
// 页面操作与批注操作共用同一个撤销栈，所以这个类同时是
// 编辑上下文（doc::EditContext）——命令通过它拿到模型与文档。
class AnnotationController : public QObject, public doc::EditContext {
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

    // ------------------------------------------------------- 页面结构操作 ----
    //
    // 页面操作与批注操作共用同一个撤销栈，可以互相撤销。

    // 在 index 处插入一张 width×height（点）的空白页。
    bool insertBlankPage(int index, double width, double height);
    // 删除第 index 页。
    bool deletePage(int index);
    // 把第 index 页转 quarterTurns 个四分之一圈（顺时针）。
    bool rotatePage(int index, int quarterTurns);
    // 把第 from 页移动到第 to 页（to 是移动之后的目标下标）。
    bool movePage(int from, int to);

    // 当前页数。
    int pageCount() const;
    // 某页的尺寸（点，已含页面自带的旋转）。
    double pageWidth(int index) const;
    double pageHeight(int index) const;
    // 某页自带的旋转（四分之一圈：0=0°，1=90°，2=180°，3=270°）。
    int pageRotation(int index) const;

    // ---------------------------------------------------------- 编辑上下文 ----

    // 批注模型（命令用它读写批注）。
    doc::AnnotationModel* annotationModel() override;
    // 页面操作入口（命令通过它改页面结构）。
    doc::PageOperations* pages() override;
    // 页面结构变了：批注页码重映射、几何缓存作废、通知视图重排。
    void pageStructureChanged(const QVector<int>& pageMapping) override;

    // 当前历史的序列化命令（恢复日志用）：按执行顺序。
    QVector<QByteArray> serializedHistory() const;

    // 执行一条命令并入撤销栈（恢复日志重放用）。
    //
    // 与用户操作走同一条路：命令进栈、能撤销、也重新记一次恢复日志。
    bool applyCommand(std::unique_ptr<doc::EditCommand> command);

signals:
    // 某页的批注有变化。
    void annotationsChanged(int pageIndex);
    // 撤销 / 重做的可用性变了。
    void undoStateChanged();
    // 工具切换了。
    void toolChanged();
    // 页面结构变了：页数、顺序或某页的旋转改了。
    // 视图要重排页面、重算布局、换掉渲染线程的文档副本。
    void documentStructureChanged();

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
    // 页面结构操作的原始实现（命令通过编辑上下文拿到它）。
    // 声明在文档之后：它持有文档句柄，析构时要先于文档释放。
    std::unique_ptr<class PdfPageOperations> pageOperations_;
    // 每页几何缓存。
    mutable QHash<int, PageGeometry> geometry_;
    // 已经在文档里写过批注的页：撤销或清空时要把它们都撤掉。
    QSet<int> syncedPages_;
    int userRotation_ = 0;
    Tool tool_ = Tool::None;
};

}  // namespace aipdf::app
