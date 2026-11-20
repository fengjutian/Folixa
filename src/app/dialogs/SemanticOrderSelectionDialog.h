#pragma once

#include "i18n/Translate.h"
#include "pdf/compliance/SemanticOrderSelection.h"

#include <QByteArray>
#include <QDialog>
#include <QRectF>
#include <QVector>

class QCheckBox;
class QLabel;
class QListWidget;

namespace aipdf::app {

// 「应用区域阅读顺序」的选区对话框：挑哪些页（或者页上的哪一块区域），
// 预览选区里的语义元素按视觉顺序会怎么排，确认之后才写回文档。
//
// 为什么要有这个对话框：这条命令以前是「整篇重建语义标签树」，
// 手工调过的顺序会被覆盖。现在它只作用**选区**，
// 选区由用户在这里挑——不挑就没有选区，什么也不做。
//
// 界面上的三块：
//   * 勾选要参与的页（只列出文档里有语义元素的页）；
//   * 「只作用于选中的文字区域」：页面上有文字选区时可用，
//     勾上以后选区就是那一块，而不是整页；
//   * 预览：选区里的语义元素、它们当前的位置与视觉名次。
class SemanticOrderSelectionDialog final : public QDialog {
    Q_OBJECT

public:
    // bytes：文档字节（对话框用它算选区里的元素与视觉顺序，做实时预览）。
    // currentPage：默认勾选的页（0 基）。
    // regionPage / region：可选的区域限制（PDF user space）；
    //        regionPage < 0 表示没有区域限制，「只作用于选中的文字区域」不可用。
    SemanticOrderSelectionDialog(const QByteArray& bytes, int currentPage, int regionPage,
                                  const QRectF& region, QWidget* parent = nullptr);

    // 用户确认的选区。
    pdf::SemanticOrderScope scope() const;

private:
    void buildUi();
    void retranslateUi();
    // 按当前的勾选状态重算选区，并刷新预览。
    void refreshPreview();
    // 当前界面状态对应的选区。
    pdf::SemanticOrderScope currentScope() const;

    QByteArray bytes_;
    int currentPage_ = 0;
    int regionPage_ = -1;
    QRectF region_;

    QListWidget* pageList_ = nullptr;
    QCheckBox* regionOnly_ = nullptr;
    QListWidget* elementList_ = nullptr;
    QLabel* summary_ = nullptr;
    i18n::TextBindings texts_;
};

}  // namespace aipdf::app
