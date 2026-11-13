#pragma once

#include <QVector>

#include <memory>

namespace aipdf::doc {

class AnnotationModel;

// 一页的备份（撤销删除页时用）。
//
// 备份的具体内容由应用层决定（PDFium 的临时文档），
// 命令层只负责把它带着走。
class PageBackup {
public:
    virtual ~PageBackup() = default;
};

// 页面结构操作。
//
// 命令不直接碰 PDFium：它只说要「在第几页插入一张空白页」，
// 怎么做由应用层决定。这样命令层不依赖 PDFium，
// 命令也能在测试里用假的实现跑。
class PageOperations {
public:
    virtual ~PageOperations() = default;

    // 在 index 处插入一张 width×height（点）的空白页。
    // pageMapping 是「旧页序号 → 新页序号」。
    virtual bool insertBlankPage(int index,
                                 double width,
                                 double height,
                                 QVector<int>* pageMapping) = 0;
    // 删除第 index 页。
    virtual bool deletePage(int index, QVector<int>* pageMapping) = 0;
    // 设置第 index 页自身的旋转（四分之一圈：0=0°，1=90°，2=180°，3=270°）。
    virtual bool setPageRotation(int index, int quarterTurns) = 0;
    // 把第 from 页移动到第 to 页（to 是移动之后的目标下标）。
    virtual bool movePage(int from, int to, QVector<int>* pageMapping) = 0;

    // 备份一页（撤销删除时用）。
    virtual bool backupPage(int index, std::shared_ptr<PageBackup>* backup) = 0;
    // 把备份的页恢复到 index 位置。
    virtual bool restorePage(int index, const PageBackup& backup) = 0;

    // 当前页数。
    virtual int pageCount() = 0;
};

// 编辑上下文：命令执行时能碰到的东西。
//
// 命令不直接持有控制器，而是通过上下文拿模型与页面操作：
// - 命令因此可以序列化（恢复日志、审计记录、别处重放）；
// - 数据层不会因为命令而反向依赖应用层的控制器。
//
// 批注命令走 annotationModel()，页面命令走 pages()，
// 页面结构变了再通过 pageStructureChanged() 通知上下文收拾残局。
class EditContext {
public:
    virtual ~EditContext() = default;

    // 批注模型。命令用它读写批注。
    virtual AnnotationModel* annotationModel() = 0;
    // 页面操作入口。没有页面操作时返回 nullptr。
    virtual PageOperations* pages() = 0;
    // 页面结构变了（插入、删除、移动页面）。
    //
    // pageMapping 是「旧页序号 → 新页序号」，被删掉的页是 -1。
    // 实现要做的事：把批注的页码重映射、清掉按页缓存、通知界面重排。
    virtual void pageStructureChanged(const QVector<int>& pageMapping) = 0;
};

}  // namespace aipdf::doc
