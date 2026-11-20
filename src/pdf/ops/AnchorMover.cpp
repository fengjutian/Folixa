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

int nextSignificant(const QVector<ContentToken>& tokens, int index) {
    for (int i = index + 1; i < tokens.size(); ++i) {
        if (tokens.at(i).type != ContentTokenType::Whitespace &&
            tokens.at(i).type != ContentTokenType::Comment) {
            return i;
        }
    }
    return -1;
}

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

QString objectName(const QPDFObjectHandle& object) {
    if (!object.isName()) return QString();
    std::string name = object.getName();
    if (!name.empty() && name.front() == '/') name.erase(name.begin());
    return QString::fromStdString(name);
}

Matrix parseMatrix(const QPDFObjectHandle& object) {
    Matrix matrix;
    if (!object.isArray() || object.getArrayNItems() < 6) return matrix;
    double values[6] = {1, 0, 0, 1, 0, 0};
    for (int i = 0; i < 6; ++i) {
        if (!object.getArrayItem(i).isNumber()) return matrix;
        values[i] = object.getArrayItem(i).getNumericValue();
    }
    matrix.a = values[0];
    matrix.b = values[1];
    matrix.c = values[2];
    matrix.d = values[3];
    matrix.e = values[4];
    matrix.f = values[5];
    return matrix;
}

qint64 objectKey(const QPDFObjectHandle& object) {
    const QPDFObjGen id = object.getObjGen();
    return (static_cast<qint64>(id.getObj()) << 32) | static_cast<quint32>(id.getGen());
}

QByteArray formatNumber(double value) {
    QByteArray text = QByteArray::number(value, 'f', 3);
    while (text.endsWith('0')) text.chop(1);
    if (text.endsWith('.')) text.chop(1);
    return text;
}

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

struct PageContext {
    int pageIndex = 0;
    QVector<AnchorImageMove> imageMoves;
    QVector<AnchorPathMove> pathMoves;
    QVector<bool> imageUsed;
    QVector<bool> pathMatched;
    QSet<qint64> visitedXObjects;
    int movedImages = 0;
    int movedPaths = 0;
    QStringList errors;
};

