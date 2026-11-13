#pragma once

#include "ai/UploadPreview.h"

#include <QDialog>

namespace aipdf::app {

// 上传预览：云端请求发出前，把「发给谁、发多少、涉及哪些页」摆给用户看。
//
// 这不是一个提示框，而是模式的一部分：用户点「取消」时请求根本不会发出。
// 对话框里只出现数量与页码，不出现正文——正文已经在他的文档里了，
// 再抄一遍只会多一个泄露面。
class UploadPreviewDialog final : public QDialog {
    Q_OBJECT

public:
    explicit UploadPreviewDialog(const ai::UploadPreview& preview, QWidget* parent = nullptr);

private:
    ai::UploadPreview preview_;
};

}  // namespace aipdf::app
