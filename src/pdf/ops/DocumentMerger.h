#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 一份待合并的源文件。
//
// bytes 是文件内容（整份读进内存：合并要在内存里重写页树与书签，
// 没有「边读边合」这种写法）。
// sourceName 是来源标识，只用来给搬过来的书签标题加前缀，
// 让用户在合并后的书签里一眼看出「这一条来自哪份文件」。
struct MergeInput {
    QByteArray bytes;
    QString sourceName;
};

// 合并选项。
struct MergeOptions {
    // 搬运源文件的书签（大纲）。
    bool copyOutlines = true;
    // 搬过来的书签标题加「文件名 · 」前缀，避免几份文件都有「第一章」时重名。
    bool outlinePrefix = true;
    // 重定向源文件里的内部跳转（「见第 N 页」这类指向本页的链接）。
    bool fixInternalLinks = true;
};

// 合并结果。
//
// bytes 是合并后的 PDF；失败时为空。
// pageOffsets[i] 是第 i 份源文件在合并结果里的**起始页号**（0 基），
// 也就是「第 i 份文件的第 0 页」在合并结果里落在哪一页。
// 有了它，调用方就能把「源页号」换算成「合并结果里的页号」。
struct MergeOutcome {
    QByteArray bytes;
    QVector<int> pageOffsets;
    int pageCount = 0;
    QString error;
};

// 源文件里的一条书签（摊平后的样子）。
//
// level 是层级：0 是顶层，1 是顶层条目的子条目，以此类推。
// sourcePage 是这条书签指向的**源文件里的页号**（0 基）；
// 解析不出来（指向页外、目标是命名目的地没解析到）时为 -1。
// uri 非空表示这是外部跳转（URI 动作），此时 sourcePage 没有意义。
//
// 注意：名字不叫 OutlineEntry —— 仓库里 pdf::OutlineEntry 已经被
// 「大纲摊平后的条目」（Navigation.h）占用了，同名会让两个意思撞在一起。
struct SourceOutlineEntry {
    int level = 0;
    QString title;
    int sourcePage = -1;
    QString uri;
};

// 合并多份 PDF。
//
// 顺序即合并顺序：第一份在前，最后一份在后。
//
// 做三件事：
//   1) 把各份文件的页按顺序接在一起（用 qpdf 官方的页面拷贝，不自己拼内容流）；
//   2) 把各份文件自己的书签搬到合并结果里（层级照搬，标题可加来源名前缀）；
//   3) 把源文件里的内部跳转（「见第 N 页」）改指到合并结果里的页号。
//
// 失败返回 false：不产出半成品（不会「少合了几页却报告成功」），
// 原因写进 outcome->error 与 error 参数。
bool mergeDocuments(const QVector<MergeInput>& inputs,
                    const MergeOptions& options,
                    MergeOutcome* outcome,
                    QString* error = nullptr);

// 第 sourceIndex 份源文件里的第 sourcePage 页（0 基），
// 在合并结果里是第几页。越界（页号落在这份文件自己的区间之外）返回 -1。
//
// 只有 pageOffsets 时，最后一份文件的页数是未知的，
// 它的「越界」要靠下面那个带总页数的重载才判得出来。
int mergedPageIndex(const QVector<int>& pageOffsets, int sourceIndex, int sourcePage);

// 同上，但额外知道合并结果的总页数（totalPages）。
//
// 为什么要多一个参数：最后一份源文件的区间没有下界，
// 只看 pageOffsets 判不出「第 100 页」是不是越界；
// 知道总页数才能给出确定的答案。totalPages < 0 表示不知道。
int mergedPageIndex(const QVector<int>& pageOffsets,
                    int sourceIndex,
                    int sourcePage,
                    int totalPages);

// 读出某份文件的大纲条目（标题 + 指向的源页号），按文档顺序摊平。
//
// 供搬运与测试用：不需要知道大纲的内部结构，只想知道「有哪些条目、
// 各自指向哪一页」。
QVector<SourceOutlineEntry> readOutlineEntries(const QByteArray& bytes, QString* error = nullptr);

}  // namespace aipdf::pdf
