#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

#include "app/ConversionReports.h"
#include "pdf/ops/OcrSuspicion.h"
#include "pdf/convert/PortfolioWriter.h"
#include "pdf/compliance/PreflightChecker.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QTableWidget;
class QTreeWidget;

namespace aipdf::app {

// 印前检查：按严重程度分组显示结论。
//
// 「能不能送印」由领域层的 report.ok 决定，界面不重算错误条数。
class PreflightDialog final : public QDialog {
    Q_OBJECT

public:
    explicit PreflightDialog(QWidget* parent = nullptr);

    // 填入一份报告。
    void setReport(const pdf::PreflightReport& report);
    // 领域层给的原始报告。
    const pdf::PreflightReport& report() const;

    // 三组各显示了多少条。
    int errorCount() const;
    int warningCount() const;
    int infoCount() const;
    // 三组加起来一共显示了多少条。
    int displayedTotal() const;
    // 领域层报告里的错误条数与警告条数（只显示，不重算）。
    int domainErrorCount() const;
    int domainWarningCount() const;
    // 顶部结论文字。
    QString conclusionText() const;

private:
    void rebuild();

    pdf::PreflightReport report_;
    PreflightGroups groups_;
    QLabel* conclusionLabel_ = nullptr;
    QLabel* errorStatus_ = nullptr;
    QLabel* warningStatus_ = nullptr;
    QLabel* infoStatus_ = nullptr;
    QTreeWidget* errorTree_ = nullptr;
    QTreeWidget* warningTree_ = nullptr;
    QTreeWidget* infoTree_ = nullptr;
};

// OCR 疑似错误：列出可疑字符。
//
// 这是**提示**不是自动改：界面里没有任何"替换"动作，
// 用户的原文一个字都不会被改写。
class OcrSuspicionDialog final : public QDialog {
    Q_OBJECT

public:
    explicit OcrSuspicionDialog(QWidget* parent = nullptr);

    void setSuspicious(const QVector<pdf::SuspiciousChar>& items);
    // 显示的行数。
    int rowCount() const;
    // 排好的行。
    QVector<SuspicionRow> rows() const;
    // 空状态时也有话说（空的列表控件在无障碍接口里查不到）。
    QString statusText() const;

private:
    void rebuild();

    QVector<SuspicionRow> rows_;
    QLabel* statusLabel_ = nullptr;
    QTableWidget* table_ = nullptr;
};

// PDF 文件包（活页夹）：把若干 PDF 收进一个包，或打开一个包看条目。
class PortfolioDialog final : public QDialog {
    Q_OBJECT

public:
    explicit PortfolioDialog(QWidget* parent = nullptr);

    // 加一个要收进包里的文件。
    void addSourceFile(const QString& filePath);
    // 待打包的条目（名字取文件名，说明留空）。
    QVector<pdf::PortfolioEntry> entries() const;

    // 写一个包；失败时返回空字节并把中文原因写进 error。
    QByteArray createPackage(QString* error = nullptr) const;

    // 打开一个包；失败时返回 false 并把中文原因写进 error。
    bool openPackage(const QString& path, QString* error = nullptr);
    // 包里的条目（名字 / 说明 / 大小）。
    QVector<PortfolioEntryRow> entryRows() const;
    // 取出第 index 个条目到目标文件。
    bool extractEntry(int index, const QString& targetPath, QString* error = nullptr);

private:
    void refreshEntries();

    QListWidget* sourceList_ = nullptr;
    QLineEdit* titleEdit_ = nullptr;
    QLineEdit* authorEdit_ = nullptr;
    QLineEdit* subjectEdit_ = nullptr;
    QTableWidget* entryTable_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    pdf::PortfolioReadResult opened_;
    bool openedOk_ = false;
};

// 转换与导出的动作集合在 ConversionExports.h 里（那边要链 PDFium 与 qpdf）。

}  // namespace aipdf::app
