#include "app/TextLayout.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace aipdf::app {
namespace {

// CJK 表意文字、假名、谚文与全角标点：这些字符之间不插空格。
bool isCjkChar(QChar ch) {
    const ushort code = ch.unicode();
    return (code >= 0x4E00 && code <= 0x9FFF) ||    // 统一表意文字
           (code >= 0x3400 && code <= 0x4DBF) ||    // 扩展 A
           (code >= 0x3040 && code <= 0x30FF) ||    // 假名
           (code >= 0xAC00 && code <= 0xD7AF) ||    // 谚文
           (code >= 0x3000 && code <= 0x303F) ||    // CJK 标点
           (code >= 0xFF00 && code <= 0xFFEF);      // 全角形式
}

// 构成「词」的字符：字母、数字、下划线。
bool isWordChar(QChar ch) {
    return ch.isLetterOrNumber() || ch == QLatin1Char('_');
}

QChar firstChar(const LayoutChar& character) {
    return character.text.isEmpty() ? QChar() : character.text.at(0);
}

// 沿某个轴把字符聚成簇：行（按 y）或列（按 x）。
struct Cluster {
    double center = 0.0;
    double extent = 0.0;
    QVector<int> members;
};

QVector<Cluster> clusterByAxis(const QVector<LayoutChar>& chars,
                                const QVector<int>& indices,
                                bool byRow) {
    auto centerOf = [&chars, byRow](int index) {
        const QRectF& box = chars[index].box;
        return byRow ? box.center().y() : box.center().x();
    };
    auto extentOf = [&chars, byRow](int index) {
        const QRectF& box = chars[index].box;
        return byRow ? box.height() : box.width();
    };

    QVector<int> sorted = indices;
    std::sort(sorted.begin(), sorted.end(), [&centerOf](int a, int b) {
        const double ca = centerOf(a);
        const double cb = centerOf(b);
        if (ca != cb) {
            return ca < cb;
        }
        return a < b;
    });

    QVector<Cluster> clusters;
    for (int index : sorted) {
        const double center = centerOf(index);
        const double extent = qMax(extentOf(index), 1.0);
        if (!clusters.isEmpty()) {
            Cluster& last = clusters.last();
            const double tolerance = 0.5 * qMax(last.extent, extent);
            if (qAbs(center - last.center) <= tolerance) {
                const double count = static_cast<double>(last.members.size());
                last.center = (last.center * count + center) / (count + 1.0);
                last.extent = qMax(last.extent, extent);
                last.members.append(index);
                continue;
            }
        }
        Cluster cluster;
        cluster.center = center;
        cluster.extent = extent;
        cluster.members.append(index);
        clusters.append(cluster);
    }
    return clusters;
}

}  // namespace

void TextLayout::setChars(const QVector<LayoutChar>& chars) {
    chars_ = chars;
    rebuild();
}

void TextLayout::clear() {
    chars_.clear();
    order_.clear();
    lines_.clear();
    vertical_ = false;
}

bool TextLayout::isEmpty() const {
    return chars_.isEmpty();
}

int TextLayout::charCount() const {
    return chars_.size();
}

const QVector<int>& TextLayout::readingOrder() const {
    return order_;
}

const QVector<TextLayout::Line>& TextLayout::lines() const {
    return lines_;
}

bool TextLayout::isVertical() const {
    return vertical_;
}

void TextLayout::rebuild() {
    order_.clear();
    lines_.clear();
    vertical_ = false;
    if (chars_.isEmpty()) {
        return;
    }

    QVector<int> all;
    all.reserve(chars_.size());
    for (int i = 0; i < chars_.size(); ++i) {
        all.append(i);
    }

    const QVector<Cluster> rows = clusterByAxis(chars_, all, true);
    const QVector<Cluster> columns = clusterByAxis(chars_, all, false);
    // 列比行少 → 文本是竖排（列内从上到下、列间从右到左）。
    vertical_ = columns.size() < rows.size();

    QVector<Cluster> groups = vertical_ ? columns : rows;
    if (vertical_) {
        std::sort(groups.begin(), groups.end(), [](const Cluster& a, const Cluster& b) {
            return a.center > b.center;  // 竖排从右往左
        });
    }

    // 阅读顺序 + 行划分。
    QVector<int> positionOf(chars_.size(), -1);
    for (const Cluster& group : groups) {
        QVector<int> members = group.members;
        std::sort(members.begin(), members.end(), [this](int a, int b) {
            if (vertical_) {
                const double ta = chars_[a].box.top();
                const double tb = chars_[b].box.top();
                if (ta != tb) {
                    return ta < tb;
                }
                return a < b;
            }
            const double la = chars_[a].box.left();
            const double lb = chars_[b].box.left();
            if (la != lb) {
                return la < lb;
            }
            return a < b;
        });

        Line line;
        line.start = order_.size();
        for (int member : members) {
            positionOf[member] = order_.size();
            order_.append(member);
        }
        line.end = order_.size();
        if (line.end <= line.start) {
            continue;
        }
        QRectF box = chars_[order_[line.start]].box;
        for (int position = line.start + 1; position < line.end; ++position) {
            box = box.united(chars_[order_[position]].box);
        }
        line.box = box;
        lines_.append(line);
    }

    // 记录每个字符在阅读顺序中的位置（hitTest 用）。
    for (int charIndex = 0; charIndex < positionOf.size(); ++charIndex) {
        if (positionOf[charIndex] < 0) {
            positionOf[charIndex] = 0;
        }
    }
    // positionOf 只在 rebuild 里用到，这里转存成成员。
    orderPositions_ = positionOf;
}

