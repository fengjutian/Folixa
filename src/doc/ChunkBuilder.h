#pragma once

#include "doc/DocumentChunk.h"

#include <QRectF>
#include <QString>
#include <QVector>

namespace aipdf::doc {

// 一行文本。
//
// 坐标是 PDF user space（左下原点、单位点），与 TextChar.box 同一套。
struct ChunkLine {
    QString text;
    QRectF box;
    // 行的字高：标题的字通常比正文大，靠它认标题。
    double charHeight = 0.0;
};

// 分块器：把一页的行归成块。
//
// 规则都是可解释的（界面验收按同样的规则断言）：
//   1. 行距明显大于正文行距 → 换块（段落之间空了一行）；
//   2. 缩进明显变化 → 换块（列表项、引用块）；
//   3. 字高明显大于正文 → 标题块，层级由字高相对正文的大小定；
//   4. 块太长 → 按行切开；太短且不是标题 → 并到相邻块。
//
// 不做的事：不做语义判断（哪句是“结论”），那是模型的事；
// 这里只按排版特征切，保证同一份文档每次切出来的块一样。
class ChunkBuilder {
public:
    struct Options {
        // 行距超过正文行距的这个倍数就认为换段了。
        double paragraphGapFactor = 1.6;
        // 字高比正文大这么多倍算标题。
        double headingHeightFactor = 1.25;
        // 一个块的最大字数（超出按行切开）。
        int maxChunkChars = 800;
        // 小于这个字数的块（且不是标题）并进相邻块。
        int minChunkChars = 40;
        // 缩进变化超过这个点数就认为换块。
        double indentTolerance = 6.0;
        // 正文字高（点）：整篇文档统一给同一个值，标题层级才跨页一致。
        // 0 表示用本页行的中位数自己估。
        double referenceCharHeight = 0.0;
    };

    // 字高样本：某一行的字高与这一行的字符数。
    //
    // 字符数是权重：标题通常很短，正文很长，
    // 按字符数加权才能让正文当基准，而不是让标题把基准带偏。
    struct HeightSample {
        double charHeight = 0.0;
        int charCount = 0;
    };

    // 分块。lines 要按阅读顺序排列（同一页内从上到下）。
    //
    // startOrder 是这一页第一个块的文档内序号；返回的块序号从这里接着排。
    //
    // idPrefix 是块 id 的前缀（默认 "p<页序号>"）。索引重建时传页内容指纹的
    // 前几位，id 就与内容绑定：页码变了（前面插了一页）id 也不变，
    // 外部引用不会错位。
    //
    // 参数给全的那版是主实现；短的那版用默认参数。
    // 不能把 Options() 直接写成默认实参：Options 是本类的嵌套类，
    // 它的成员默认值在类还没定义完时不可用（GCC 直接报错）。
    static QVector<DocumentChunk> build(int pageIndex,
                                        const QVector<ChunkLine>& lines,
                                        const QString& sectionPath,
                                        int startOrder = 0);
    static QVector<DocumentChunk> build(int pageIndex,
                                        const QVector<ChunkLine>& lines,
                                        const QString& sectionPath,
                                        int startOrder,
                                        const Options& options,
                                        const QString& idPrefix = QString());

    // 词数：中文按字、英文按词。
    static int countWords(const QString& text);

    // 按字符数加权的字高中位数：当正文字高。
    //
    // 为什么不用普通中位数：一页上往往只有一两个大标题，普通中位数会被标题带走。
    // 例如「标题 18pt + 正文 14pt」，普通中位数是 18——正文被当成了标题。
    // 按字符数加权之后，谁的字符多谁说话权重就大，标题压不住正文。
    static double estimateBodyHeight(const QVector<HeightSample>& samples);
};

}  // namespace aipdf::doc
