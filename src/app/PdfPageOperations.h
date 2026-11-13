#pragma once

#include <QVector>

#include "doc/EditContext.h"
#include "pdf/PdfiumAdapter.h"

namespace aipdf::app {

// PDFium 版页面操作：命令层要的页面结构操作，落到 PDFium 文档上。
//
// 与控制器的分工：
// - 控制器（应用层）负责「用户动作」：进撤销栈、发通知；
// - 这里负责「原始操作」：直接改文档，返回页序号映射。
//
// 页面操作用到的 PDFium 事实（实测见 build/probe/page_probe.cpp）：
//   - 插入空白页与旋转页对大纲没有影响；
//   - 删除页只让指向被删页的那一条书签失效；
//   - 移动页的实现是「导入副本 + 删除原件」，
//     被移动页上的内部链接目标会失效，PDFium 没有公开接口能修回来。
class PdfPageOperations final : public doc::PageOperations {
public:
    explicit PdfPageOperations(pdf::PdfDocument document);

    bool insertBlankPage(int index,
                         double width,
                         double height,
                         QVector<int>* pageMapping) override;
    bool deletePage(int index, QVector<int>* pageMapping) override;
    bool setPageRotation(int index, int quarterTurns) override;
    bool movePage(int from, int to, QVector<int>* pageMapping) override;

    bool backupPage(int index, std::shared_ptr<doc::PageBackup>* backup) override;
    bool restorePage(int index, const doc::PageBackup& backup) override;

    int pageCount() override;

private:
    pdf::PdfDocument document_;
};

}  // namespace aipdf::app
