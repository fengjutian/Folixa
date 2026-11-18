#pragma once

#include <QString>
#include <QVector>

#include "pdf/annot/Annotation.h"

namespace aipdf::pdf {

// 批注筛选条件。
//
// 为什么把筛选做成领域层的一个纯函数：批注面板上要按类型筛、
// 按关键字筛、按页筛，这些规则**必须和列表里显示的顺序一致**。
// 规则写在界面里的话，改一次就得同步改两处，很快就会对不上。
//
// 约定：
//   * 空条件 = 不筛（types 为空、keyword 为空、pageIndex 为负）。
//   * 关键字在**正文与作者**里找，不区分大小写。
struct AnnotationFilter {
    // 只保留这些类型；空 = 不按类型筛。
    QVector<AnnotationType> types;
    // 正文或作者里要包含的关键字；空 = 不筛。
    QString keyword;
    // 只看某一页；负数 = 不筛。
    int pageIndex = -1;
    // 是否包含回复（replyToId != 0 的那些）。
    bool includeReplies = true;
    // 只看回复（与 includeReplies 互斥：勾上它就只剩回复）。
    bool onlyReplies = false;
    // 是否要求批注有正文（把空正文的图章类批注滤掉）。
    bool requireContents = false;

    // 是否什么都没设（全空 = 不过滤）。
    bool isEmpty() const;
};

// 一条批注是否符合筛选条件。
bool matchesAnnotationFilter(const Annotation& annotation, const AnnotationFilter& filter);

// 按条件挑出批注（保持原来的顺序）。
//
// 拿不到 id 的批注（id == 0）一律保留：那是刚建好还没进模型的，
// 滤掉它等于凭空丢东西。
QVector<Annotation> filterAnnotations(const QVector<Annotation>& annotations,
                                       const AnnotationFilter& filter);

}  // namespace aipdf::pdf
