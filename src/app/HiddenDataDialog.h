#pragma once

#include "pdf/PdfSecurity.h"

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>

class QTreeWidget;

namespace aipdf::app {

// 一类隐藏数据的中文标题。
//
// 为什么这张表在 app 层：领域层只给 ASCII 的 id（稳定、可比、可存），
// 中文句子归界面拼，这样切语言时标题也跟着变。
//
// 认不出来的 id 原样返回（显示成 id 本身），不编一句。
QString hiddenDataCategoryTitle(const QString& id);

// 「这一类有几项」的说明（中文）。
QString hiddenDataCategoryCountText(const pdf::HiddenDataCategory& category);

// 「清理隐藏信息」的勾选式对话框。
//
// 为什么不是一句「确定要清理吗」：原来那个组合入口会把**嵌入文件本体**一起删掉，
// 用户点一下就丢了附件，还不知道自己丢了什么。这里把每一类列出来让他自己勾，
// 嵌入文件默认不勾 —— 要删得他自己勾。
class HiddenDataDialog final : public QDialog {
    Q_OBJECT

public:
    explicit HiddenDataDialog(QWidget* parent = nullptr);

    // 填入枚举结果（pdf::enumerateHiddenData 的返回值）。
    void setCategories(const QVector<pdf::HiddenDataCategory>& categories);

    // 用户勾选的类别 id。空 = 什么都不清（调用方必须自己处理这个情况，
    // 不能把「没勾」当成「全清」）。
    QStringList selectedCategoryIds() const;

    // 全部类别的中文标题（按界面顺序），供调用方拼提示。
    QStringList categoryTitles() const;

    // 这一类默认要不要勾上。
    //
    // 只有「元数据、元数据流、增量历史、脚本、自动动作、缩略图」默认勾上。
    //
    // 为什么不给「除嵌入文件外全勾」：签名、表单已填值、OCR 文字层、图层配置、
    // XFA 表单、隐藏批注 —— 这些删掉用户很难恢复（签名还牵涉法律效力）。
    // 让用户点一下「清理」就顺手把这些清掉，是把「一键清理」做成了「一键毁数据」。
    // 想清就自己勾，勾选框就在眼前。
    static bool defaultChecked(const QString& id);

    // 默认勾选的类别 id（界面顺序）。
    //
    // 批处理没有逐项勾选的界面（它是对一批文件跑固定步骤），
    // 那里用的就是这份清单 —— 两处口径必须一致。
    static QStringList defaultCleanupCategoryIds();

private:
    void rebuild();

    QTreeWidget* tree_ = nullptr;
    QVector<pdf::HiddenDataCategory> categories_;
};

}  // namespace aipdf::app