// 处理一条内容流。placement 把「这条流的本地坐标」映到页面坐标；
// resources 用于查 /XObject；stream 可能为空（页面多条流合并后的虚拟流）。
bool processStream(QPDFObjectHandle stream, QPDFObjectHandle resources, const Matrix& placement,
                   int depth, PageContext* context) {
    if (depth > 8) return false;
    const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
    if (!data) return false;
    const QByteArray bytes(reinterpret_cast<const char*>(data->getBuffer()),
                           static_cast<qsizetype>(data->getSize()));
    const ContentParseResult parsed = ContentStream::tokenize(bytes);
    if (!parsed.isValid()) {
        context->errors.append(QStringLiteral("第 %1 页内容流解析失败：%2")
                                   .arg(context->pageIndex + 1)
                                   .arg(parsed.error));
        return false;
    }
    const QVector<ContentToken>& tokens = parsed.tokens;

    // ---- 第一遍：路径包围盒（本地坐标 × CTM × placement） ----
    QVector<PathObject> pathObjects;
    {
        Matrix ctm;
        QVector<Matrix> stack;
        PathObject current;
        bool active = false;
        for (int i = 0; i < tokens.size(); ++i) {
            const ContentToken& token = tokens.at(i);
            if (token.type != ContentTokenType::Operator) continue;
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
                    current.matrix = multiply(ctm, placement);
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
                            addPoint(&current, indices.at(p), indices.at(p + 1),
                                     tokens.at(indices.at(p)).raw.toDouble(),
                                     tokens.at(indices.at(p + 1)).raw.toDouble());
                        }
                    }
                }
            } else if (isPainting(op)) {
                if (active) {
                    if (current.hasPoint) pathObjects.append(current);
                    active = false;
                }
            }
        }
        if (active && current.hasPoint) pathObjects.append(current);
    }

    // ---- 路径平移：记 token 下标 → 新数字 ----
    QHash<int, QByteArray> overrides;
    for (int moveIndex = 0; moveIndex < context->pathMoves.size(); ++moveIndex) {
        if (context->pathMatched.value(moveIndex)) continue;
        const AnchorPathMove& move = context->pathMoves.at(moveIndex);
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
        context->pathMatched[moveIndex] = true;
        const Matrix& m = match->matrix;
        const double det = m.a * m.d - m.b * m.c;
        if (std::abs(det) < 1e-9) {
            context->errors.append(
                QStringLiteral("第 %1 页路径的 CTM 不可逆").arg(context->pageIndex + 1));
            continue;
        }
        const double pageY = -move.deltaY;
        const double userX = (0.0 * m.d - pageY * m.c) / det;
        const double userY = (pageY * m.a - 0.0 * m.b) / det;
        for (const QPair<int, int>& point : match->points) {
            const double x = tokens.at(point.first).raw.toDouble();
            const double y = tokens.at(point.second).raw.toDouble();
            overrides.insert(point.first, formatNumber(x + userX));
            overrides.insert(point.second, formatNumber(y + userY));
        }
        ++context->movedPaths;
    }

    // ---- 第二遍：重建（路径改数字 + 图片 cm + 递归 XObject） ----
    Matrix ctm;
    QVector<Matrix> stack;
    QByteArray output;
    int pendingClose = -1;
    bool changed = false;
    for (int i = 0; i < tokens.size(); ++i) {
        const ContentToken& token = tokens.at(i);
        if (token.type == ContentTokenType::Number) {
            const auto override = overrides.constFind(i);
            output += override != overrides.constEnd() ? override.value() : token.raw;
            continue;
        }
        if (token.type == ContentTokenType::Name) {
            const int next = nextSignificant(tokens, i);
            if (next >= 0 && tokens.at(next).raw == "Do") {
                const QPDFObjectHandle xobjects = resources.getKey("/XObject");
                QString rawName = QString::fromLatin1(token.raw);
                if (rawName.startsWith(QLatin1Char('/'))) rawName.remove(0, 1);
                const QPDFObjectHandle object =
                    xobjects.isDictionary() ? xobjects.getKey("/" + rawName.toStdString())
                                            : QPDFObjectHandle();
                if (object.isStream()) {
                    const QPDFObjectHandle dict = object.getDict();
                    const QString subtype = objectName(dict.getKey("/Subtype"));
                    if (subtype == QLatin1String("Image")) {
                        const QRectF bounds = unitSquareBounds(multiply(ctm, placement));
                        int match = -1;
                        for (int m = 0; m < context->imageMoves.size(); ++m) {
                            if (context->imageUsed.value(m)) continue;
                            if (sameRect(bounds, context->imageMoves.at(m).from)) {
                                match = m;
                                break;
                            }
                        }
                        if (match >= 0) {
                            const double delta = context->imageMoves.at(match).deltaY;
                            const Matrix effective = multiply(ctm, placement);
                            const double det =
                                effective.a * effective.d - effective.b * effective.c;
                            double localX = 0.0;
                            double localY = -delta;
                            if (std::abs(det) > 1e-9) {
                                localX = (0.0 * effective.d - (-delta) * effective.c) / det;
                                localY = ((-delta) * effective.a - 0.0 * effective.b) / det;
                            }
                            output += "q 1 0 0 1 " + formatNumber(localX) + " " +
                                      formatNumber(localY) + " cm ";
                            pendingClose = match;
                            context->imageUsed[match] = true;
                            ++context->movedImages;
                            changed = true;
                        }
                    } else if (subtype == QLatin1String("Form")) {
                        if (depth < 8) {
                            const qint64 key = objectKey(object);
                            if (key == 0 || !context->visitedXObjects.contains(key)) {
                                if (key != 0) context->visitedXObjects.insert(key);
                                Matrix childPlacement = multiply(
                                    parseMatrix(dict.getKey("/Matrix")),
                                    multiply(ctm, placement));
                                const QPDFObjectHandle childResources =
                                    dict.getKey("/Resources");
                                if (processStream(object, childResources, childPlacement,
                                                  depth + 1, context)) {
                                    changed = true;
                                }
                            }
                        }
                    }
                }
            }
            output += token.raw;
            continue;
        }
        if (token.type != ContentTokenType::Operator) {
            output += token.raw;
            continue;
        }
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
    }
    if (pendingClose >= 0) output += " Q";

    if (output != bytes) {
        stream.replaceStreamData(output.toStdString(), QPDFObjectHandle::newNull(),
                                 QPDFObjectHandle::newNull());
        changed = true;
    }
    return changed;
}

