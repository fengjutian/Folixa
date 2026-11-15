#pragma once

#include <QString>
#include <QVector>

namespace aipdf::app {

// 检索选项。
struct SearchOptions {
    bool caseSensitive = false;
    // 全字匹配：命中区间两侧不能紧挨着西文单词字符（中文不参与，见实现说明）。
    bool wholeWord = false;
    // 正则匹配：query 按 QRegularExpression 解释；写坏了就当没命中，不抛给界面。
    bool regex = false;
    // 只在这些页里找；lastPage 为 -1 表示到最后一页。
    int firstPage = 0;
    int lastPage = -1;

    bool includesPage(int pageIndex) const {
        return pageIndex >= firstPage && (lastPage < 0 || pageIndex <= lastPage);
    }
};

// 一处命中：在 plain 文本里的 UTF-16 位置与长度。
struct TextMatch {
    int at = 0;
    int length = 0;

    bool operator==(const TextMatch& other) const {
        return at == other.at && length == other.length;
    }
};

// 在一段纯文本里找关键词。
//
// 单独拆出来是为了能脱离文档、线程与界面直接测：大小写和全字匹配这两条规则
// 很容易写错（尤其全字匹配，把中文当成"非单词"还是"单词"会影响结果），
// 塞进后台线程里就只能靠界面验收碰运气。
QVector<TextMatch> findTextMatches(const QString& plain,
                                   const QString& query,
                                   const SearchOptions& options);

}  // namespace aipdf::app
