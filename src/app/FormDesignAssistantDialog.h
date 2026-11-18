#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QString>
#include <QVector>

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
class FormDesignAssistantDialog final : public QDialog {
    Q_OBJECT

public:
    explicit FormDesignAssistantDialog(ipc::WorkerClient* client, QWidget* parent = nullptr);

    // 用户确认后的字段设计（按表格里的顺序）。
    QVector<DesignedFormField> fields() const;

private:
    void requestDesign();
    void handleEvent(const QString& requestId, const QString& event, const QJsonObject& payload);
    void handleFailed(const QString& requestId, const QString& message);
    void fillTable();
    void setBusy(bool busy);

    ipc::WorkerClient* client_ = nullptr;
    QString activeRequestId_;
    QString answer_;
    QVector<DesignedFormField> fields_;
    QPlainTextEdit* description_ = nullptr;
    QPlainTextEdit* raw_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* generateButton_ = nullptr;
    QPushButton* createButton_ = nullptr;
};

}  // namespace aipdf::app
