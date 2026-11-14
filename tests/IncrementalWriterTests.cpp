// 增量写入的测试：给批注补 /AP 之后，文件还在不在、原字节动没动、别人看不看得见。
//
// 这条路径最怕两件事：把文件写坏（打不开），或者写完只有自己认（别的阅读器看不见）。
// 所以四类断言缺一不可：
//   1) 结构：原字节一字不改，新对象与 xref 追加在尾部，重新打开仍然合法；
//   2) 数据：批注还在、/NM 标记还在、类型没变；
//   3) 可见性：同一页在补 /AP 前后的渲染差异，必须正好落在批注矩形里；
//   4) 失败语义：找不到标记 / 外观流为空时必须明确失败，不能返回半成品。

#include "pdf/Annotation.h"
#include "pdf/AppearanceStream.h"
#include "pdf/IncrementalWriter.h"
#include "pdf/PdfiumAdapter.h"

#include <QImage>
#include <QTest>

#include <algorithm>

using aipdf::pdf::Annotation;
using aipdf::pdf::AnnotationType;
using aipdf::pdf::appendAnnotationAppearance;
using aipdf::pdf::appendAnnotationAppearances;
using aipdf::pdf::Appearance;
using aipdf::pdf::AppearanceAttachment;
using aipdf::pdf::buildAnnotationAppearance;
using aipdf::pdf::openDocumentFromBytes;
using aipdf::pdf::openDocument;
using aipdf::pdf::PdfDocument;

namespace {

QString fixturePath(const QString& name) {
    return QStringLiteral(AIPDF_PDF_FIXTURES_DIR "/") + name;
}

Annotation makeHighlight(quint64 id) {
    Annotation annotation;
    annotation.id = id;
    annotation.pageIndex = 0;
    annotation.type = AnnotationType::Highlight;
    annotation.rect = QRectF(60.0, 200.0, 200.0, 18.0);
    annotation.quads = { QRectF(60.0, 200.0, 200.0, 18.0) };
    annotation.color = QColor(255, 214, 0);
    return annotation;
}

// 把批注写进一份真实文档，返回 PDFium 保存出来的字节。
QByteArray documentWithHighlight(quint64 id) {
    const auto result = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    if (!result.document.isValid()) {
        return {};
    }
    auto page = result.document.page(0);
    if (!page.has_value()) {
        return {};
    }
    if (!page->setAnnotations({ makeHighlight(id) })) {
        return {};
    }
    return result.document.serialize();
}

QByteArray markerFor(quint64 id) {
    return QByteArray("Folixa:") + QByteArray::number(id);
}

int fixturePageCount() {
    const auto result = openDocument(fixturePath(QStringLiteral("annotation.pdf")));
    return result.document.isValid() ? result.document.pageCount() : -1;
}

struct PixelDiff {
    int count = 0;
    QRect bounds;
};

// ---------------------------------------------------------------- 结构校验 ----
//
// 一个独立于写入器的 xref 链检查器。存在的理由：PDFium 对坏文件相当宽容，
// "能打开"不等于"结构正确"——偏移写错、链断掉，它可能照样渲染，别的阅读器就未必。
// 这里按规范逐项核：startxref → xref 段 → 每个在用条目指向的确实是 "<n> <g> obj"，
// 再沿 /Prev 一路回溯到文件头。
//
// 这不是产品代码，是测试用的对照物：它和写入器不共享任何实现，写错了才会被抓到。

class Scanner {
public:
    explicit Scanner(const QByteArray& pdf) : pdf_(pdf) {}

