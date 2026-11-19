#pragma once

// 把字形轮廓写回 PDF（qpdf 侧）。
//
// 这一层做三件事：
//   1. 把页面上「显示文字」的算子删掉（真的没有文字，而不是靠「字体资源缺失」蒙混）；
//   2. 把轮廓写成路径算子（m / l / c / h + f）追加到页面内容流末尾；
//   3. 把页面资源里的字体去掉——字体对象因此不再被引用，
//      qpdf 写盘时不会把它们写出去，产物里就没有 FontFile 了。
//
// 为什么轮廓追加在内容流末尾而不是原位替换：
//   原位替换需要知道每个文字算子对应哪几个字符、以及当时的内容流 CTM 是多少，
//   这两样都要自己实现一遍内容流解释器，越界风险大。
//   追加在末尾的代价是**层叠顺序**：如果页面上有压在文字上面的不透明图形，
//   轮廓会盖在它上面（文字反而更显眼）。这是已知取舍，报告里会说明。

#include "doc/convert/GlyphOutline.h"
#include "pdf/content/ContentStream.h"

#include <QByteArray>
#include <QSet>
#include <QString>
#include <QVector>

#include <cmath>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#endif

namespace aipdf::doc {
namespace detail {

// 一个 2×3 仿射矩阵（PDF 的 cm 算子用的那套）。
struct AffineMatrix {
    double a = 1.0;
    double b = 0.0;
    double c = 0.0;
    double d = 1.0;
    double e = 0.0;
    double f = 0.0;
};

// 矩阵相乘：返回 m × n（先作用 n 再作用 m）。
inline AffineMatrix multiply(const AffineMatrix& m, const AffineMatrix& n) {
    AffineMatrix r;
    r.a = m.a * n.a + m.b * n.c;
    r.b = m.a * n.b + m.b * n.d;
    r.c = m.c * n.a + m.d * n.c;
    r.d = m.c * n.b + m.d * n.d;
    r.e = m.e * n.a + m.f * n.c + n.e;
    r.f = m.e * n.b + m.f * n.d + n.f;
    return r;
}

// 矩阵求逆；不可逆时返回单位阵（退化情况按「不改」处理）。
inline AffineMatrix invert(const AffineMatrix& m) {
    const double determinant = m.a * m.d - m.b * m.c;
    if (std::abs(determinant) < 1e-12) {
        return AffineMatrix();
    }
    AffineMatrix r;
    r.a = m.d / determinant;
    r.b = -m.b / determinant;
    r.c = -m.c / determinant;
    r.d = m.a / determinant;
    r.e = (m.c * m.f - m.d * m.e) / determinant;
    r.f = (m.b * m.e - m.a * m.f) / determinant;
    return r;
}

inline bool isIdentity(const AffineMatrix& m) {
    return std::abs(m.a - 1.0) < 1e-9 && std::abs(m.b) < 1e-9 && std::abs(m.c) < 1e-9 &&
           std::abs(m.d - 1.0) < 1e-9 && std::abs(m.e) < 1e-9 && std::abs(m.f) < 1e-9;
}

// 坐标格式化：小数点后三位（1/72000 英寸），肉眼与印刷都看不出来。
inline QByteArray formatCoordinate(double value) {
    QByteArray text = QByteArray::number(value, 'f', 3);
    if (text == "-0.000") {
        text = "0.000";
    }
    return text;
}

// 一个字符的轮廓 → PDF 路径算子。
//
// 为什么贝塞尔是「三个点一组」：PDFium 的字形轮廓里，
// 一条三次贝塞尔由「控制点 1、控制点 2、终点」三个 BEZIERTO 点表示，
// 三个一组才是一条完整的曲线；零头就是数据坏了，要报错而不是猜。
//
// 注意操作数的顺序：PDF 的内容流是「先操作数、后算子」
// （"x y m"，不是 "m x y"）。写反了 PDFium 会认为算子缺操作数，
// 整段内容流直接作废——页面会变成空白，而且不报任何错。
inline bool buildCharPath(const QVector<OutlineSegment>& segments, QByteArray* out, QString* error) {
    QVector<OutlineSegment> pending;
    for (const OutlineSegment& segment : segments) {
        switch (segment.type) {
            case OutlineSegmentType::Move:
                if (!pending.isEmpty()) {
                    if (error != nullptr) {
                        *error = QStringLiteral("字形轮廓数据不完整（贝塞尔段缺少终点）。");
                    }
                    return false;
                }
                *out += formatCoordinate(segment.x) + " " + formatCoordinate(segment.y) + " m\n";
                break;
            case OutlineSegmentType::Line:
                if (!pending.isEmpty()) {
                    if (error != nullptr) {
                        *error = QStringLiteral("字形轮廓数据不完整（直线段插在贝塞尔段中间）。");
                    }
                    return false;
                }
                *out += formatCoordinate(segment.x) + " " + formatCoordinate(segment.y) + " l\n";
                break;
            case OutlineSegmentType::Bezier:
                pending.append(segment);
                if (pending.size() == 3) {
                    *out += formatCoordinate(pending.at(0).x) + " " +
                            formatCoordinate(pending.at(0).y) + " " +
                            formatCoordinate(pending.at(1).x) + " " +
                            formatCoordinate(pending.at(1).y) + " " +
                            formatCoordinate(pending.at(2).x) + " " +
                            formatCoordinate(pending.at(2).y) + " c\n";
                    pending.clear();
                }
                break;
        }
        if (segment.close) {
            if (!pending.isEmpty()) {
                if (error != nullptr) {
                    *error = QStringLiteral("字形轮廓数据不完整（子路径没有闭合）。");
                }
                return false;
            }
            *out += "h\n";
        }
    }
    if (!pending.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("字形轮廓数据不完整（贝塞尔段缺少终点）。");
        }
        return false;
    }
    return true;
}

// 一页的轮廓 → 一段内容流。
inline QByteArray buildOutlineContent(const QVector<OutlinedChar>& chars, QString* error) {
    QByteArray content;
    content += "q\n";
    QColor currentColor(-1, -1, -1);
    for (const OutlinedChar& character : chars) {
        QByteArray path;
        if (!buildCharPath(character.segments, &path, error)) {
            return QByteArray();
        }
        if (path.isEmpty()) {
            continue;
        }
        if (character.hasFillColor && character.fillColor != currentColor) {
            currentColor = character.fillColor;
            content += QByteArray::number(currentColor.red() / 255.0, 'f', 3) + " " +
                      QByteArray::number(currentColor.green() / 255.0, 'f', 3) + " " +
                      QByteArray::number(currentColor.blue() / 255.0, 'f', 3) + " rg\n";
        }
        content += path;
        content += "f\n";
    }
    content += "Q\n";
    return content;
}

#ifdef AIPDF_WITH_QPDF

// 内容流里要删掉的算子。
//
// Tj / TJ / ' / " 是「显示文字」；Tf 是「选字体」。
// Tf 也要删：留着就会引用一个已经不存在的字体资源，PDFium 会记一条资源缺失的警告。
inline bool isStrippedOperator(const QByteArray& op) {
    return op == "Tj" || op == "TJ" || op == "'" || op == "\"" || op == "Tf";
}

// 内容流扫描的结果。
struct ContentScan {
    // 删掉之后的内容流。
    QByteArray content;
    // 删掉的文字算子个数。
    int removedTextOperators = 0;
    // 内容流结束时累计的 CTM（用来把追加的轮廓放回默认坐标系）。
    AffineMatrix finalMatrix;
};

// 扫一遍内容流：删掉显示文字的算子，顺带记住结束时 CTM 是多少。
//
// 为什么重新拼一遍而不是原地删：删掉一段 token 之后，两边的操作数可能粘在一起
// （"24" + "Tf" 变成 "24Tf"），正文就丢了。所以这里只保留有效 token，
// 彼此之间显式补一个空格——这是本仓库踩过的坑。
inline ContentScan stripTextOperators(const QByteArray& content, QString* error) {
    ContentScan scan;
    const pdf::ContentParseResult parsed = pdf::ContentStream::tokenize(content);
    if (!parsed.isValid()) {
        if (error != nullptr) {
            *error = QStringLiteral("内容流解析失败：%1").arg(parsed.error);
        }
        return scan;
    }

    QVector<pdf::ContentToken> kept;
    QVector<AffineMatrix> matrixStack;
    AffineMatrix current;
    int operandStart = 0;
    int depth = 0;

    auto numbersIn = [&](int from, int to) {
        QVector<double> numbers;
        for (int i = from; i < to; ++i) {
            if (parsed.tokens.at(i).type != pdf::ContentTokenType::Number) {
                continue;
            }
            bool ok = false;
            const double value = parsed.tokens.at(i).raw.toDouble(&ok);
            if (ok) {
                numbers.append(value);
            }
        }
        return numbers;
    };

    for (int i = 0; i < parsed.tokens.size(); ++i) {
        const pdf::ContentToken& token = parsed.tokens.at(i);
        if (token.type == pdf::ContentTokenType::ArrayStart ||
            token.type == pdf::ContentTokenType::DictionaryStart) {
            ++depth;
            continue;
        }
        if (token.type == pdf::ContentTokenType::ArrayEnd ||
            token.type == pdf::ContentTokenType::DictionaryEnd) {
            --depth;
            continue;
        }
        if (depth > 0) {
            continue;  // 数组/字典内部不算操作数
        }
        if (token.type != pdf::ContentTokenType::Operator) {
            continue;
        }

        const QByteArray op = token.raw;
        if (op == "q") {
            matrixStack.append(current);
        } else if (op == "Q") {
            if (!matrixStack.isEmpty()) {
                current = matrixStack.takeLast();
            }
        } else if (op == "cm") {
            const QVector<double> numbers = numbersIn(operandStart, i);
            if (numbers.size() >= 6) {
                AffineMatrix m;
                m.a = numbers.at(0);
                m.b = numbers.at(1);
                m.c = numbers.at(2);
                m.d = numbers.at(3);
                m.e = numbers.at(4);
                m.f = numbers.at(5);
                current = multiply(m, current);
            }
        } else if (isStrippedOperator(op)) {
            ++scan.removedTextOperators;
            operandStart = i + 1;
            continue;  // 操作数与算子一起丢掉
        }

        for (int k = operandStart; k < i; ++k) {
            const pdf::ContentToken& operand = parsed.tokens.at(k);
            if (operand.type == pdf::ContentTokenType::Whitespace ||
                operand.type == pdf::ContentTokenType::Comment) {
                continue;
            }
            kept.append(operand);
        }
        kept.append(token);
        operandStart = i + 1;
    }

    QByteArray rebuilt;
    for (const pdf::ContentToken& token : kept) {
        if (!rebuilt.isEmpty()) {
            rebuilt += " ";
        }
        rebuilt += token.raw;
    }
    // 结尾留一个换行：后面还要往这条内容流里追加轮廓路径，
    // 没有分隔符的话 "ET" 与 "q" 会粘成 "ETq"，正文就丢了
    // （本仓库在 qpdf 的 TokenFilter 上踩过同一类坑）。
    if (!rebuilt.isEmpty()) {
        rebuilt += "\n";
    }
    scan.content = rebuilt;
    scan.finalMatrix = current;
    return scan;
}

// 把页面（含 Form XObject）里的字体资源去掉。
inline void stripFontsFromResources(QPDFObjectHandle resources,
                                    QSet<QByteArray>* visitedXObjects) {
    if (!resources.isDictionary()) {
        return;
    }
    resources.removeKey("/Font");

    const QPDFObjectHandle xObjects = resources.getKeyIfDict("/XObject");
    if (!xObjects.isDictionary()) {
        return;
    }
    for (const auto& entry : xObjects.getDictAsMap()) {
        QPDFObjectHandle xObject = entry.second;
        if (!xObject.isStream()) {
            continue;
        }
        // 同一个 Form 可能被引用多次；按对象号去重，避免重复处理与死循环。
        const QByteArray key = QByteArray::number(xObject.getObjectID()) + " " +
                               QByteArray::number(xObject.getGeneration());
        if (visitedXObjects->contains(key)) {
            continue;
        }
        visitedXObjects->insert(key);

        const QPDFObjectHandle subtype = xObject.getKey("/Subtype");
        if (!subtype.isName() || subtype.getName() != "Form") {
            continue;
        }
        const std::shared_ptr<Buffer> data = xObject.getStreamData(qpdf_dl_generalized);
        if (!data) {
            continue;
        }
        const QByteArray original(reinterpret_cast<const char*>(data->getBuffer()),
                                  static_cast<qsizetype>(data->getSize()));
        QString scanError;
        const ContentScan scan = stripTextOperators(original, &scanError);
        if (scanError.isEmpty() && scan.content != original) {
            xObject.replaceStreamData(scan.content.toStdString(), QPDFObjectHandle::newNull(),
                                      QPDFObjectHandle::newNull());
        }
        stripFontsFromResources(xObject.getKeyIfDict("/Resources"), visitedXObjects);
    }
}

// 写盘的结果。
struct OutlineWriteResult {
    bool ok = false;
    QByteArray bytes;
    QString error;
};

// 把轮廓写回一份新的 PDF。
//
// sourceBytes：原始 PDF。
// pageOutlines：每一页的轮廓（按页号对齐）。
// keepTextPages：这些页保留原文字（不删文字算子、不去字体），
//                用于「转不了就保留原文」的回退策略。
inline OutlineWriteResult writeFlattenedPdf(const QByteArray& sourceBytes,
                                             const QVector<PageOutlineResult>& pageOutlines,
                                             const QSet<int>& keepTextPages) {
    OutlineWriteResult result;
    try {
        QPDF pdf;
        pdf.processMemoryFile("flatten.pdf", sourceBytes.constData(),
                              static_cast<size_t>(sourceBytes.size()));
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();

        for (size_t index = 0; index < pages.size(); ++index) {
            QPDFObjectHandle page = pages.at(index);
            const int pageIndex = static_cast<int>(index);
            const bool keepText = keepTextPages.contains(pageIndex);

            page.coalesceContentStreams();
            const std::vector<QPDFObjectHandle> contents = page.getPageContents();

            QByteArray outlineContent;
            if (pageIndex < pageOutlines.size()) {
                QString buildError;
                outlineContent = buildOutlineContent(pageOutlines.at(pageIndex).outlined, &buildError);
                if (!buildError.isEmpty()) {
                    result.error = QStringLiteral("第 %1 页：%2").arg(pageIndex + 1).arg(buildError);
                    return result;
                }
            }

            for (size_t streamIndex = 0; streamIndex < contents.size(); ++streamIndex) {
                QPDFObjectHandle stream = contents.at(streamIndex);
                if (!stream.isStream()) {
                    continue;
                }
                if (keepText) {
                    // 这一页走「保留原文字」的回退：内容流一个字都不动。
                    continue;
                }
                const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
                if (!data) {
                    continue;
                }
                const QByteArray original(reinterpret_cast<const char*>(data->getBuffer()),
                                          static_cast<qsizetype>(data->getSize()));
                QString scanError;
                ContentScan scan = stripTextOperators(original, &scanError);
                if (!scanError.isEmpty()) {
                    result.error = QStringLiteral("第 %1 页：%2").arg(pageIndex + 1).arg(scanError);
                    return result;
                }

                QByteArray rebuilt = scan.content;
                const bool lastStream = streamIndex + 1 == contents.size();
                if (lastStream && !outlineContent.isEmpty() && !keepText) {
                    // 内容流末尾的 CTM 未必是单位阵（有的页面留下了一个没撤销的 cm），
                    // 所以先把 CTM 复位，再写轮廓，否则轮廓会整体偏移或缩放。
                    const AffineMatrix inverse = invert(scan.finalMatrix);
                    if (!isIdentity(inverse)) {
                        rebuilt += "\nq " + formatCoordinate(inverse.a) + " " +
                                  formatCoordinate(inverse.b) + " " + formatCoordinate(inverse.c) +
                                  " " + formatCoordinate(inverse.d) + " " + formatCoordinate(inverse.e) +
                                  " " + formatCoordinate(inverse.f) + " cm\n";
                    }
                    rebuilt += outlineContent;
                }
                if (rebuilt != original) {
                    stream.replaceStreamData(rebuilt.toStdString(), QPDFObjectHandle::newNull(),
                                             QPDFObjectHandle::newNull());
                }
            }

            if (!keepText) {
                QSet<QByteArray> visited;
                stripFontsFromResources(page.getKeyIfDict("/Resources"), &visited);
            }
        }

        QPDFWriter writer(pdf);
        writer.setOutputMemory();
        writer.write();
        std::unique_ptr<Buffer> output(writer.getBuffer());
        if (!output || output->getSize() == 0) {
            result.error = QStringLiteral("qpdf 写出的 PDF 为空。");
            return result;
        }
        result.bytes = QByteArray(reinterpret_cast<const char*>(output->getBuffer()),
                                  static_cast<qsizetype>(output->getSize()));
        result.ok = true;
        return result;
    } catch (const std::exception& exception) {
        result.error = QString::fromUtf8(exception.what());
        return result;
    }
}

#endif  // AIPDF_WITH_QPDF

}  // namespace detail
}  // namespace aipdf::doc
