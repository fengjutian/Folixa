#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QSizeF>
#include <QString>
#include <QVector>

#include "pdf/forms/FormAutoDetect.h"
#include "pdf/forms/FormDesignParser.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

namespace aipdf::ipc {
class WorkerClient;
}

namespace aipdf::app {

// 设计出来的一个字段现在住在 pdf 层（解析逻辑要能单独测）。
//
// 这里保留别名：调用点写的是 aipdf::app::DesignedFormField，
// 为了搬家去改别人的代码不划算。类型本身是同一个，别名不是新类型。
using DesignedFormField = pdf::DesignedFormField;

// 「设计助手」对话框：写一句描述，让模型给出表单字段设计，用户确认后再建域。
//
// 为什么模型只给"设计"不给"操作"：模型说的坐标与字段名都可能不合用，
// 必须由人在表格里看过、改过，才变成真正的建域动作。
//
// 版面（绝对位置与跨页）是**确定性计算**出来的（pdf::layoutDesignedFields），
// 不是模型给的：模型只给字段清单，位置由这一层按页面尺寸算，
// 放不下就顺延到下一页。这样同一份清单永远排出同一个版面。
class FormDesignAssistantDialog final : public QDialog {
    Q_OBJECT

public:
    explicit FormDesignAssistantDialog(ipc::WorkerClient* client, QWidget* parent = nullptr);

    // 带页面尺寸的构造。
    //
    // 给了页面尺寸，助手才会算版面：表格里显示每个字段的绝对位置（点）与页号，
    // 字段多于一页时顺延到下一页；没给就退回老行为（只用模型给的比例坐标）。
    //
    // pageSizes 是每一页的尺寸（顺序即页序），firstPageIndex 是起始页的页号。
    FormDesignAssistantDialog(ipc::WorkerClient* client, QWidget* parent,
                               const QVector<QSizeF>& pageSizes, int firstPageIndex);

    // 用户确认后的字段设计（按表格里的顺序）。
    //
    // 位置：给了页面尺寸时，相对坐标已按算出来的版面折算回本页比例；
    // 但跨页的字段要用 placements() 拿页号与绝对矩形——比例换算只对当前页成立。
    QVector<DesignedFormField> fields() const;

    // 用户确认后的版面：每个字段落在哪一页的哪个位置（页坐标，单位点）。
    // 没有页面尺寸时是空的（此时只有相对坐标可用）。
    QVector<pdf::PlacedFormField> placements() const;

    // 把自动识别出来的候选灌进表格。
    //
    // 候选的位置是识别出来的（页坐标），不再重排——重排会把"线框在哪"
    // 这条信息丢掉。用户确认之后，建域用的就是这些位置。
    void loadCandidates(const QVector<pdf::DetectedFormField>& candidates);

    // 现有页面不够，还需要补几页（0 表示够用）。
    int extraPagesNeeded() const;

private:
    void buildUi();
    void requestDesign();
    void handleEvent(const QString& requestId, const QString& event, const QJsonObject& payload);
    void handleFailed(const QString& requestId, const QString& message);
    void fillTable();
    void setBusy(bool busy);
    // 重算版面（模型给的字段走确定性排布；识别出来的候选位置固定）。
    void rebuildPlacements();
    // 有没有页面尺寸可用（有才排版面）。
    bool hasLayout() const { return !pageSizes_.isEmpty(); }
    // 某一页的尺寸；越界时返回空。
    QSizeF pageSizeOf(int pageIndex) const;

    ipc::WorkerClient* client_ = nullptr;
    QString activeRequestId_;
    QString answer_;
    QVector<DesignedFormField> fields_;
    // 识别出来的候选（位置固定，不参与重排）。
    QVector<pdf::PlacedFormField> fixedPlacements_;
    // 当前清单是不是"识别出来的候选"。
    bool detectedMode_ = false;
    // 当前版面。
    QVector<pdf::PlacedFormField> placements_;
    int extraPagesNeeded_ = 0;
    QVector<QSizeF> pageSizes_;
    int firstPageIndex_ = 0;

    QPlainTextEdit* description_ = nullptr;
    QPlainTextEdit* raw_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* generateButton_ = nullptr;
    QPushButton* createButton_ = nullptr;
};

}  // namespace aipdf::app
