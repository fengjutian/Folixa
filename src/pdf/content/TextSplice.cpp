#include "pdf/content/TextSplice.h"

#include "pdf/content/ContentStream.h"
#include "pdf/content/FontResource.h"

#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <cmath>
#include <memory>

#include <QHash>

namespace aipdf::pdf {
namespace {

#ifdef AIPDF_WITH_QPDF

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

QByteArray decodeLiteral(const QByteArray& raw) {
    QByteArray out;
    for (int i = 1; i + 1 < raw.size(); ++i) {
        char c = raw.at(i);
        if (c != '\\') {
            out += c;
            continue;
        }
        if (++i + 1 >= raw.size()) break;
        c = raw.at(i);
        if (c == 'n') out += '\n';
        else if (c == 'r') out += '\r';
        else if (c == 't') out += '\t';
        else if (c == 'b') out += '\b';
        else if (c == 'f') out += '\f';
        else if (c == '\n') continue;
        else if (c == '\r') {
            if (i + 1 < raw.size() && raw.at(i + 1) == '\n') ++i;
        } else if (c >= '0' && c <= '7') {
            int value = c - '0';
            for (int n = 0; n < 2 && i + 1 < raw.size() - 1 && raw.at(i + 1) >= '0' &&
                            raw.at(i + 1) <= '7';
                 ++n) {
                value = value * 8 + (raw.at(++i) - '0');
            }
            out += static_cast<char>(value);
        } else {
            out += c;
        }
    }
    return out;
}

QByteArray decodeHex(const QByteArray& raw) {
    QByteArray out;
    int high = -1;
    for (char c : raw) {
        const int digit = hexDigit(c);
        if (digit < 0) continue;
        if (high < 0) high = digit;
        else {
            out += static_cast<char>(high * 16 + digit);
            high = -1;
        }
    }
    return out;
}

QByteArray encodeLiteral(const QByteArray& bytes) {
    QByteArray out("(");
    for (char raw : bytes) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (c == '(' || c == ')' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c < 32 || c > 126) {
            out += '\\';
            out += QByteArray::number(c, 8).rightJustified(3, '0');
        } else {
            out += static_cast<char>(c);
        }
    }
    out += ')';
    return out;
}

QString unicodeName(char32_t code) {
    return QStringLiteral("U+%1").arg(code, 4, 16, QLatin1Char('0')).toUpper();
}

// 用页面字体把文字编码成内容流里的字符串操作数。
// 写不出任何一个字就失败——绝不静默换成别的字体。
bool encodeWithFont(const FontResource& font, const QString& text, QByteArray* out,
                    QString* error) {
    out->clear();
    if (text.isEmpty()) return true;
    const QVector<FontGlyph> glyphs = font.encode(text);
    QByteArray codes;
    for (const FontGlyph& glyph : glyphs) {
        if (!glyph.covered) {
            if (error) {
                *error = QStringLiteral("字体 %1（%2）写不出「%3」（%4）")
                             .arg(font.resourceName, font.baseFont,
                                  QString::fromUcs4(&glyph.unicode, 1), unicodeName(glyph.unicode));
            }
            return false;
        }
        if (font.codeBytes == 2) {
            codes += static_cast<char>((glyph.code >> 8) & 0xFF);
            codes += static_cast<char>(glyph.code & 0xFF);
        } else {
            codes += static_cast<char>(glyph.code & 0xFF);
        }
    }
    if (font.codeBytes == 2) {
        QByteArray hex;
        for (char c : codes) hex += QByteArray::number(static_cast<unsigned char>(c), 16)
                                       .rightJustified(2, '0')
                                       .toUpper();
        *out = "<" + hex + ">";
    } else {
        *out = encodeLiteral(codes);
    }
    return true;
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

struct ShowElement {
    int tokenIndex = -1;
    QString text;
};

struct ShowSpan {
    int first = -1;
    int last = -1;  // 操作符 token 下标
    QByteArray op;
    QString fontResource;
    double fontSize = 0.0;
    QVector<ShowElement> elements;
    const FontResource* font = nullptr;
};

QVector<ShowSpan> scanShows(const QVector<ContentToken>& tokens, const PageFontList& fonts) {
    QVector<ShowSpan> spans;
    QString currentFont;
    double currentSize = 0.0;
    for (int i = 0; i < tokens.size(); ++i) {
        if (tokens.at(i).type != ContentTokenType::Operator) continue;
        const QByteArray op = tokens.at(i).raw;
        if (op == "Tf") {
            // Tf 的操作数是「字号 资源名」：先遇到字号，再遇到资源名。
            const int sizeIndex = previousSignificant(tokens, i);
            const int nameIndex = sizeIndex >= 0 ? previousSignificant(tokens, sizeIndex) : -1;
            if (nameIndex >= 0 && sizeIndex >= 0 &&
                tokens.at(nameIndex).type == ContentTokenType::Name &&
                tokens.at(sizeIndex).type == ContentTokenType::Number) {
                QByteArray name = tokens.at(nameIndex).raw.mid(1);
                currentFont = QString::fromLatin1(name);
                currentSize = tokens.at(sizeIndex).raw.toDouble();
            }
            continue;
        }
        ShowSpan span;
        span.last = i;
        span.op = op;
        span.fontResource = currentFont;
        span.fontSize = currentSize;
        span.font = findFontResource(fonts, currentFont);

        if (op == "Tj" || op == "'" || op == "\"") {
            const int operand = previousSignificant(tokens, i);
            if (operand < 0) continue;
            if (tokens.at(operand).type != ContentTokenType::LiteralString &&
                tokens.at(operand).type != ContentTokenType::HexString) {
                continue;
            }
            span.first = operand;
            QByteArray bytes = tokens.at(operand).type == ContentTokenType::LiteralString
                                   ? decodeLiteral(tokens.at(operand).raw)
                                   : decodeHex(tokens.at(operand).raw);
            span.elements.append(ShowElement{operand,
                                             span.font != nullptr ? span.font->decode(bytes)
                                                                  : QString()});
            spans.append(span);
            continue;
        }
        if (op == "TJ") {
            const int arrayEnd = previousSignificant(tokens, i);
            if (arrayEnd < 0 || tokens.at(arrayEnd).type != ContentTokenType::ArrayEnd) continue;
            int depth = 0;
            int arrayStart = -1;
            for (int j = arrayEnd; j >= 0; --j) {
                if (tokens.at(j).type == ContentTokenType::ArrayEnd) ++depth;
                else if (tokens.at(j).type == ContentTokenType::ArrayStart) {
                    if (--depth == 0) {
                        arrayStart = j;
                        break;
                    }
                }
            }
            if (arrayStart < 0) continue;
            span.first = arrayStart;
            for (int j = arrayStart + 1; j < arrayEnd; ++j) {
                if (tokens.at(j).type != ContentTokenType::LiteralString &&
                    tokens.at(j).type != ContentTokenType::HexString) {
                    continue;
                }
                QByteArray bytes = tokens.at(j).type == ContentTokenType::LiteralString
                                       ? decodeLiteral(tokens.at(j).raw)
                                       : decodeHex(tokens.at(j).raw);
                span.elements.append(
                    ShowElement{j, span.font != nullptr ? span.font->decode(bytes) : QString()});
            }
            spans.append(span);
        }
    }
    return spans;
}

struct Match {
    int spanIndex = -1;
    int elementIndex = -1;
    int localPos = 0;
};

QString pageTextOf(const QVector<ShowSpan>& spans) {
    QString text;
    for (const ShowSpan& span : spans) {
        for (const ShowElement& element : span.elements) text += element.text;
    }
    return text;
}

QString formatAdjustment(double value) {
    if (std::abs(value) < 0.0005) return QStringLiteral("0");
    QString text = QString::number(value, 'f', 3);
    while (text.endsWith(QLatin1Char('0'))) text.chop(1);
    if (text.endsWith(QLatin1Char('.'))) text.chop(1);
    return text;
}

// 把一段文字（可能含多处替换）编码成 TJ 数组里的元素序列。
bool buildElementBody(const ShowSpan& span, const QString& text, const QVector<int>& matchPositions,
                      const QString& find, const QString& replace, QByteArray* out,
                      int* removedGlyphs, int* insertedGlyphs, QString* error) {
    out->clear();
    if (span.font == nullptr) {
        if (error) *error = QStringLiteral("页面上找不到字体资源：%1").arg(span.fontResource);
        return false;
    }
    int cursor = 0;
    for (int position : matchPositions) {
        const QString unchanged = text.mid(cursor, position - cursor);
        QByteArray encoded;
        if (!encodeWithFont(*span.font, unchanged, &encoded, error)) return false;
        *out += encoded;

        QByteArray replacement;
        if (!encodeWithFont(*span.font, replace, &replacement, error)) return false;
        *out += replacement;
        *removedGlyphs += find.size();
        *insertedGlyphs += replace.size();

        const double removedWidth = span.font->measure(find, span.fontSize);
        const double insertedWidth = span.font->measure(replace, span.fontSize);
        const double adjustment =
            -(removedWidth - insertedWidth) * 1000.0 / span.fontSize;
        *out += ' ';
        *out += formatAdjustment(adjustment).toLatin1();
        *out += ' ';
        cursor = position + find.size();
    }
    QByteArray tail;
    if (!encodeWithFont(*span.font, text.mid(cursor), &tail, error)) return false;
    *out += tail;
    return true;
}

QByteArray buildReplacement(const QVector<ContentToken>& tokens, const ShowSpan& span,
                            const QVector<QVector<int>>& matchesPerElement, const QString& find,
                            const QString& replace, int* removedGlyphs, int* insertedGlyphs,
                            QString* error) {
    QByteArray expression = "[";
    if (span.op == "TJ") {
        // 保留数组里原有的数字位移，只替换字符串元素。
        for (int i = span.first + 1; i < span.last; ++i) {
            if (tokens.at(i).type == ContentTokenType::ArrayEnd) break;
            const auto elementIt = std::find_if(
                span.elements.constBegin(), span.elements.constEnd(),
                [&](const ShowElement& element) { return element.tokenIndex == i; });
            if (elementIt != span.elements.constEnd()) {
                const int elementIndex = static_cast<int>(elementIt - span.elements.constBegin());
                QByteArray body;
                if (!buildElementBody(span, elementIt->text, matchesPerElement.at(elementIndex),
                                      find, replace, &body, removedGlyphs, insertedGlyphs,
                                      error)) {
                    return {};
                }
                expression += body;
                expression += ' ';
            } else {
                expression += tokens.at(i).raw;
                expression += ' ';
            }
        }
    } else {
        QByteArray body;
        if (!buildElementBody(span, span.elements.first().text, matchesPerElement.first(), find,
                              replace, &body, removedGlyphs, insertedGlyphs, error)) {
            return {};
        }
        expression += body;
    }
    expression += "] TJ";
    return expression;
}

#endif  // AIPDF_WITH_QPDF

}  // namespace

TextSpliceResult splicePageText(const QByteArray& pdfBytes, const TextSpliceTarget& target) {
    TextSpliceResult result;
    result.report.pageIndex = target.pageIndex;
    if (target.find.isEmpty()) {
        result.report.error = QStringLiteral("要替换的文字为空");
        return result;
    }
    if (target.pageIndex < 0) {
        result.report.error = QStringLiteral("页码不能为负");
        return result;
    }
    if (pdfBytes.isEmpty()) {
        result.report.error = QStringLiteral("PDF 字节为空");
        return result;
    }
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        pdf.processMemoryFile("text-splice.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));
        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();
        if (target.pageIndex >= static_cast<int>(pages.size())) {
            result.report.error = QStringLiteral("页码越界：%1（共 %2 页）")
                                      .arg(target.pageIndex)
                                      .arg(static_cast<int>(pages.size()));
            return result;
        }
        QPDFObjectHandle page = pages.at(static_cast<size_t>(target.pageIndex));
        page.coalesceContentStreams();
        const PageFontList fonts = readPageFonts(pdfBytes, target.pageIndex);

        QVector<QByteArray> streams;
        QVector<QVector<ContentToken>> tokens;
        std::vector<QPDFObjectHandle> contents = page.getPageContents();
        for (QPDFObjectHandle& stream : contents) {
            if (!stream.isStream()) continue;
            const std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
            if (!data) continue;
            streams.append(QByteArray(reinterpret_cast<const char*>(data->getBuffer()),
                                      static_cast<qsizetype>(data->getSize())));
            const ContentParseResult parsed = ContentStream::tokenize(streams.last());
            if (!parsed.isValid()) {
                result.report.error = QStringLiteral("内容流解析失败：%1").arg(parsed.error);
                return result;
            }
            tokens.append(parsed.tokens);
        }

        // 先扫一遍：把每段显示操作与它们的文字都列出来。
        QVector<QVector<ShowSpan>> streamSpans;
        QVector<ShowSpan> allSpans;
        QVector<int> spanStream;
        for (int index = 0; index < streams.size(); ++index) {
            QVector<ShowSpan> spans = scanShows(tokens.at(index), fonts);
            for (const ShowSpan& span : spans) {
                allSpans.append(span);
                spanStream.append(index);
            }
            streamSpans.append(spans);
        }

        QVector<Match> matches;
        for (int spanIndex = 0; spanIndex < allSpans.size(); ++spanIndex) {
            const ShowSpan& span = allSpans.at(spanIndex);
            if (span.op == "'" || span.op == "\"") {
                // 这两个操作符带换行语义，不能简单换成 TJ。
                if (span.elements.first().text.contains(target.find, target.sensitivity)) {
                    result.report.error =
                        QStringLiteral("匹配落在 ' 或 \" 操作符里，暂不支持替换");
                    return result;
                }
                continue;
            }
            if (span.font == nullptr) continue;
            for (int elementIndex = 0; elementIndex < span.elements.size(); ++elementIndex) {
                const QString& text = span.elements.at(elementIndex).text;
                int position = 0;
                while (true) {
                    const int found = text.indexOf(target.find, position, target.sensitivity);
                    if (found < 0) break;
                    matches.append(Match{spanIndex, elementIndex, found});
                    position = found + target.find.size();
                }
            }
        }
        if (matches.isEmpty()) {
            result.report.error = QStringLiteral("这一页上没有找到要替换的文字：%1").arg(target.find);
            return result;
        }

        QVector<Match> selected;
        if (target.occurrence < 0) {
            selected = matches;
        } else if (target.occurrence < matches.size()) {
            selected.append(matches.at(target.occurrence));
        } else {
            result.report.error = QStringLiteral("这一页只有 %1 处匹配，找不到第 %2 处")
                                      .arg(matches.size())
                                      .arg(target.occurrence);
            return result;
        }

        // 期望的文字：每个元素内把选中的匹配换掉，其余逐字保留。
        QVector<Match> ordered = selected;
        std::sort(ordered.begin(), ordered.end(), [](const Match& left, const Match& right) {
            if (left.spanIndex != right.spanIndex) return left.spanIndex < right.spanIndex;
            if (left.elementIndex != right.elementIndex) return left.elementIndex < right.elementIndex;
            return left.localPos < right.localPos;
        });
        QString rebuilt;
        for (int spanIndex = 0; spanIndex < allSpans.size(); ++spanIndex) {
            const ShowSpan& span = allSpans.at(spanIndex);
            for (int elementIndex = 0; elementIndex < span.elements.size(); ++elementIndex) {
                const QString& text = span.elements.at(elementIndex).text;
                QVector<int> positions;
                for (const Match& match : ordered) {
                    if (match.spanIndex == spanIndex && match.elementIndex == elementIndex) {
                        positions.append(match.localPos);
                    }
                }
                std::sort(positions.begin(), positions.end());
                int cursor = 0;
                for (int position : positions) {
                    rebuilt += text.mid(cursor, position - cursor);
                    rebuilt += target.replace;
                    cursor = position + target.find.size();
                }
                rebuilt += text.mid(cursor);
            }
        }
        const QString expected = rebuilt;

        // 每个 span 生成一条替换后的表达式；同一个 span 里的多处匹配一起处理。
        QHash<int, QVector<Match>> matchesBySpan;
        for (const Match& match : selected) matchesBySpan[match.spanIndex].append(match);

        QHash<int, QByteArray> expressionBySpan;
        QHash<int, QPair<int, int>> rangeBySpan;
        for (auto it = matchesBySpan.constBegin(); it != matchesBySpan.constEnd(); ++it) {
            const int spanIndex = it.key();
            const ShowSpan& span = allSpans.at(spanIndex);
            QVector<QVector<int>> perElement(span.elements.size());
            for (const Match& match : it.value()) perElement[match.elementIndex].append(match.localPos);
            for (QVector<int>& positions : perElement) std::sort(positions.begin(), positions.end());
            int removed = 0;
            int inserted = 0;
            QString error;
            const QByteArray expression =
                buildReplacement(tokens.at(spanStream.at(spanIndex)), span, perElement,
                                 target.find, target.replace, &removed, &inserted, &error);
            if (expression.isEmpty()) {
                result.report.error = error.isEmpty() ? QStringLiteral("改写失败") : error;
                return result;
            }
            expressionBySpan.insert(spanIndex, expression);
            rangeBySpan.insert(spanIndex, {span.first, span.last});
            result.report.occurrences += it.value().size();
            result.report.glyphsRemoved += removed;
            result.report.glyphsInserted += inserted;
            if (result.report.fontResource.isEmpty()) {
                result.report.fontResource = span.fontResource;
                result.report.baseFont = span.font != nullptr ? span.font->baseFont : QString();
            }
        }
        for (int i = 0; i < result.report.occurrences; ++i) {
            result.report.removed.append(target.find);
            result.report.inserted.append(target.replace);
        }

        // 逐流重建：同一条流里按 token 顺序走，遇到被替换的 span 就整段换掉。
        QVector<QByteArray> rewritten = streams;
        for (int streamIndex = 0; streamIndex < streams.size(); ++streamIndex) {
            QVector<int> spans;
            for (auto it = expressionBySpan.constBegin(); it != expressionBySpan.constEnd(); ++it) {
                if (spanStream.value(it.key()) == streamIndex) spans.append(it.key());
            }
            if (spans.isEmpty()) continue;
            std::sort(spans.begin(), spans.end(), [&](int left, int right) {
                return rangeBySpan.value(left).first < rangeBySpan.value(right).first;
            });
            QByteArray output;
            int tokenIndex = 0;
            int next = 0;
            const int tokenCount = tokens.at(streamIndex).size();
            while (tokenIndex < tokenCount) {
                if (next < spans.size() &&
                    rangeBySpan.value(spans.at(next)).first == tokenIndex) {
                    output += expressionBySpan.value(spans.at(next));
                    tokenIndex = rangeBySpan.value(spans.at(next)).second + 1;
                    ++next;
                    continue;
                }
                output += tokens.at(streamIndex).at(tokenIndex).raw;
                ++tokenIndex;
            }
            rewritten[streamIndex] = output;
        }

        // 写回 qpdf。
        int streamCursor = 0;
        for (QPDFObjectHandle& stream : contents) {
            if (!stream.isStream()) continue;
            if (streamCursor >= rewritten.size()) break;
            if (rewritten.at(streamCursor) != streams.at(streamCursor)) {
                stream.replaceStreamData(rewritten.at(streamCursor).toStdString(),
                                         QPDFObjectHandle::newNull(),
                                         QPDFObjectHandle::newNull());
            }
            ++streamCursor;
        }

        // 门禁：把改写后的流重新解析、重新解码，与期望串逐字比对。
        QString afterText;
        for (const QByteArray& streamBytes : rewritten) {
            const ContentParseResult parsed = ContentStream::tokenize(streamBytes);
            if (!parsed.isValid()) {
                result.report.error =
                    QStringLiteral("改写后的内容流解析失败：%1").arg(parsed.error);
                return result;
            }
            afterText += pageTextOf(scanShows(parsed.tokens, fonts));
        }
        if (afterText != expected) {
            result.report.error = QStringLiteral("改写后回读不一致：期望「%1」，实际「%2」")
                                      .arg(expected, afterText);
            return result;
        }

        QPDFWriter writer(pdf);
        writer.setOutputMemory();
        writer.write();
        std::unique_ptr<Buffer> output(writer.getBuffer());
        if (!output || output->getSize() == 0) {
            result.report.error = QStringLiteral("qpdf 写出的 PDF 为空");
            return result;
        }
        result.bytes = QByteArray(reinterpret_cast<const char*>(output->getBuffer()),
                                  static_cast<qsizetype>(output->getSize()));
    } catch (const std::exception& exception) {
        result.report.error = QString::fromUtf8(exception.what());
    }
#else
    Q_UNUSED(pdfBytes)
    Q_UNUSED(target)
    result.report.error = QStringLiteral("qpdf is unavailable");
#endif
    return result;
}

}  // namespace aipdf::pdf