QPDFObjectHandle pageResources(QPDFObjectHandle page) {
    for (int depth = 0; depth < 64 && page.isDictionary(); ++depth) {
        if (page.hasKey("/Resources")) {
            const QPDFObjectHandle resources = page.getKey("/Resources");
            if (resources.isDictionary()) return resources;
        }
        if (!page.hasKey("/Parent")) break;
        page = page.getKey("/Parent");
    }
    return QPDFObjectHandle::newDictionary();
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
    for (auto it = imagesByPage.constBegin(); it != imagesByPage.constEnd(); ++it) {
        pageSet.insert(it.key());
    }
    for (auto it = pathsByPage.constBegin(); it != pathsByPage.constEnd(); ++it) {
        pageSet.insert(it.key());
    }

    try {
        QPDF pdf;
        pdf.processMemoryFile("anchor-move.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();

        for (int pageIndex : pageSet) {
            if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) {
                result.errors.append(QStringLiteral("页码越界：%1").arg(pageIndex));
                continue;
            }
            QPDFObjectHandle page = pages.at(static_cast<size_t>(pageIndex));
            page.coalesceContentStreams();

            PageContext context;
            context.pageIndex = pageIndex;
            context.imageMoves = imagesByPage.value(pageIndex);
            context.pathMoves = pathsByPage.value(pageIndex);
            context.imageUsed = QVector<bool>(context.imageMoves.size(), false);
            context.pathMatched = QVector<bool>(context.pathMoves.size(), false);
            const QPDFObjectHandle resources = pageResources(page);

            std::vector<QPDFObjectHandle> contents = page.getPageContents();
            if (contents.size() == 1) {
                processStream(contents.front(), resources, Matrix{}, 0, &context);
            } else if (!contents.empty()) {
                // 多条流：拼成一条虚拟流处理，写回时合并。
                QByteArray joined;
                for (QPDFObjectHandle& part : contents) {
                    if (!part.isStream()) continue;
                    const std::shared_ptr<Buffer> data = part.getStreamData(qpdf_dl_generalized);
                    if (!data) continue;
                    joined += QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                                         static_cast<qsizetype>(data->getSize()));
                    joined += "\n";
                }
                QPDFObjectHandle fake = pdf.newStream(joined.toStdString());
                processStream(fake, resources, Matrix{}, 0, &context);
                const std::shared_ptr<Buffer> data = fake.getStreamData(qpdf_dl_generalized);
                if (data) {
                    const QByteArray after(reinterpret_cast<const char*>(data->getBuffer()),
                                           static_cast<qsizetype>(data->getSize()));
                    if (after != joined) {
                        QPDFObjectHandle merged = pdf.newStream(after.toStdString());
                        page.replaceKey("/Contents", pdf.makeIndirectObject(merged));
                    }
                }
            }

            result.moved += context.movedImages;
            result.movedPaths += context.movedPaths;
            result.errors += context.errors;

            for (int m = 0; m < context.imageMoves.size(); ++m) {
                if (context.imageUsed.value(m)) continue;
                const QRectF& rect = context.imageMoves.at(m).from;
                result.errors.append(
                    QStringLiteral("第 %1 页没找到要移动的图片（%2, %3, %4×%5）")
                        .arg(pageIndex + 1)
                        .arg(rect.x(), 0, 'f', 1)
                        .arg(rect.y(), 0, 'f', 1)
                        .arg(rect.width(), 0, 'f', 1)
                        .arg(rect.height(), 0, 'f', 1));
            }
            for (int m = 0; m < context.pathMoves.size(); ++m) {
                if (context.pathMatched.value(m)) continue;
                const QRectF& rect = context.pathMoves.at(m).from;
                result.errors.append(
                    QStringLiteral("第 %1 页没找到要移动的路径（%2, %3, %4×%5）")
                        .arg(pageIndex + 1)
                        .arg(rect.x(), 0, 'f', 1)
                        .arg(rect.y(), 0, 'f', 1)
                        .arg(rect.width(), 0, 'f', 1)
                        .arg(rect.height(), 0, 'f', 1));
            }
        }

        if (!result.errors.isEmpty()) return result;
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
