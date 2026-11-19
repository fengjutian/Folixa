#pragma once

#include <QDialog>
#include <QVector>

#include "pdf/ops/StreamingEdit.h"

class QLabel;
class QListWidget;
class QPushButton;

namespace aipdf::app {

// 「跨文本块合并」的选块对话框：把当前页上的文本块列出来，让人挑要合并哪几块。
//
// 为什么要有这个对话框：跨块合并这件事**必须由人指定范围**。
// 程序自作主张把哪些块合并在一起，合并出来的段落就未必是用户想要的那一段。
// 列出来让人勾选，是最不容易出错的做法。
class TextBlockMergeDialog final : public QDialog {
    Q_OBJECT

public:
    explicit TextBlockMergeDialog(const QVector<pdf::ContentTextBlock>& blocks,
                                  QWidget* parent = nullptr);

    // 选中的块 id（按块号从小到大）。
    QVector<int> selectedBlockIds() const;

private:
    void updateButtons();

    QVector<pdf::ContentTextBlock> blocks_;
    QListWidget* list_ = nullptr;
    QLabel* hint_ = nullptr;
    QPushButton* okButton_ = nullptr;
};

}  // namespace aipdf::app
