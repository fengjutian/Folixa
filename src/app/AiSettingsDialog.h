#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace aipdf::app {

// 「AI 服务设置」对话框：选服务商、填地址与模型、存 API 密钥。
//
// 这个对话框只负责**收集**设置，不直接碰密钥存储。
// 密钥写入由 AiPanel 决定（这样对话框可以被无副作用地打开/取消）。
//
// 密钥字段永远不回显已存的值：只有「已保存 / 未保存」这个状态。
// 界面上也不显示密钥长度——那也是信息。
class AiSettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit AiSettingsDialog(QWidget* parent = nullptr);

    // 用现有配置填表。
    // hasKey：凭据管理器里已经存了这个服务商的密钥。
    void applyConfig(const QString& providerId,
                     const QString& baseUrl,
                     const QString& model,
                     bool hasKey,
                     double inputPricePerMillion,
                     double outputPricePerMillion,
                     int timeoutMs);

    QString providerId() const;
    QString baseUrl() const;
    QString model() const;
    // 用户这次新填的密钥。留空表示「不修改已存的密钥」。
    QString apiKey() const;
    // 用户要不要清掉已存的密钥。
    bool clearCredential() const;
    double inputPricePerMillion() const;
    double outputPricePerMillion() const;
    int timeoutMs() const;

private:
    // 演示模式时把联网相关字段灰掉：填了也没用，容易误导。
    void updateEnabledState();

    QComboBox* providerCombo_ = nullptr;
    QLineEdit* baseUrlEdit_ = nullptr;
    QLineEdit* modelEdit_ = nullptr;
    QLineEdit* apiKeyEdit_ = nullptr;
    QCheckBox* clearKeyBox_ = nullptr;
    QLabel* keyHint_ = nullptr;
    QDoubleSpinBox* inputPrice_ = nullptr;
    QDoubleSpinBox* outputPrice_ = nullptr;
    QSpinBox* timeout_ = nullptr;
    bool hasKey_ = false;
};

}  // namespace aipdf::app
