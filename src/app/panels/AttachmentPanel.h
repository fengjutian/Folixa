#pragma once

#include "i18n/Translate.h"
#include "pdf/kernel/PdfiumAdapter.h"

#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;
class QTableWidget;

namespace aipdf::app {

// 附件面板：列出文档里的嵌入文件（名称 / 说明 / 大小），并提供
// 添加、打开、保存副本与删除。
//
// 面板自己不碰 PDF：它只把用户意图变成信号，真正的文档操作在 PdfView 上，
// 由主窗口接线。这样面板可以脱离 PDFium 单独构造与测试。
class AttachmentPanel final : public QWidget {
    Q_OBJECT

public:
    explicit AttachmentPanel(QWidget* parent = nullptr);

    // 用一份新的附件列表刷新表格。没有附件时显示空状态。
    void setAttachments(const QVector<pdf::DocumentAttachment>& attachments);
    // 当前显示的附件条数（供验收与测试读取）。
    int count() const;
    // 选中行的附件名；没有选中时返回空字符串。
    QString selectedName() const;
    // 按名字定位并选中一行（测试与验收用）。
    bool selectAttachment(const QString& name);

signals:
    void addRequested();
    void openRequested(const QString& name);
    void saveRequested(const QString& name);
    void removeRequested(const QString& name);

private:
    void buildUi();
    void retranslateUi();
    void updateEmptyState();
    void updateButtons();
    void activateRow(int row);
    void requestRemove();
    void requestSave();
    void requestOpen();

    QVector<pdf::DocumentAttachment> entries_;
    QStackedWidget* stack_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* emptyTitle_ = nullptr;
    QLabel* emptyHint_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* openButton_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    i18n::TextBindings texts_;
};

}  // namespace aipdf::app
