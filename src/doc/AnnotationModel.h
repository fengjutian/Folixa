#pragma once

#include <QHash>
#include <QObject>
#include <QVector>

#include "pdf/annot/Annotation.h"

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

    // 打开文档时把文件里已有的批注灌进模型。
    //
    // 与 add() 的区别：
    //   * 不发 changed 信号——灌入不是用户操作，不能触发「模型 → 文档」的
    //     反向投影（否则一打开文档就会重写整页批注）；
    //   * 已存在的 id 不覆盖；
    //   * id 计数器抬到 max(id)+1，避免本会话新建的批注与文件里的旧 id 撞号
    //     （撞号会让保存时把外观写到另一条批注上）。
    void seed(const QVector<pdf::Annotation>& annotations);

    // 把 id 计数器抬到至少 id+1。
    //
    // 给“文件里有、但本会话不接管”的批注用：它们的 id 仍占着，新建批注不能
    // 复用，否则保存时 DocumentWriter 会按 id 把新批注的外观写到旧批注上。
    void reserveId(quint64 id);

    // 把某一页的批注标记为「文档里已经没有外观流」。
    //
    // 同步到 PDFium 之后，本程序写的批注会被重建，/AP 随之丢失；显示层必须
    // 自己再画一遍。不发 changed 信号：调用方正在做投影，不需要再触发一次。
    void clearAppearanceForPage(int pageIndex);
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
