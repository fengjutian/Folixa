#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

#include "ai/AiContentDetection.h"
#include "ai/AiImageGeneration.h"
#include "ai/PptGeneration.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTextBrowser;

namespace aipdf::app {

class AiToolService;

// 文生图：提示词 + 风格 + 张数 → 生成 → 预览 → 插到当前页。
//
// 解析失败（ok=false）时把领域层给的中文原因显示出来，
// **不能当成生成成功**。
class ImageGenerationDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ImageGenerationDialog(AiToolService* service, QWidget* parent = nullptr);

    // 按界面上的参数构造一次请求（领域层负责规范化与夹取）。
    ai::ImageGenerationRequest buildRequest() const;
    // 按上面那次请求拼出请求体（领域层负责协议细节）。
    QByteArray buildPayload() const;
    // 请求能不能发；不能发时 reason 是中文原因。
    bool canSend(QString* reason = nullptr) const;

    // 收到一张生成好的图。
    void addGeneratedImage(const QByteArray& bytes, const QString& mimeType);
    // 已经拿到几张图。
    int generatedCount() const;
    // 当前选中的那张图的字节（没有选中时为空）。
    QByteArray currentImageBytes() const;
    // 状态行文字。
    QString statusText() const;

signals:
    // 用户要把这张图插到当前页。
    void insertRequested(const QByteArray& bytes);

private:
    void refreshPreview();

    AiToolService* service_ = nullptr;
    QPlainTextEdit* promptEdit_ = nullptr;
    QLineEdit* negativeEdit_ = nullptr;
    QLineEdit* styleEdit_ = nullptr;
    QComboBox* sizeBox_ = nullptr;
    QSpinBox* countSpin_ = nullptr;
    QLabel* preview_ = nullptr;
    QListWidget* resultList_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* generateButton_ = nullptr;
    QVector<QByteArray> images_;
};

// 总结配图：先把正文压成要点，再给每条要点配一张图。
class SummaryImageDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SummaryImageDialog(AiToolService* service, QWidget* parent = nullptr);

    // 待总结的正文（由调用方给：当前页或整篇）。
    QString documentText() const;
    void setDocumentText(const QString& text);
    // 用户的画面要求（例如「偏商务风格」）。
    QString instruction() const;
    void setInstruction(const QString& value);

    // 按界面上的内容排一个配图计划（领域层负责要点与提示词）。
    ai::SummaryImagePlan buildPlan() const;
    // 显示出来的计划。
    void setPlan(const ai::SummaryImagePlan& plan);
    // 计划里的要点。
    QStringList keyPoints() const;
    // 计划里的图像提示词。
    QStringList imagePrompts() const;
    // 状态行文字。
    QString statusText() const;

private:
    AiToolService* service_ = nullptr;
    QPlainTextEdit* textEdit_ = nullptr;
    QLineEdit* instructionEdit_ = nullptr;
    QListWidget* pointList_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    ai::SummaryImagePlan plan_;
};

// AI 生成 PPT：三种内容来源（主题 / 文档 / 大纲）共用一个对话框。
class PptGenerationDialog final : public QDialog {
    Q_OBJECT

public:
    explicit PptGenerationDialog(AiToolService* service, QWidget* parent = nullptr);

    // 内容来源。
    ai::PptSource source() const;
    void setSource(ai::PptSource source);
    // 主题（Topic 来源用）。
    QString topic() const;
    void setTopic(const QString& value);
    // 文档正文（Document 来源用）。
    QString documentText() const;
    void setDocumentText(const QString& text);
    // 大纲（Outline 来源用）。
    QString outlineText() const;
    void setOutlineText(const QString& text);
    // 附加要求。
    QString instruction() const;
    // 最多多少页。
    int maxSlides() const;
    void setMaxSlides(int count);

    // 生成出来的整套 PPT。
    ai::PptDeck deck() const;
    void setDeck(const ai::PptDeck& deck);
    // 按当前来源与参数生成一套 PPT（模型调用是阻塞的）。
    void generateDeck();
    // 预览用的 Markdown（deckToMarkdown）。
    QString markdown() const;
    // 状态行文字。
    QString statusText() const;

signals:
    // 用户要把整套 PPT 存成 Markdown 文件。
    void saveMarkdownRequested(const QString& markdown);

private:
    AiToolService* service_ = nullptr;
    QComboBox* sourceBox_ = nullptr;
    QLineEdit* topicEdit_ = nullptr;
    QPlainTextEdit* documentEdit_ = nullptr;
    QPlainTextEdit* outlineEdit_ = nullptr;
    QLineEdit* instructionEdit_ = nullptr;
    QSpinBox* slideSpin_ = nullptr;
    QListWidget* slideList_ = nullptr;
    QTextBrowser* preview_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    ai::PptDeck deck_;
};

// AI 内容检测：对一段文字给出判定。
//
// 解析不出来时显示「无法判断」，**不显示「完全人工」**。
class DetectionDialog final : public QDialog {
    Q_OBJECT

public:
    explicit DetectionDialog(AiToolService* service, QWidget* parent = nullptr);

    // 待检测的文本。
    QString sourceText() const;
    void setSourceText(const QString& text);

    // 领域层给的判定。
    ai::DetectionVerdict verdict() const;
    void setVerdict(const ai::DetectionVerdict& verdict);
    // 判定在界面上怎么写。
    QString verdictText() const;
    // 状态行文字。
    QString statusText() const;

private:
    AiToolService* service_ = nullptr;
    QPlainTextEdit* textEdit_ = nullptr;
    QLabel* verdictLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    ai::DetectionVerdict verdict_;
};

}  // namespace aipdf::app
