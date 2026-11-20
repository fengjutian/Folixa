#pragma once

#include "i18n/Translate.h"
#include "pdf/compliance/SemanticReadingOrder.h"

#include <QDialog>
#include <QVector>

class QListWidget;
class QPushButton;

namespace aipdf::app {

// 语义级阅读顺序对话框：按页列出语义树的元素，可以在页内上下移动。
//
// 为什么不依赖 PdfView：这一层只负责「把顺序摆出来、让人挪」，
// 读写文档是领域层的事。独立出来才能单测，也才能被别的入口复用。
//
// 跨页移动不支持：语义元素的顺序只在**一页之内**有意义
// （第 2 页的第一个元素挪到第 1 页最后，读屏软件念出来会莫名其妙），
// 所以「上移 / 下移」只作用于同一页内的元素。
class SemanticOrderDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SemanticOrderDialog(const QVector<pdf::SemanticOrderPage>& pages,
                                  QWidget* parent = nullptr);

    // 用户确认后的顺序（没动过就等于传进来的顺序）。
    QVector<pdf::SemanticOrderPage> order() const;

protected:
    // 确认前校验：元素个数必须与传进来的一致——少一个就等于丢了一段内容。
    void accept() override;

private:
    void buildUi();
    void retranslateUi();
    // 上下移动当前选中的元素（delta = -1 上移，+1 下移）。
    void moveSelected(int delta);
    // 当前行是不是某一页里的元素（页标题不是）。
    bool isEntryRow(int row) const;

    QVector<pdf::SemanticOrderPage> pages_;
    QListWidget* list_ = nullptr;
    QPushButton* moveUpButton_ = nullptr;
    QPushButton* moveDownButton_ = nullptr;
    i18n::TextBindings texts_;
};

}  // namespace aipdf::app
