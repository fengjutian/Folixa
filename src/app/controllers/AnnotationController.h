#pragma once

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVector>

#include "app/layout/PageTransform.h"
#include "doc/edit/AnnotationModel.h"
#include "doc/edit/EditContext.h"
#include "doc/edit/UndoStack.h"
#include "app/layout/MeasureGeometry.h"
#include "pdf/annot/Annotation.h"
#include "pdf/kernel/PdfiumAdapter.h"

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
        Ellipse, // 椭圆：拖出一块
        Line,    // 直线：拖出两点
        Arrow,   // 箭头：拖出两点
        Polygon, // 多边形：逐点点击，双击结束
        Cloud,   // 云线：同多边形，边框画成扇贝
        Callout,      // 箭头框：先拖出注释框，再点一下选箭头指向的位置
        RegionHighlight, // 区域高亮：逐点点击，双击结束，画半透明填充
        FreeText, // 文本框：拖出一块再输入文字
        Stamp,   // 图章：点一下放一个
        NumberStamp, // 编号图章：点一下放一个，编号自动递增
        ImageStamp, // 图片图章
        Sound,   // 声音批注
        Measure, // 测量：拖出两点
        MeasurePerimeter, // 周长：逐点
        MeasureArea,      // 面积：逐点
        Attachment, // 附件：点一下放一个
        Ink,     // 墨迹：拖出一笔
        Eraser,  // 橡皮：划过的墨迹整笔删掉
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
    // 以矩形为界的图形（矩形 / 椭圆）。
    quint64 addShape(pdf::AnnotationType type, int pageIndex, const QRectF& rect);
    // 直线 / 箭头：两点确定，/L 由写入器补齐。
    quint64 addLine(pdf::AnnotationType type, int pageIndex, const QPointF& from,
                    const QPointF& to);
    // 多边形 / 云线：顶点序列，/Vertices 与 /BE 由写入器补齐。
    // 文本框：拖出一块，再把文字放进去。
    // 图章：在 rect 里放一个文字图章（边框 + 居中粗体字）。
    quint64 addStamp(int pageIndex, const QRectF& rect, const QString& label);
    // 图片图章：图片字节随文档一起保存，外观写成 Image XObject。
    quint64 addImageStamp(int pageIndex, const QRectF& rect, const QByteArray& imageBytes,
                          const QString& label = {});
    // 声音批注：WAV 已被解析成裸 PCM。
    // 用一段完整 WAV 创建声音批注（附件形态，见 .cpp 里的说明）。
    quint64 addSoundFromWav(int pageIndex, const QRectF& rect, const QString& name,
                            const QByteArray& wav);
    quint64 addSound(int pageIndex, const QRectF& rect, const QString& name, const QByteArray& pcm,
                     int rate, int channels, int bits);
    // 距离测量：两点 + 比例（1 点 = scale 个单位），长度写进 /Contents。
    quint64 addMeasure(int pageIndex, const QPointF& from, const QPointF& to, double scale = 1.0);
    // 周长 / 面积：顶点序列，文字直接写进 /Contents。
    quint64 addMeasureShape(MeasureMode mode, int pageIndex, const QVector<QPointF>& vertices,
                            double scale = 1.0);
    // 附件：在页面上放一个回形针图标，文件内容随文档保存。
    quint64 addAttachment(int pageIndex, const QPointF& point, const QString& fileName,
                          const QByteArray& data);
    quint64 addFreeText(int pageIndex, const QRectF& rect, const QString& text,
                        double fontSize = 12.0);
    quint64 addPolygon(pdf::AnnotationType type, int pageIndex,
                       const QVector<QPointF>& vertices);

    // 添加一条**已经算好外观**的批注（callout 箭头框、区域高亮这类）。
    //
    // 为什么让调用方把批注填好再进来：几何与外观流的内容由
    // pdf::AnnotationShapes 那一层算（它是纯函数，可以单独编译与单测），
    // 这一层只负责重新分配 id 并进撤销栈 —— 走的是与普通批注完全一样的流程。
    //
    // 注意：这里**不能**去调 pdf::AnnotationShapes 的函数：
    // 有几个单测目标只编了 AnnotationController.cpp，没有编 AnnotationShapes.cpp，
    // 引用它会直接链接失败。几何与外观流由界面层算好后整个传进来。
    quint64 addPreparedAnnotation(pdf::Annotation annotation);
    // 墨迹：一笔（user space 的一串点）。
    quint64 addInk(int pageIndex, const QVector<QPointF>& stroke);

    // 删除一条批注。
    bool removeAnnotation(quint64 id);
    // 回复：在父批注旁边挂一条便签，/IRT 指向父批注。
    quint64 addReply(quint64 parentId, const QString& text, const QString& author = {});
    // 从外部（XFDF 等）导入一条批注：重新分配 id，其余字段照抄。
    quint64 importAnnotation(const pdf::Annotation& annotation);
    // 审阅状态：Accepted / Rejected / Cancelled / Completed / None（空字符串表示清除）。
    bool setReviewState(quint64 id, const QString& state);
    // 批量改外观：颜色 / 线宽 / 不透明度。width 或 opacity <=0 表示不动。
    bool updateAppearance(const QVector<quint64>& ids, const QColor& color, double lineWidth,
                          double opacity);
    // 修改批注（颜色、正文）。
    bool updateAnnotation(quint64 id, const QColor& color, const QString& contents);
    // 设置替代文本（写进批注的 /Alt）。空字符串表示清除。
    bool setAltText(quint64 id, const QString& altText);

    // 撤销 / 重做。
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;
    int undoCount() const;
    int redoCount() const;
    void discardRedoHistory();
    QString undoText() const;
    QString redoText() const;

    // 批注列表。
    QVector<pdf::Annotation> annotationsForPage(int pageIndex) const;
    QVector<pdf::Annotation> allAnnotations() const;
    // 附件内容（id → 文件名与字节）。保存时交给 DocumentWriter：
    // PDFium 的对象里没有这些内容，只能从模型带过去。
    QHash<quint64, QPair<QString, QByteArray>> attachmentPayloads() const;
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
    // 用另一份文档里备份的一页替换当前页（可撤销）。
    bool replacePageWithBackup(int index, std::shared_ptr<doc::PageBackup> backup);

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
    // 打开文档时把文件里已有的批注读进模型（只接管能无损往返的类型）。
    void loadExistingAnnotations();
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
    // 本会话对哪些批注负责（按页）。同步时只删这些 id：文件里其它软件写的、
    // 或本程序读不回来的批注一律不动，避免“新增一条就把同页旧批注删掉”。
    QHash<int, QSet<quint64>> managedIds_;
    // 已经读过「文件里已有批注」的页：大文档打开时不一次性扫全篇，
    // 第一次编辑某页时再补读（见 syncPageToDocument）。
    QSet<int> seededPages_;
    int userRotation_ = 0;
    Tool tool_ = Tool::None;
};

}  // namespace aipdf::app
