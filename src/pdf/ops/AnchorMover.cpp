#include "pdf/ops/AnchorMover.h"

#include "pdf/content/ContentStream.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <QHash>

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

QVector<double> lastNumbers(const QVector<ContentToken>& tokens, int index, int count) {
    QVector<double> values;
    for (int i = index - 1; i >= 0 && values.size() < count; --i) {
        if (tokens.at(i).type == ContentTokenType::Number) values.append(tokens.at(i).raw.toDouble());
    }
    std::reverse(values.begin(), values.end());
    return values;
}

bool sameRect(const QRectF& left, const QRectF& right) {
    return std::abs(left.left() - right.left()) < 1.5 &&
           std::abs(left.top() - right.top()) < 1.5 &&
           std::abs(left.width() - right.width()) < 1.5 &&
           std::abs(left.height() - right.height()) < 1.5;
}

}  // namespace

AnchorMoveResult moveAnchoredImages(const QByteArray& pdfBytes,
                                    const QVector<AnchorImageMove>& moves) {
    AnchorMoveResult result;
    if (moves.isEmpty()) {
        result.bytes = pdfBytes;
        return result;
    }
    if (pdfBytes.isEmpty()) {
        result.errors.append(QStringLiteral("PDF 字节为空"));
        return result;
    }
    QHash<int, QVector<AnchorImageMove>> byPage;
    for (const AnchorImageMove& move : moves) byPage[move.pageIndex].append(move);
    QHash<int, QVector<bool>> used;
    for (auto it = byPage.constBegin(); it != byPage.constEnd(); ++it) {
        used.insert(it.key(), QVector<bool>(it.value().size(), false));
    }

    try {
        QPDF pdf;
        pdf.processMemoryFile("anchor-move.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        bool changed = false;

        for (auto it = byPage.constBegin(); it != byPage.constEnd(); ++it) {
            const int pageIndex = it.key();
            if (pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) {
                result.errors.append(QStringLiteral("页码越界：%1").arg(pageIndex));
                continue;
            }
            QPDFObjectHandle page = pages.at(static_cast<size_t>(pageIndex));
            page.coalesceContentStreams();
            const std::vector<QPDFObjectHandle> contents = page.getPageContents();
            if (contents.size() != 1) {
                result.errors.append(
                    QStringLiteral("第 %1 页不是单条内容流，暂不支持搬动").arg(pageIndex + 1));
                continue;
            }
            QPDFObjectHandle stream = contents.front();
            const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
            if (!data) continue;
            const QByteArray bytes(reinterpret_cast<const char*>(data->getBuffer()),
                                   static_cast<qsizetype>(data->getSize()));
            const ContentParseResult parsed = ContentStream::tokenize(bytes);
            if (!parsed.isValid()) {
                result.errors.append(
                    QStringLiteral("第 %1 页内容流解析失败：%2").arg(pageIndex + 1).arg(parsed.error));
                continue;
            }
            const QVector<ContentToken>& tokens = parsed.tokens;
            QVector<bool>& pageUsed = used[pageIndex];

            Matrix ctm;
            QVector<Matrix> stack;
            QByteArray output;
            int pendingClose = -1;  // 正在为哪个 Do 收尾 Q
            for (int i = 0; i < tokens.size(); ++i) {
                const ContentToken& token = tokens.at(i);
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
                        const QVector<double> values = lastNumbers(tokens, i, 6);
                        if (values.size() == 6) {
                            Matrix m;
                            m.a = values.at(0);
                            m.b = values.at(1);
                            m.c = values.at(2);
                            m.d = values.at(3);
                            m.e = values.at(4);
                            m.f = values.at(5);
                            ctm = multiply(m, ctm);
                        }
                    } else if (token.raw == "Do" && pendingClose < 0) {
                        // 没有配对的图片 Do：照常输出。
                    }
                    if (!skipRaw) output += token.raw;
                    continue;
                }
                if (token.type == ContentTokenType::Name) {
                    const int next = nextSignificant(tokens, i);
                    if (next >= 0 && tokens.at(next).raw == "Do") {
                        const QRectF bounds = unitSquareBounds(ctm);
                        int match = -1;
                        for (int m = 0; m < byPage.value(pageIndex).size(); ++m) {
                            if (pageUsed.value(m)) continue;
                            if (sameRect(bounds, byPage.value(pageIndex).at(m).from)) {
                                match = m;
                                break;
                            }
                        }
                        if (match >= 0) {
                            const double delta = byPage.value(pageIndex).at(match).deltaY;
                            output += "q 1 0 0 1 0 " +
                                      QByteArray::number(-delta, 'f', 3) + " cm ";
                            pendingClose = match;
                            pageUsed[match] = true;
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

            if (output != bytes) {
                stream.replaceStreamData(output.toStdString(), QPDFObjectHandle::newNull(),
                                         QPDFObjectHandle::newNull());
            }
        }

        for (auto it = byPage.constBegin(); it != byPage.constEnd(); ++it) {
            const QVector<bool>& pageUsed = used.value(it.key());
            for (int m = 0; m < it.value().size(); ++m) {
                if (pageUsed.value(m)) continue;
                const QRectF& rect = it.value().at(m).from;
                result.errors.append(
                    QStringLiteral("第 %1 页没找到要移动的图片（%2, %3, %4×%5）")
                        .arg(it.key() + 1)
                        .arg(rect.x(), 0, 'f', 1)
                        .arg(rect.y(), 0, 'f', 1)
                        .arg(rect.width(), 0, 'f', 1)
                        .arg(rect.height(), 0, 'f', 1));
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

}  // namespace aipdf::pdf