    void skipSpace() {
        while (pos_ < pdf_.size()) {
            const char c = pdf_.at(pos_);
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool readInt(long* value) {
        skipSpace();
        const qsizetype begin = pos_;
        while (pos_ < pdf_.size() && pdf_.at(pos_) >= '0' && pdf_.at(pos_) <= '9') {
            ++pos_;
        }
        if (pos_ == begin) {
            return false;
        }
        *value = pdf_.mid(begin, pos_ - begin).toLong();
        return true;
    }

    bool consume(const char* token) {
        skipSpace();
        const QByteArray text(token);
        if (pdf_.mid(pos_, text.size()) != text) {
            return false;
        }
        pos_ += text.size();
        return true;
    }

    void setPosition(qsizetype pos) { pos_ = pos; }
    void advance() { ++pos_; }
    qsizetype position() const { return pos_; }
    qsizetype size() const { return pdf_.size(); }
    char at(qsizetype index) const { return pdf_.at(index); }

private:
    const QByteArray& pdf_;
    qsizetype pos_ = 0;
};

bool verifyXrefChain(const QByteArray& pdf, QString* error) {
    const qsizetype startxrefAt = pdf.lastIndexOf("startxref");
    if (startxrefAt < 0) {
        *error = QStringLiteral("没有 startxref");
        return false;
    }
    Scanner scanner(pdf);
    scanner.setPosition(startxrefAt + 9);
    long offset = 0;
    if (!scanner.readInt(&offset) || offset <= 0 || offset >= pdf.size()) {
        *error = QStringLiteral("startxref 指向越界：%1").arg(offset);
        return false;
    }

    QVector<long> visited;
    long pendingObjects = 0;
    while (offset > 0) {
        if (visited.contains(offset)) {
            *error = QStringLiteral("/Prev 出现环：%1").arg(offset);
            return false;
        }
        if (visited.size() > 64) {
            *error = QStringLiteral("xref 链过长");
            return false;
        }
        visited.append(offset);

        scanner.setPosition(offset);
        if (!scanner.consume("xref")) {
            *error = QStringLiteral("偏移 %1 处不是 xref").arg(offset);
            return false;
        }

        bool sawSection = false;
        while (true) {
            const qsizetype mark = scanner.position();
            long first = 0;
            if (!scanner.readInt(&first)) {
                scanner.setPosition(mark);
                break;
            }
            long count = 0;
            if (!scanner.readInt(&count) || count < 0 || count > 100000) {
                *error = QStringLiteral("xref 段头非法");
                return false;
            }
            sawSection = true;

            for (long i = 0; i < count; ++i) {
                long entryOffset = 0;
                long generation = 0;
                if (!scanner.readInt(&entryOffset) || !scanner.readInt(&generation)) {
                    *error = QStringLiteral("xref 条目被截断");
                    return false;
                }
                scanner.skipSpace();
                if (scanner.position() >= pdf.size()) {
                    *error = QStringLiteral("xref 条目缺类型");
                    return false;
                }
                const char type = scanner.at(scanner.position());
                scanner.advance();
                if (type != 'n' && type != 'f') {
                    *error = QStringLiteral("xref 条目类型非法：%1").arg(QLatin1Char(type));
                    return false;
                }
                ++pendingObjects;

                if (type == 'n' && entryOffset > 0) {
                    // 条目必须精确指向 "<对象号> <代号> obj"。
                    const QByteArray expected =
                        QByteArray::number(first + i) + " " + QByteArray::number(generation) + " obj";
                    if (pdf.mid(entryOffset, expected.size()) != expected) {
                        *error = QStringLiteral("对象 %1 的偏移 %2 没指向 \"%3\"")
                                     .arg(first + i)
                                     .arg(entryOffset)
                                     .arg(QString::fromLatin1(expected));
                        return false;
                    }
                }
            }
        }
        if (!sawSection) {
            *error = QStringLiteral("偏移 %1 处的 xref 没有条目").arg(offset);
            return false;
        }

        if (!scanner.consume("trailer")) {
            *error = QStringLiteral("偏移 %1 处的 xref 后没有 trailer").arg(offset);
            return false;
        }
        const qsizetype trailerAt = scanner.position();
        const qsizetype dictionaryEnd = pdf.indexOf("startxref", trailerAt);
        if (dictionaryEnd < 0) {
            *error = QStringLiteral("trailer 之后没有 startxref");
            return false;
        }
        const QByteArray trailer = pdf.mid(trailerAt, dictionaryEnd - trailerAt);
        if (!trailer.contains("/Root")) {
            *error = QStringLiteral("trailer 缺 /Root");
            return false;
        }
        const qsizetype prevAt = trailer.indexOf("/Prev");
        if (prevAt < 0) {
            break;  // 到最老的一段了
        }
        qsizetype cursor = prevAt + 5;
        while (cursor < trailer.size() &&
               (trailer.at(cursor) < '0' || trailer.at(cursor) > '9')) {
            ++cursor;
        }
        const qsizetype digitsBegin = cursor;
        while (cursor < trailer.size() && trailer.at(cursor) >= '0' && trailer.at(cursor) <= '9') {
            ++cursor;
        }
        offset = trailer.mid(digitsBegin, cursor - digitsBegin).toLong();
        if (offset <= 0) {
            break;
        }
    }

    if (pendingObjects <= 0) {
        *error = QStringLiteral("一个对象都没扫到");
        return false;
    }
    return true;
}


PixelDiff comparePixels(const QImage& before, const QImage& after) {
    PixelDiff diff;
    if (before.size() != after.size() || before.isNull()) {
        return diff;
    }
    int minX = before.width(), maxX = -1, minY = before.height(), maxY = -1;
    for (int y = 0; y < before.height(); ++y) {
        for (int x = 0; x < before.width(); ++x) {
            const QRgb a = before.pixel(x, y);
            const QRgb b = after.pixel(x, y);
            if (qAbs(qRed(a) - qRed(b)) > 2 || qAbs(qGreen(a) - qGreen(b)) > 2 ||
                qAbs(qBlue(a) - qBlue(b)) > 2) {
                ++diff.count;
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
    }
    if (diff.count > 0) {
        diff.bounds = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
    }
    return diff;
}

}  // namespace

class IncrementalWriterTests : public QObject {
    Q_OBJECT

private slots:
    void appendedFileKeepsOriginalBytesIntact();
    void appendedFileStillOpensWithAnnotation();
    void appearanceChangesPixelsInsideRectOnly();
    void unknownMarkerIsRejected();
    void emptyAppearanceIsRejected();
    void truncatedPdfIsRejected();
    void duplicateMarkerProducesSingleAppearance();
    void appearanceContentIsActuallyDrawn();
    void incrementalFileHasValidXrefChain();
    void corruptXrefOffsetIsCaught();
};

void IncrementalWriterTests::appendedFileKeepsOriginalBytesIntact() {
    const QByteArray original = documentWithHighlight(7);
    QVERIFY2(!original.isEmpty(), "夹具文档应当能序列化");

    const auto appearance = buildAnnotationAppearance(makeHighlight(7));
    const auto write = appendAnnotationAppearance(original, markerFor(7), appearance);

    QVERIFY2(write.ok, qPrintable(write.error));
    QVERIFY(write.bytes.size() > original.size());
    // 增量写入的定义：前缀一个字节都不能变。
    QCOMPARE(write.bytes.left(original.size()), original);
    QVERIFY(write.bytes.contains("/AP"));
    QCOMPARE(write.appearanceCount, 1);
    QCOMPARE(write.rewrittenObjects, 1);
}

void IncrementalWriterTests::appendedFileStillOpensWithAnnotation() {
    const QByteArray original = documentWithHighlight(7);
    const auto write =
        appendAnnotationAppearance(original, markerFor(7), buildAnnotationAppearance(makeHighlight(7)));
    QVERIFY(write.ok);

    const auto reopened = openDocumentFromBytes(write.bytes);
    QVERIFY(reopened.document.isValid());
    // 页数必须与夹具原文档一致：写成 0 页是最典型的「结构写坏」。
    QCOMPARE(reopened.document.pageCount(), fixturePageCount());
    QVERIFY(reopened.document.pageCount() > 0);

    auto page = reopened.document.page(0);
    QVERIFY(page.has_value());
    const auto annotations = page->annotations();
    QCOMPARE(annotations.size(), 1);
    QCOMPARE(annotations.first().type, AnnotationType::Highlight);
    // /NM 里存的 id 必须原样读回来，否则撤销栈与文档对不上。
    QCOMPARE(annotations.first().id, quint64(7));
}

void IncrementalWriterTests::appearanceChangesPixelsInsideRectOnly() {
    const QByteArray original = documentWithHighlight(7);
    const Annotation model = makeHighlight(7);
    const auto write =
        appendAnnotationAppearance(original, markerFor(7), buildAnnotationAppearance(model));
    QVERIFY(write.ok);

    const auto before = openDocumentFromBytes(original);
    const auto after = openDocumentFromBytes(write.bytes);
    QVERIFY(before.document.isValid());
    QVERIFY(after.document.isValid());
    const auto pageBefore = before.document.page(0);
    const auto pageAfter = after.document.page(0);
    QVERIFY(pageBefore.has_value());
    QVERIFY(pageAfter.has_value());

    const QImage imageBefore = pageBefore->render(1.0);
    const QImage imageAfter = pageAfter->render(1.0);
    QCOMPARE(imageBefore.size(), imageAfter.size());
    QVERIFY(!imageBefore.isNull());

    const PixelDiff diff = comparePixels(imageBefore, imageAfter);
    // 光栅化会有 1px 边缘误差，给 ±2 的余量；面积对不上说明外观流被缩放或裁剪了。
    QVERIFY2(diff.count > 0, "补了 /AP 却什么都没画出来");
    QVERIFY2(qAbs(diff.bounds.width() - 200) <= 2,
             qPrintable(QStringLiteral("差异宽 %1").arg(diff.bounds.width())));
    QVERIFY2(qAbs(diff.bounds.height() - 18) <= 2,
             qPrintable(QStringLiteral("差异高 %1").arg(diff.bounds.height())));
}

void IncrementalWriterTests::truncatedPdfIsRejected() {
    const QByteArray original = documentWithHighlight(7);
    // 砍掉尾部：没有 trailer、没有 startxref，写入器必须拒绝而不是猜着写。
    const QByteArray truncated = original.left(original.size() / 2);

    const auto write = appendAnnotationAppearance(truncated, markerFor(7),
                                                  buildAnnotationAppearance(makeHighlight(7)));
    QVERIFY(!write.ok);
    QVERIFY(write.bytes.isEmpty());
    QVERIFY(!write.error.isEmpty());
}

void IncrementalWriterTests::duplicateMarkerProducesSingleAppearance() {
    const QByteArray original = documentWithHighlight(7);
    const auto appearance = buildAnnotationAppearance(makeHighlight(7));

    const auto write = appendAnnotationAppearances(
        original, { { markerFor(7), appearance }, { markerFor(7), appearance } });

    QVERIFY(write.ok);
    // 同一个标记补两次会在字典里插出两个 /AP 键，谁生效取决于阅读器——必须去重。
    QCOMPARE(write.appearanceCount, 1);
    QCOMPARE(write.bytes.count("/AP << /N"), 1);
}

void IncrementalWriterTests::appearanceContentIsActuallyDrawn() {
    // 一条正例 + 一条反例，合起来才算守卫：
    //   正例：把整块外观填成纯蓝，矩形中心必须变蓝 → 证明画的是我们的流；
    //   反例：content 为空时中心不能是蓝 → 证明上一条不是"怎么改都成立"。
    const QByteArray original = documentWithHighlight(7);
    const auto originalPage = openDocumentFromBytes(original).document.page(0);
    QVERIFY(originalPage.has_value());
    const QImage baseline = originalPage->render(1.0);

    auto centerAfterPatching = [&](const QByteArray& content) {
        Appearance appearance = buildAnnotationAppearance(makeHighlight(7));
        appearance.content = content;
        const auto write = appendAnnotationAppearance(original, markerFor(7), appearance);
        if (!write.ok) {
            return QColor();
        }
        const auto patchedPage = openDocumentFromBytes(write.bytes).document.page(0);
        if (!patchedPage.has_value()) {
            return QColor();
        }
        const QImage image = patchedPage->render(1.0);
        // 矩形中心在页面坐标 (160, 209)，图像 y 要翻转。
        return image.pixelColor(160, image.height() - 209);
    };

    const QColor blue = centerAfterPatching("q\n0 0 1 rg\n0 0 200 18 re\nf\nQ\n");
    QVERIFY2(blue.isValid(), "补外观失败");
    QVERIFY2(blue.blue() > 200 && blue.red() < 60,
             qPrintable(QStringLiteral("中心颜色 rgb(%1,%2,%3)，期望蓝")
                            .arg(blue.red())
                            .arg(blue.green())
                            .arg(blue.blue())));

    const QColor empty = centerAfterPatching("q\nQ\n");
    QVERIFY2(empty.isValid(), "补外观失败");
    // 空白外观时 PDFium 仍会按 /QuadPoints 合成高亮底色，所以这里只能断言"不是我们那层蓝"。
    QVERIFY2(!(empty.blue() > 200 && empty.red() < 60),
             qPrintable(QStringLiteral("空内容也画出了蓝，守卫失效：rgb(%1,%2,%3)")
                            .arg(empty.red())
                            .arg(empty.green())
                            .arg(empty.blue())));
}

void IncrementalWriterTests::unknownMarkerIsRejected() {
    const QByteArray original = documentWithHighlight(7);
    const auto write =
        appendAnnotationAppearance(original, markerFor(99), buildAnnotationAppearance(makeHighlight(7)));

    QVERIFY(!write.ok);
    QVERIFY(write.bytes.isEmpty());
    QVERIFY(!write.error.isEmpty());
}

void IncrementalWriterTests::emptyAppearanceIsRejected() {
    const QByteArray original = documentWithHighlight(7);
    Annotation note = makeHighlight(7);
    note.type = AnnotationType::Note;  // 便签没有图形外观

    const auto write = appendAnnotationAppearance(original, markerFor(7),
                                                  buildAnnotationAppearance(note));
    QVERIFY(!write.ok);
    QVERIFY(write.bytes.isEmpty());
}

void IncrementalWriterTests::incrementalFileHasValidXrefChain() {
    const QByteArray original = documentWithHighlight(7);
    const auto write = appendAnnotationAppearance(original, markerFor(7),
                                                  buildAnnotationAppearance(makeHighlight(7)));
    QVERIFY(write.ok);

    QString error;
    // 原文件本身要过，追加之后的文件也要过——否则"能打开"只是 PDFium 宽容。
    QVERIFY2(verifyXrefChain(original, &error), qPrintable(error));
    QVERIFY2(verifyXrefChain(write.bytes, &error), qPrintable(error));
}

void IncrementalWriterTests::corruptXrefOffsetIsCaught() {
    const QByteArray original = documentWithHighlight(7);
    const auto write = appendAnnotationAppearance(original, markerFor(7),
                                                  buildAnnotationAppearance(makeHighlight(7)));
    QVERIFY(write.ok);

    // 负例守卫：把新 xref 里某个条目的偏移改掉一位，校验器必须抓出来。
    QByteArray corrupt = write.bytes;
    const qsizetype entry = corrupt.lastIndexOf(" 00000 n");
    QVERIFY(entry > 10);
    const qsizetype offsetAt = entry - 10;
    corrupt[offsetAt] = corrupt.at(offsetAt) == '0' ? '1' : '0';

    QString error;
    QVERIFY2(!verifyXrefChain(corrupt, &error), "改坏的 xref 居然通过了校验");
    QVERIFY(!error.isEmpty());
}

QTEST_MAIN(IncrementalWriterTests)
#include "IncrementalWriterTests.moc"
