#pragma once

#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

#include "pdf/forms/FormFieldWriter.h"

namespace aipdf::pdf {

// 设计出来的一个字段。
//
// 用比例坐标（相对页面尺寸）而不是绝对点：同一份设计套到 A4 与 Letter 上都能对齐。
struct DesignedFormField {
    QString name;
    QString toolTip;
    FormFieldKind kind = FormFieldKind::Text;
    double relX = 0.1;
    double relY = 0.1;
    double relWidth = 0.3;
    double relHeight = 0.03;
};

// 版面规则：把字段清单排到页面上时用的尺寸（单位：点）。
//
// 为什么这些是"点"而不是比例：版面一旦排定就要能直接建域，
// 比例还要再乘一次页面尺寸，中间任何一步的坐标系约定搞反都会整体偏移。
struct FormLayoutOptions {
    // 页边距（点）。
    double marginLeft = 56.0;
    double marginRight = 56.0;
    double marginTop = 56.0;
    double marginBottom = 56.0;
    // 字段外观的字号：行高与字段高度都由它推算。
    double fontSize = 10.0;
    // 行与行之间留白（点）。
    double rowGap = 8.0;
    // 标签列宽（点）：左边留给字段名的那一列。
    // 助手不画标签文字（建域接口只能建域），这一列只是留白，
    // 让输入框对齐成一列、看起来像表单；不想要就设成 0。
    double labelWidth = 120.0;
    // 每行放几个字段。
    int columns = 1;
};

// 排好版的一个字段：落在哪一页的哪个位置。
struct PlacedFormField {
    // 绝对页号（从 0 起）。
    int pageIndex = 0;
    // 相对起始页的页偏移。
    int pageOffset = 0;
    QString name;
    QString toolTip;
    FormFieldKind kind = FormFieldKind::Text;
    // 页坐标（PDF user space，左下原点、y 向上），单位点。
    QRectF rect;
};

// 版面排布的结果。
struct FormLayoutResult {
    // 是否排成功。失败时 error 里有中文原因。
    bool ok = false;
    // 失败原因（中文）。成功时为空。
    QString error;
    // 排好版的字段（按清单顺序）。
    QVector<PlacedFormField> placements;
    // 用掉了几页。
    int pagesUsed = 0;
    // 现有页面不够，还需要补几页（0 表示够用）。
    int extraPagesNeeded = 0;
};

// 模型看到的系统提示词。
//
// 为什么要求"只输出 JSON"：我们要的是一份能落盘的字段清单，不是散文。
// 让模型自己套代码块、加解释，解析就得写一堆容错——所以在提示词里就把格式钉死。
QString designSystemPrompt();

// 模型的种类名 → 建域用的类型。认不出来的一律当文本域。
FormFieldKind kindFromName(const QString& name);

// 建域用的类型 → 界面表格里显示的种类名。
QString kindName(FormFieldKind kind);

// 把模型可能加上的代码块围栏去掉。
QString stripCodeFence(const QString& text);

// 解析模型给出的字段清单。
//
// 失败时 *error 写的是可以直接显示给用户的说明：文案与「设计助手」对话框
// 原来显示的完全一致（沿用同一个翻译上下文），所以界面行为一个字都没变。
// out / error 都可以是空指针。
bool parseDesignedFields(const QString& answer, QVector<DesignedFormField>* out, QString* error);

// 把字段清单排到页面上：算出每个字段的绝对位置与尺寸，放不下就顺延到下一页。
//
// 为什么版面要在这里算、而且不许模型给坐标：
//   * 模型给的坐标是"猜"的——经常互相重叠、跑到页外、或者挤在一角；
//     版面必须由**确定性的计算**得出，同一份清单永远排出同一个结果；
//   * 排出来的位置可以直接建域（页坐标、单位点），中间不再有比例换算，
//     也就没有换算搞反的风险。
//
// 排布规则（每页独立算，页与页之间不连续）：
//   * 内容区 = 页面去掉四边页边距；
//   * 字段按清单顺序从上往下排，一行一个（columns > 1 时一行几个）；
//   * 左边留出 labelWidth 的标签列，输入框占剩下的宽度；
//   * 行高 = 字号 × 1.8 + 行距，勾选框与单选按钮只占其中一小块；
//   * 下一行放不下（会越过下边距）就顺延到下一页，从页顶重新排。
//
// pageSizes 给的是每一页的尺寸（顺序即页序）；pagesUsed 是用掉了几页，
// extraPagesNeeded 是页面不够时还需要补几页（补页由调用方决定）。
// out / error 都可以是空指针。
FormLayoutResult layoutDesignedFields(const QVector<DesignedFormField>& fields,
                                       const QVector<QSizeF>& pageSizes,
                                       int firstPageIndex = 0,
                                       const FormLayoutOptions& options = FormLayoutOptions());

}  // namespace aipdf::pdf
