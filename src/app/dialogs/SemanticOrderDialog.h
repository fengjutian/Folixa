#pragma once

#include "i18n/Translate.h"
#include "pdf/compliance/SemanticReadingOrder.h"

#include <QDialog>
#include <QVector>

class QListWidget;
class QPushButton;

namespace aipdf::app {

// 语义级阅读顺序对话框：按阅读位置（槽位）列出语义树的元素，可以上下调整，
// 也可以把元素挪到**别的槽位**去。
//
// 为什么不依赖 PdfView：这一层只负责「把顺序摆出来、让人挪」，
// 读写文档是领域层的事。独立出来才能单测，也才能被别的入口复用。
//
// 跨页移动**是支持的**：语义元素在阅读顺序里的位置，与它的内容在哪一页
// 是两件事。把第 1 页的一个元素挪到第 2 页的槽位，读屏软件会先念第 1 页
// 剩下的内容、再念第 2 页的——而这个元素的内容仍然印在第 1 页上
// （它的 /Pg 与 MCID 都不变）。
//
// 界面上的六种操作：
//   * 上移 / 下移：与相邻的元素换位置（越过槽位边界时进入相邻槽位）；
//   * 移到最前 / 移到最后：整篇的首尾；
//   * 移到上一页末尾 / 移到下一页开头：跨页搬动的两个常用落点。
class SemanticOrderDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SemanticOrderDialog(const QVector<pdf::SemanticOrderSlot>& positions,
                                  QWidget* parent = nullptr);

    // 用户确认后的顺序（没动过就等于传进来的顺序）。
    QVector<pdf::SemanticOrderSlot> order() const;

    // 现在这份顺序能不能写回（元素一个不多、一个不少）。
    //
    // 为什么单独给一个函数：确认前的校验要用弹窗提示，
    // 而「这份顺序合不合法」本身是个能单测的判断。
    bool isOrderValid(QString* reason = nullptr) const;

protected:
    // 确认前校验：语义元素一个都不能少、也不能多。
    void accept() override;

private:
    void buildUi();
    void retranslateUi();
    // 上下移动当前选中的元素（delta = -1 上移，+1 下移）。
    void moveSelected(int delta);
    // 移到整篇的最前 / 最后。
    void moveToEdge(bool toFront);
    // 移到上一页的末尾 / 下一页的开头（按元素内容所在的物理页算）。
    void moveToAdjacentPage(bool previous);
    // 当前行是不是某一页里的元素（页标题不是）。
    bool isEntryRow(int row) const;
    // 按列表里的页标题重新认一遍每个元素属于哪个槽位。
    void refreshSlotOfRows();
    // 元素的编号（flatEntries_ 的下标）；页标题返回 -1。
    int entryIdOfRow(int row) const;

    QVector<pdf::SemanticOrderSlot> positions_;
    // 全部元素拍平（按槽位顺序）；列表项用它认人。
    QVector<pdf::SemanticOrderEntry> flatEntries_;
    QListWidget* list_ = nullptr;
    QPushButton* moveUpButton_ = nullptr;
    QPushButton* moveDownButton_ = nullptr;
    QPushButton* moveToTopButton_ = nullptr;
    QPushButton* moveToBottomButton_ = nullptr;
    QPushButton* moveToPreviousPageButton_ = nullptr;
    QPushButton* moveToNextPageButton_ = nullptr;
    i18n::TextBindings texts_;
};

}  // namespace aipdf::app
