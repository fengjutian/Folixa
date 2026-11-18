#include "app/layout/PageTextLines.h"

#include <algorithm>

namespace aipdf::app {

QVector<UserSpaceLine> userSpaceTextLines(const pdf::TextPage& text) {
    // 一个字符在 PDF user space 里的竖直区间是 [下边, 上边]。
    // QRectF 存的是 (x, y=下边, w, h)，所以 QRectF::top() 其实是**下边**、
    // ::bottom() 是**上边**——跟屏幕坐标的直觉相反。
    // 下面一律用 lower/upper 命名，不再直接用 top()/bottom()，免得读反。
    //
    // 也不要碰 QRectF::intersected()/united()：它们假定 top() <= bottom()，
    // 对这种「y 是下边」的矩形会直接算成空矩形（这正是旧实现把一行拆成
    // 「每个字一行」的根因）。
    struct Piece {
        QRectF box;
        QString text;
        double center = 0.0;
        double extent = 0.0;  // 字符高度
    };

    QVector<Piece> pieces;
    for (const pdf::TextChar& character : text.chars) {
        // 只丢空字符与空框的字符：空格要留着，
        // 否则行里的文字会粘成一坨（「更改区域顺序」对话框里就没法看了）。
        if (character.text.isEmpty() || character.box.isEmpty()) {
            continue;
        }
        Piece piece;
        piece.box = character.box;
        piece.text = character.text;
        piece.center = (character.box.top() + character.box.bottom()) / 2.0;
        piece.extent = qMax(character.box.height(), 1.0);
        pieces.append(piece);
    }
    if (pieces.isEmpty()) {
        return {};
    }

    // 按竖直中心从高到低排：PDF 的 y 向上，中心越高越靠上。
    // 排完再聚类，簇的先后顺序就是「从上到下」的阅读顺序。
    std::sort(pieces.begin(), pieces.end(), [](const Piece& left, const Piece& right) {
        if (left.center != right.center) {
            return left.center > right.center;
        }
        return left.box.left() < right.box.left();
    });

    // 贪心聚类：与上一簇的中心差不超过半个行高就算同一行。
    // 这条规则和 app/TextLayout 的行聚类完全一致（选区与全文搜索都在用），
    // 两边算出来的行必须对得上，否则标签树的顺序会和用户看到的选区矛盾。
    struct Cluster {
        QVector<int> members;
        double center = 0.0;
        double extent = 0.0;
    };
    QVector<Cluster> clusters;
    for (int index = 0; index < pieces.size(); ++index) {
        const Piece& piece = pieces.at(index);
        if (!clusters.isEmpty()) {
            Cluster& last = clusters.last();
            const double tolerance = 0.5 * qMax(last.extent, piece.extent);
            if (qAbs(piece.center - last.center) <= tolerance) {
                const double count = static_cast<double>(last.members.size());
                last.center = (last.center * count + piece.center) / (count + 1.0);
                last.extent = qMax(last.extent, piece.extent);
                last.members.append(index);
                continue;
            }
        }
        Cluster cluster;
        cluster.center = piece.center;
        cluster.extent = piece.extent;
        cluster.members.append(index);
        clusters.append(cluster);
    }

    QVector<UserSpaceLine> lines;
    for (Cluster& cluster : clusters) {
        // 行内按从左到右排。
        std::sort(cluster.members.begin(), cluster.members.end(), [&pieces](int left, int right) {
            return pieces.at(left).box.left() < pieces.at(right).box.left();
        });
        double lower = 0.0;
        double upper = 0.0;
        double left = 0.0;
        double right = 0.0;
        bool first = true;
        for (int member : cluster.members) {
            const Piece& piece = pieces.at(member);
            const double pieceLower = piece.box.top();
            const double pieceUpper = piece.box.bottom();
            if (first) {
                lower = pieceLower;
                upper = pieceUpper;
                left = piece.box.left();
                right = piece.box.right();
                first = false;
                continue;
            }
            lower = qMin(lower, pieceLower);
            upper = qMax(upper, pieceUpper);
            left = qMin(left, piece.box.left());
            right = qMax(right, piece.box.right());
        }
        UserSpaceLine line;
        // 行的盒子用 PDF 约定：y 是下边、高度向上为正。
        // pdf::TaggedRegion 收的也是这种矩形，写进标签树才对得上。
        line.box = QRectF(left, lower, right - left, upper - lower);
        for (int member : cluster.members) {
            line.text += pieces.at(member).text;
        }
        lines.append(line);
    }
    return lines;
}

}  // namespace aipdf::app
