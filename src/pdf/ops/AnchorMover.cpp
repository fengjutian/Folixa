#include "pdf/ops/AnchorMover.h"

#include "pdf/content/ContentStream.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <memory>

namespace aipdf::pdf {
namespace {

struct Matrix {
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0, e = 0.0, f = 0.0;
};

Matrix multiply(const Matrix& left, const Matrix& right) {
    Matrix out;
    out.a = left.a * right.a + left.b * right.c;
    out.b = left.a * right.b + left.b * right.d;
    out.c = left.c * right.a + left.d * right.c;
    out.d = left.c * right.b + left.d * right.d;
    out.e = left.e * right.a + left.f * right.c + right.e;
    out.f = left.e * right.b + left.f * right.d + right.f;
    return out;
}

QPointF transform(const Matrix& matrix, double x, double y) {
    return QPointF(x * matrix.a + y * matrix.c + matrix.e,
                   x * matrix.b + y * matrix.d + matrix.f);
}

QRectF unitSquareBounds(const Matrix& matrix) {
    const QPointF points[4] = {transform(matrix, 0, 0), transform(matrix, 1, 0),
                               transform(matrix, 0, 1), transform(matrix, 1, 1)};
    QRectF bounds(points[0], points[0]);
    for (int i = 1; i < 4; ++i) {
        bounds.setLeft(qMin(bounds.left(), points[i].x()));
        bounds.setRight(qMax(bounds.right(), points[i].x()));
        bounds.setTop(qMin(bounds.top(), points[i].y()));
        bounds.setBottom(qMax(bounds.bottom(), points[i].y()));
    }
    return bounds;
}

int previousSignificant(const QVector<ContentToken>& tokens, int index) {
    for (int i = index - 1; i >= 0; --i) {
        if (tokens.at(i).type != ContentTokenType::Whitespace &&
            tokens.at(i).type != ContentTokenType::Comment) {
            return i;
        }
    }
    return -1;
}

int nextSignificant(const QVector<ContentToken>& tokens, int index) {
    for (int i = index + 1; i < tokens.size(); ++i) {
        if (tokens.at(i).type != ContentTokenType::Whitespace &&
            tokens.at(i).type != ContentTokenType::Comment) {
            return i;
        }
    }
    return -1;
}

// 取 index 之前连续的 count 个数字 token（返回 token 下标，按原顺序）。
QVector<int> lastNumberIndices(const QVector<ContentToken>& tokens, int index, int count) {
    QVector<int> indices;
    for (int i = index - 1; i >= 0 && indices.size() < count; --i) {
        if (tokens.at(i).type == ContentTokenType::Number) indices.append(i);
        else if (tokens.at(i).type != ContentTokenType::Whitespace &&
                 tokens.at(i).type != ContentTokenType::Comment) {
            break;
        }
    }
    std::reverse(indices.begin(), indices.end());
    return indices;
}

bool sameRect(const QRectF& left, const QRectF& right, double tolerance = 2.5) {
    return std::abs(left.left() - right.left()) < tolerance &&
           std::abs(left.top() - right.top()) < tolerance &&
           std::abs(left.width() - right.width()) < tolerance &&
           std::abs(left.height() - right.height()) < tolerance;
}

bool isPathConstruction(const QByteArray& op) {
    return op == "m" || op == "l" || op == "c" || op == "v" || op == "y" || op == "re" ||
           op == "h";
}

bool isPainting(const QByteArray& op) {
    return op == "S" || op == "s" || op == "f" || op == "F" || op == "f*" || op == "B" ||
           op == "B*" || op == "b" || op == "b*" || op == "n";
}

// 第一条路径的几何：点对的 token 下标 + 页面包围盒 + 构造时的 CTM。
struct PathObject {
    QVector<QPair<int, int>> points;
    QRectF bounds;
    Matrix matrix;
    bool hasPoint = false;
    bool used = false;
};

void includePoint(PathObject* path, double x, double y) {
    const QPointF point = transform(path->matrix, x, y);
    if (!path->hasPoint) {
        path->bounds = QRectF(point, point);
        path->hasPoint = true;
        return;
    }
    path->bounds.setLeft(qMin(path->bounds.left(), point.x()));
    path->bounds.setRight(qMax(path->bounds.right(), point.x()));
    path->bounds.setTop(qMin(path->bounds.top(), point.y()));
    path->bounds.setBottom(qMax(path->bounds.bottom(), point.y()));
}

void addPoint(PathObject* path, int xIndex, int yIndex, double x, double y) {
    includePoint(path, x, y);
    path->points.append({xIndex, yIndex});
}

QByteArray formatNumber(double value) {
    QByteArray text = QByteArray::number(value, 'f', 3);
    while (text.endsWith('0')) text.chop(1);
    if (text.endsWith('.')) text.chop(1);
    return text;
}

}  // namespace

AnchorMoveResult moveAnchoredGraphics(const QByteArray& pdfBytes,
                                      const QVector<AnchorImageMove>& images,
                                      const QVector<AnchorPathMove>& paths) {
    AnchorMoveResult result;
    if (images.isEmpty() && paths.isEmpty()) {
        result.bytes = pdfBytes;
        return result;
    }
    if (pdfBytes.isEmpty()) {
        result.errors.append(QStringLiteral("PDF 字节为空"));
        return result;
    }

    QHash<int, QVector<AnchorImageMove>> imagesByPage;
    for (const AnchorImageMove& move : images) imagesByPage[move.pageIndex].append(move);
    QHash<int, QVector<AnchorPathMove>> pathsByPage;
    for (const AnchorPathMove& move : paths) pathsByPage[move.pageIndex].append(move);

    QSet<int> pageSet;
    for (auto it = imagesByPage.constBegin(); it != imagesByPage.constEnd(); ++it) pageSet.insert(it.key());
    for (auto it = pathsByPage.constBegin(); it != pathsByPage.constEnd(); ++it) pageSet.insert(it.key());

    try {
        QPDF pdf;
        pdf.processMemoryFile("anchor-move.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        bool changed = false;

        for (int pageIndex : pageSet) {
            if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) {
                result.errors.append(QStringLiteral("页码越界：%1").arg(pageIndex));
                continue;
            }
            QPDFObjectHandle page = pages.at(static_cast<size_t>(pageIndex));
            page.coalesceContentStreams();
            // 多内容流按顺序拼成一条再处理：CTM 本来就是跨流连续的。
            std::vector<QPDFObjectHandle> contents = page.getPageContents();
            QByteArray bytes;
            for (QPDFObjectHandle& part : contents) {
                if (!part.isStream()) continue;
                const std::shared_ptr<Buffer> data = part.getStreamData(qpdf_dl_generalized);
                if (!data) continue;
                bytes += QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                                    static_cast<qsizetype>(data->getSize()));
                bytes += "\n";
            }
            const ContentParseResult parsed = ContentStream::tokenize(bytes);
            if (!parsed.isValid()) {
                result.errors.append(
                    QStringLiteral("第 %1 页内容流解析失败：%2").arg(pageIndex + 1).arg(parsed.error));
                continue;
            }
            const QVector<ContentToken>& tokens = parsed.tokens;

            // ---- 第一遍：解析路径，算出要改哪些数字 ----
            QVector<PathObject> pathObjects;
            {
                Matrix ctm;
                QVector<Matrix> stack;
                QVector<int> pending;
                PathObject current;
                bool active = false;
                for (int i = 0; i < tokens.size(); ++i) {
                    const ContentToken& token = tokens.at(i);
                    if (token.type == ContentTokenType::Number) {
                        pending.append(i);
                        continue;
                    }
                    if (token.type != ContentTokenType::Operator) {
                        pending.clear();
                        continue;
                    }
                    const QByteArray& op = token.raw;
                    if (op == "q") {
                        stack.append(ctm);
                    } else if (op == "Q") {
                        if (!stack.isEmpty()) ctm = stack.takeLast();
                    } else if (op == "cm") {
                        const QVector<int> indices = lastNumberIndices(tokens, i, 6);
                        if (indices.size() == 6) {
                            Matrix m;
                            m.a = tokens.at(indices.at(0)).raw.toDouble();
                            m.b = tokens.at(indices.at(1)).raw.toDouble();
                            m.c = tokens.at(indices.at(2)).raw.toDouble();
                            m.d = tokens.at(indices.at(3)).raw.toDouble();
                            m.e = tokens.at(indices.at(4)).raw.toDouble();
                            m.f = tokens.at(indices.at(5)).raw.toDouble();
                            ctm = multiply(m, ctm);
                        }
                    } else if (isPathConstruction(op)) {
                        if (!active) {
                            current = PathObject{};
                            current.matrix = ctm;
                            active = true;
                        }
                        const int needed = op == "c" ? 6 : (op == "v" || op == "y" ? 4
                                                : op == "re" ? 4 : op == "h" ? 0 : 2);
                        const QVector<int> indices = lastNumberIndices(tokens, i, needed);
                        if (indices.size() == needed && needed > 0) {
                            if (op == "re") {
                                const double x = tokens.at(indices.at(0)).raw.toDouble();
                                const double y = tokens.at(indices.at(1)).raw.toDouble();
                                const double w = tokens.at(indices.at(2)).raw.toDouble();
                                const double h = tokens.at(indices.at(3)).raw.toDouble();
                                addPoint(&current, indices.at(0), indices.at(1), x, y);
                                includePoint(&current, x + w, y + h);
                            } else {
                                for (int p = 0; p + 1 < indices.size(); p += 2) {
                                    const double x = tokens.at(indices.at(p)).raw.toDouble();
                                    const double y = tokens.at(indices.at(p + 1)).raw.toDouble();
                                    addPoint(&current, indices.at(p), indices.at(p + 1), x, y);
                                }
                            }
                        }
                    } else if (isPainting(op)) {
                        if (active) {
                            if (current.hasPoint) pathObjects.append(current);
                            active = false;
                        }
                    }
                    pending.clear();
                }
                if (active && current.hasPoint) pathObjects.append(current);
            }

            // ---- 第一遍续：把命中的路径点改掉（记 token 下标 → 新数字） ----
            QHash<int, QByteArray> overrides;
            const QVector<AnchorPathMove>& pagePathMoves = pathsByPage.value(pageIndex);
            QVector<bool> pathMatched(pagePathMoves.size(), false);
            for (int moveIndex = 0; moveIndex < pagePathMoves.size(); ++moveIndex) {
                const AnchorPathMove& move = pagePathMoves.at(moveIndex);
                PathObject* match = nullptr;
                for (PathObject& object : pathObjects) {
                    if (object.used) continue;
                    if (sameRect(object.bounds, move.from)) {
                        match = &object;
                        break;
                    }
                }
                if (match == nullptr) continue;
                match->used = true;
                pathMatched[moveIndex] = true;
                const Matrix& m = match->matrix;
                const double det = m.a * m.d - m.b * m.c;
                if (std::abs(det) < 1e-9) {
                    result.errors.append(QStringLiteral("第 %1 页路径的 CTM 不可逆").arg(pageIndex + 1));
                    continue;
                }
                // 页面空间向量 (0, -deltaY) 换算回用户空间。
                const double pageX = 0.0;
                const double pageY = -move.deltaY;
                const double userX = (pageX * m.d - pageY * m.c) / det;
                const double userY = (pageY * m.a - pageX * m.b) / det;
                for (const QPair<int, int>& point : match->points) {
                    const double x = tokens.at(point.first).raw.toDouble();
                    const double y = tokens.at(point.second).raw.toDouble();
                    overrides.insert(point.first, formatNumber(x + userX));
                    overrides.insert(point.second, formatNumber(y + userY));
                }
                ++result.movedPaths;
                changed = true;
            }
            for (int m = 0; m < pagePathMoves.size(); ++m) {
                if (pathMatched.value(m)) continue;
                const QRectF& rect = pagePathMoves.at(m).from;
                {
                    result.errors.append(
                        QStringLiteral("第 %1 页没找到要移动的路径（%2, %3, %4×%5）")
                            .arg(pageIndex + 1)
                            .arg(rect.x(), 0, 'f', 1)
                            .arg(rect.y(), 0, 'f', 1)
                            .arg(rect.width(), 0, 'f', 1)
                            .arg(rect.height(), 0, 'f', 1));
                }
            }

            // ---- 第二遍：重建内容流（路径改数字 + 图片包 cm） ----
            const QVector<AnchorImageMove>& pageImageMoves = imagesByPage.value(pageIndex);
            QVector<bool> imageUsed(pageImageMoves.size(), false);
            Matrix ctm;
            QVector<Matrix> stack;
            QByteArray output;
            int pendingClose = -1;
            for (int i = 0; i < tokens.size(); ++i) {
                const ContentToken& token = tokens.at(i);
                if (token.type == ContentTokenType::Number) {
                    const auto override = overrides.constFind(i);
                    output += override != overrides.constEnd() ? override.value() : token.raw;
                    continue;
                }
                if (token.type == ContentTokenType::Operator) {
                    bool skipRaw = false;
                    if (token.raw == "q") {
                        stack.append(ctm);
                    } else if (token.raw == "Q") {
                        if (!stack.isEmpty()) ctm = stack.takeLast();
                        if (pendingClose >= 0) {
                            output += " Q";
                            pendingClose = -1;
                            skipRaw = true;
                        }
                    } else if (token.raw == "cm") {
                        const QVector<int> indices = lastNumberIndices(tokens, i, 6);
                        if (indices.size() == 6) {
                            Matrix m;
                            m.a = tokens.at(indices.at(0)).raw.toDouble();
                            m.b = tokens.at(indices.at(1)).raw.toDouble();
                            m.c = tokens.at(indices.at(2)).raw.toDouble();
                            m.d = tokens.at(indices.at(3)).raw.toDouble();
                            m.e = tokens.at(indices.at(4)).raw.toDouble();
                            m.f = tokens.at(indices.at(5)).raw.toDouble();
                            ctm = multiply(m, ctm);
                        }
                    }
                    if (!skipRaw) output += token.raw;
                    continue;
                }
                if (token.type == ContentTokenType::Name) {
                    const int next = nextSignificant(tokens, i);
                    if (next >= 0 && tokens.at(next).raw == "Do") {
                        const QRectF bounds = unitSquareBounds(ctm);
                        int match = -1;
                        for (int m = 0; m < pageImageMoves.size(); ++m) {
                            if (imageUsed.value(m)) continue;
                            if (sameRect(bounds, pageImageMoves.at(m).from)) {
                                match = m;
                                break;
                            }
                        }
                        if (match >= 0) {
                            const double delta = pageImageMoves.at(match).deltaY;
                            output += "q 1 0 0 1 0 " + QByteArray::number(-delta, 'f', 3) + " cm ";
                            pendingClose = match;
                            imageUsed[match] = true;
                            ++result.moved;
                            changed = true;
                        }
                    }
                    output += token.raw;
                    continue;
                }
                output += token.raw;
            }
            if (pendingClose >= 0) output += " Q";
            for (int m = 0; m < pageImageMoves.size(); ++m) {
                if (imageUsed.value(m)) continue;
                const QRectF& rect = pageImageMoves.at(m).from;
                result.errors.append(
                    QStringLiteral("第 %1 页没找到要移动的图片（%2, %3, %4×%5）")
                        .arg(pageIndex + 1)
                        .arg(rect.x(), 0, 'f', 1)
                        .arg(rect.y(), 0, 'f', 1)
                        .arg(rect.width(), 0, 'f', 1)
                        .arg(rect.height(), 0, 'f', 1));
            }

            if (output != bytes) {
                if (contents.size() == 1) {
                    contents.front().replaceStreamData(output.toStdString(),
                                                       QPDFObjectHandle::newNull(),
                                                       QPDFObjectHandle::newNull());
                } else {
                    // 原本是多条流：合并成一条，内容不变。
                    QPDFObjectHandle merged = pdf.newStream(output.toStdString());
                    page.replaceKey("/Contents", pdf.makeIndirectObject(merged));
                }
            }
        }

        if (!result.errors.isEmpty()) return result;
        if (changed) {
            QPDFWriter writer(pdf);
            writer.setOutputMemory();
            writer.write();
            std::unique_ptr<Buffer> buffer(writer.getBuffer());
            if (!buffer || buffer->getSize() == 0) {
                result.errors.append(QStringLiteral("写出的 PDF 为空"));
                return result;
            }
            result.bytes = QByteArray(reinterpret_cast<const char*>(buffer->getBuffer()),
                                      static_cast<qsizetype>(buffer->getSize()));
        } else {
            result.bytes = pdfBytes;
        }
    } catch (const std::exception& exception) {
        result.errors.append(QString::fromUtf8(exception.what()));
    }
    return result;
}

AnchorMoveResult moveAnchoredImages(const QByteArray& pdfBytes,
                                    const QVector<AnchorImageMove>& moves) {
    return moveAnchoredGraphics(pdfBytes, moves, {});
}

}  // namespace aipdf::pdf
