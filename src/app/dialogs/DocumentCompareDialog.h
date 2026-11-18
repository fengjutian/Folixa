#pragma once

#include "i18n/Translate.h"
#include "pdf/ops/DocumentComparer.h"

#include <QDialog>
#include <QString>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;

namespace aipdf::app {

// 文档比较对话框：选两份 PDF，逐页比对文字块，列出差异。
//
// 这一层只做界面：比较本身在 pdf::compareDocuments 里。
// 界面的职责只有三件：收参数、把结构化结果摆成表、把「用户点了哪一条」
// 变成一个可以跳转的信号。
//
// 每条差异都带页码与原文片段：表格里直接显示原文与新文，
// 用户不打开原始文件就能核对；双击某一行会发 navigateToPage，
// 由上层跳到那一页。
class DocumentCompareDialog final : public QDialog {
    Q_OBJECT

public:
    explicit DocumentCompareDialog(QWidget* parent = nullptr);

    // 预填两份文件。Ribbon 入口可以把「当前打开的文档」和用户选的文件填进来。
    void setFiles(const QString& oldPath, const QString& newPath);
    // 当前填着的两份文件。
    QString oldFilePath() const;
    QString newFilePath() const;

    // 比较设置（两份文件各自的打开密码等）。
    void setOptions(const pdf::DocumentCompareOptions& options);
    pdf::DocumentCompareOptions options() const;

    // 跑一次比较。返回是否成功；失败原因在 lastError() 里（中文）。
    //
    // 这一步是同步的：比较的是文字块，不是像素，大文档也要跑一会儿，
    // 所以调用前应该先上等待光标。
    bool runComparison();

    // 上一次比较的结果（没跑过时 succeeded 为 false）。
    pdf::DocumentCompareResult lastResult() const;
    // 上一次比较的失败原因（中文）。成功时为空。
    QString lastError() const;

    // 表格第 row 行对应的差异。行号越界时返回 nullptr。
    const pdf::CompareDifference* differenceAt(int row) const;
    // 表格里的行数。
    int differenceRowCount() const;

    // 视觉差异表格里的第 row 行。行号越界时返回 nullptr。
    const pdf::VisualPageDifference* visualDifferenceAt(int row) const;
    // 视觉差异表格里的行数。
    int visualDifferenceRowCount() const;

    // 跳到第 row 行对应的差异：发出 navigateToPage。
    //
    // 双击表格某一行走的也是这条路径；单独暴露出来是为了让「跳到差异」
    // 这样的按钮与自动测试都能用同一套逻辑。
    void activateDifference(int row);

    // 差异种类对应的中文名（表格第一列用）。
    static QString diffKindName(pdf::DiffKind kind);

signals:
    // 用户点了某条差异：请求跳到对应页。
    //
    // pageIndex 是页号（0 基）；snippet 是这一处的文字片段。
    // 新文里没有对应页时 pageIndex 是原文页号，snippet 是原文片段。
    void navigateToPage(int pageIndex, const QString& snippet);

private:
    void buildUi();
    void retranslateUi();
    void browseOldFile();
    void browseNewFile();
    void updateSummary();
    void onTableActivated(int row, int column);
    // 差异在表格里显示的页号：新文有就用新文页号，否则用原文页号。
    static int displayPageOf(const pdf::CompareDifference& difference);

    QLineEdit* oldFileEdit_ = nullptr;
    QLineEdit* newFileEdit_ = nullptr;
    QPushButton* browseOldButton_ = nullptr;
    QPushButton* browseNewButton_ = nullptr;
    QPushButton* compareButton_ = nullptr;
    QCheckBox* pixelCompareCheck_ = nullptr;
    // 「自动对齐」开关：插页 / 删页后按内容配对，而不是按页码位置硬对。
    QCheckBox* autoAlignCheck_ = nullptr;
    QLabel* summary_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QTableWidget* table_ = nullptr;
    QTableWidget* visualTable_ = nullptr;

    pdf::DocumentCompareOptions options_;
    pdf::DocumentCompareResult lastResult_;
    QString lastError_;
    i18n::TextBindings texts_;
};

}  // namespace aipdf::app
