#pragma once

#include <QByteArray>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::pdf {

// 候选字段的类型。
//
// 为什么单独列一个类型，而不是直接用 FormFieldKind：
// 识别阶段只知道"这看起来像什么"，还不知道要不要建域；
// FormFieldKind 是建域时的类型（还带条形码这种识别不出来的种类），
// 两者语义不同，混用会让"识别"提前替"建域"做决定。
enum class DetectedFieldKind {
    Text,
    Number,
    Date,
    CheckBox,
};

// 识别出来的一个候选字段。
struct DetectedFormField {
    // 所在页（0 起）。
    int pageIndex = 0;
    // 字段名：来自标签文字，去掉了结尾的冒号与空白。
    QString name;
    // 标签原文（"姓名："这样的带冒号形式）。
    QString label;
    // 候选区域：页坐标（PDF user space，左下原点），与页面图形、字符框同一坐标系。
    QRectF rect;
    // 类型推断（依据标签文字与框的形状）。
    DetectedFieldKind kind = DetectedFieldKind::Text;
    // 置信度（0..1）。给界面排序用：低置信度的排在后面，让人先看高置信度的。
    double confidence = 0.0;
};

// 文档里**已有**的 AcroForm 域。
//
// 为什么与候选分开列：已有域是事实（用户填过的、或者别人做好的），
// 候选是猜测（线框暗示出来的）。混在一起会让界面把两者当成同一种东西，
// 用户点"全部创建"就会重复建域。
struct ExistingFormField {
    int pageIndex = 0;
    QString name;
    // 页坐标（PDF user space，左下原点）。
    QRectF rect;
    // PDFium 的域类型（FPDF_FORMFIELD_*）。保留原值，界面照旧按它显示。
    int pdfiumType = 0;
};

// 一次表单自动识别的结果。
struct FormDetectionResult {
    // 是否成功。失败时 error 里有中文原因。
    bool ok = false;
    // 失败原因（中文）。成功时为空。
    QString error;
    // 文档页数。
    int pageCount = 0;
    // 文档里已有的 AcroForm 域（照旧枚举，行为不变）。
    QVector<ExistingFormField> existingFields;
    // **没有域**的页上识别出来的候选字段。
    QVector<DetectedFormField> candidates;
    // 已经有域的页上、线框暗示但还没建域的位置（补充候选，与上面分开）。
    QVector<DetectedFormField> supplementary;
    // 逐页的问题（中文原因）。例如某页的图形取不出来。
    QStringList warnings;

    int totalCandidateCount() const { return candidates.size() + supplementary.size(); }
};

// 识别整篇文档里的表单候选字段。
//
// 做什么：
//   * 已有 AcroForm 域：照旧枚举（existingFields），行为与以前完全一致；
//   * **无域表单页**：把页面上用线画出来的表格与填空区找出来（candidates），
//     再把线框左侧或上方的标签文字（"姓名："这种）与候选格配对，
//     推断字段名、类型与置信度；
//   * 已经有域的页：线框暗示但还没建域的位置放进 supplementary，
//     不与 candidates 混在一起。
//
// 坐标口径：候选区域是 **PDF user space**（左下原点、y 向上），单位点，
// 与页面图形、字符框同一坐标系——这三者必须同源，
// 否则标签与框线会整体错位。
//
// 边界（认不出来就说认不出来，不硬凑）：
//   * 没有标签文字的线框区域**不报**：正文段落、页眉页脚因此不会被当成字段；
//   * 页眉页脚带（页面上下各 6%）里的文字不作为标签；
//   * 整页大小的框（页面边框）不报；
//   * 只认水平/垂直的直线段，斜线与曲线不参与；
//   * XFA 动态表单不在这条路径里（PDFium 没有 XFA 引擎）。
//
// 依赖：矢量路径走 qpdf（extractPageGraphics），文字层与已有域走 PDFium。
// 未启用 qpdf 的构建下如实报错，不拿别的路径冒充。
FormDetectionResult detectFormFields(const QByteArray& pdfBytes);

// 只识别一页（页码从 0 起）。上面的结果里只会有这一页的内容。
FormDetectionResult detectFormFieldsOnPage(const QByteArray& pdfBytes, int pageIndex);

// 按标签文字与框的形状推断类型。
//
// 为什么单独暴露成公开函数：类型推断的关键词表要能单独测，
// 而且界面（设计助手）拿到候选之后也可能想让人改类型。
DetectedFieldKind inferFieldKind(const QString& label, const QRectF& rect);

// 类型的界面显示名（英文小写，与 kindName 一致的风格）。
QString detectedKindName(DetectedFieldKind kind);

}  // namespace aipdf::pdf
