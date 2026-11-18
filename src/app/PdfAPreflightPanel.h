#pragma once

#include "pdf/compliance/PdfAChecker.h"

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QTreeWidget>

class QLabel;

namespace aipdf::app {

// 一个结论码对应的中文标题。
//
// 为什么要在 app 层做这张表：领域层按仓库约束只给「码 + ASCII 技术细节」
// （src/pdf/compliance/PdfAChecker.h），中文句子归界面拼。码是稳定的，文案会随语言变，
// 所以界面拿码查这张表，而不是拿文案去猜。
//
// 认不出来的码原样返回（显示成码本身），不编一句。
QString pdfAPreflightCodeTitle(const QString& code);

// 一行结论：「[条款] 标题：说明（建议：…）」。
QString pdfAPreflightFindingLine(const pdf::PdfAPreflightFinding& finding);

// 顶部结论行。
//
// 注意用词：这里**只能**说「预检发现 N 个问题」，不能说「已通过 PDF/A 校验」——
// 本模块是预检，不是认证（真正的认证要 veraPDF）。
QString pdfAPreflightConclusionText(const pdf::PdfAPreflightReport& report);

// 「这是预检不是认证」的说明。
QString pdfAPreflightScopeNoticeText();

// PDF/A-2b 预检结果对话框。
class PdfAPreflightDialog final : public QDialog {
    Q_OBJECT

public:
    explicit PdfAPreflightDialog(QWidget* parent = nullptr);

    void setReport(const pdf::PdfAPreflightReport& report);

    // 顶部结论。
    QString conclusionText() const;
    // 边界说明（只做预检，不出认证结论）。
    QString scopeNoticeText() const;
    // 全部结论行。
    QStringList findingLines() const;
    int passCount() const;
    int warningCount() const;
    int errorCount() const;

private:
    void rebuild();

    pdf::PdfAPreflightReport report_;
    QLabel* conclusionLabel_ = nullptr;
    QLabel* scopeLabel_ = nullptr;
    QTreeWidget* tree_ = nullptr;
};

}  // namespace aipdf::app