int TextLayout::lineOfPosition(int orderPosition) const {
    // lines_ 是阅读顺序上的连续区间，二分查找即可。
    int low = 0;
    int high = lines_.size() - 1;
    while (low <= high) {
        const int middle = (low + high) / 2;
        const Line& line = lines_[middle];
        if (orderPosition < line.start) {
            high = middle - 1;
        } else if (orderPosition >= line.end) {
            low = middle + 1;
        } else {
            return middle;
        }
    }
    return -1;
}

int TextLayout::hitTest(const QPointF& point) const {
    int best = -1;
    double bestDistance = std::numeric_limits<double>::max();
    for (int charIndex = 0; charIndex < chars_.size(); ++charIndex) {
        const QRectF& box = chars_[charIndex].box;
        if (box.contains(point)) {
            return orderPositions_.value(charIndex, 0);
        }
        const double dx = qMax(box.left() - point.x(), 0.0) + qMax(point.x() - box.right(), 0.0);
        const double dy = qMax(box.top() - point.y(), 0.0) + qMax(point.y() - box.bottom(), 0.0);
        const double distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = charIndex;
        }
    }
    if (best < 0) {
        return -1;
    }
    // 点的位置离字形很近时也算命中，方便拖选。
    const double reference = qMax(chars_[best].box.height(), 1.0);
    const double tolerance = 0.5 * reference + 2.0;
    if (bestDistance > tolerance * tolerance) {
        return -1;
    }
    return orderPositions_.value(best, 0);
}

bool TextLayout::wordRangeAt(int orderIndex, int* start, int* end) const {
    if (orderIndex < 0 || orderIndex >= order_.size()) {
        return false;
    }
    int first = orderIndex;
    while (first > 0) {
        const QChar ch = firstChar(chars_[order_[first - 1]]);
        if (ch.isNull() || !isWordChar(ch)) {
            break;
        }
        --first;
    }
    int last = orderIndex + 1;
    while (last < order_.size()) {
        const QChar ch = firstChar(chars_[order_[last]]);
        if (ch.isNull() || !isWordChar(ch)) {
            break;
        }
        ++last;
    }
    if (start != nullptr) {
        *start = first;
    }
    if (end != nullptr) {
        *end = last;
    }
    return true;
}

QString TextLayout::textForRange(int start, int end) const {
    const int from = qMax(0, start);
    const int to = qMin(order_.size(), end);
    if (from >= to) {
        return {};
    }

    QString result;
    int previousPosition = -1;
    for (int position = from; position < to; ++position) {
        const LayoutChar& character = chars_[order_[position]];
        if (previousPosition >= 0) {
            const LayoutChar& previous = chars_[order_[previousPosition]];
            const bool sameLine = lineOfPosition(previousPosition) == lineOfPosition(position);
            if (!sameLine) {
                result += QLatin1Char('\n');
            } else {
                // 词间没有空格字符时，按几何间距补一个空格。
                const double gap = character.box.left() - previous.box.right();
                const double reference = qMax(previous.box.height(), character.box.height());
                const QChar previousChar = firstChar(previous);
                const QChar currentChar = firstChar(character);
                const bool cjkPair = !previousChar.isNull() && !currentChar.isNull() &&
                                     isCjkChar(previousChar) && isCjkChar(currentChar);
                const bool hasSpace = previous.text.endsWith(QLatin1Char(' ')) ||
                                      character.text.startsWith(QLatin1Char(' '));
                if (!cjkPair && !hasSpace && gap > 0.2 * reference) {
                    result += QLatin1Char(' ');
                }
            }
        }
        result += character.text;
        previousPosition = position;
    }
    return result;
}

QVector<QRectF> TextLayout::highlightRects(int start, int end) const {
    QVector<QRectF> rects;
    const int from = qMax(0, start);
    const int to = qMin(order_.size(), end);
    if (from >= to) {
        return rects;
    }
    for (const Line& line : lines_) {
        const int lineFrom = qMax(line.start, from);
        const int lineTo = qMin(line.end, to);
        if (lineFrom >= lineTo) {
            continue;
        }
        QRectF box = chars_[order_[lineFrom]].box;
        for (int position = lineFrom + 1; position < lineTo; ++position) {
            box = box.united(chars_[order_[position]].box);
        }
        rects.append(box);
    }
    return rects;
}

}  // namespace aipdf::app
