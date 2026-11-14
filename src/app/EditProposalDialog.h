#pragma once

#include "tools/ProposalValidator.h"

#include <QDialog>

namespace aipdf::app {

// 编辑提案的确认框。
//
// 设计上只做一件事：把「模型打算改什么」摊开给用户看，然后等一个明确的答复。
// 默认按钮是「取消」——回车不该等于同意。
class EditProposalDialog : public QDialog {
    Q_OBJECT

public:
    explicit EditProposalDialog(const tools::Proposal& proposal, QWidget* parent = nullptr);
};

}  // namespace aipdf::app
