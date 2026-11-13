#pragma once

#include <QHash>
#include <QObject>
#include <QVector>

#include "pdf/Annotation.h"

namespace aipdf::doc {

// 批注模型：一份文档里所有批注的集合。
//
// 模型只管「当前状态」，撤销栈管「怎么走到这个状态的」。
// 两者分开，撤销/重做就不必让模型自己记历史，
// 模型也能单独被序列化、被测试、被别的视图读。
class AnnotationModel : public QObject {
    Q_OBJECT

public:
    explicit AnnotationModel(QObject* parent = nullptr);

    // 清空（换文档时用）。
    void clear();

    // 分配一个新的批注 id。已删除的批注的 id 不会被重复使用。
    quint64 allocateId() const;

    // 添加批注。id 为 0 或与已有批注重复时返回 false。
    bool add(const pdf::Annotation& annotation);
    // 按 id 删除。找不到返回 false。
    bool remove(quint64 id);
    // 按 id 替换整条批注（改颜色、改正文用）。找不到返回 false。
    bool update(const pdf::Annotation& annotation);

    // 按 id 查批注。找不到返回 nullptr。
    const pdf::Annotation* find(quint64 id) const;
    // 某页的批注（按插入顺序）。
    QVector<pdf::Annotation> forPage(int pageIndex) const;
    // 全部批注（按插入顺序）。
    QVector<pdf::Annotation> all() const;
    int count() const;
    int countForPage(int pageIndex) const;

    // 页面结构变了：按「旧页序号 → 新页序号」重映射批注的页码。
    //
    // pageMapping 里是 -1 的页表示被删掉了，那几页上的批注一并删掉；
    // 其余批注跟着页面搬到新页码。
    // 整个模型按「整体重置」通知一次：页码变了，投影要重做。
    void remapPages(const QVector<int>& pageMapping);

signals:
    // 某页的批注有变化；pageIndex 为 -1 表示整体重置。
    void changed(int pageIndex);

private:
    // 用哈希表按 id 存，插入顺序另外记一份：
    // 批注面板要按创建顺序列出来，哈希表的顺序是不确定的。
    QHash<quint64, pdf::Annotation> annotations_;
    QVector<quint64> order_;
    mutable quint64 nextId_ = 1;
};

}  // namespace aipdf::doc
