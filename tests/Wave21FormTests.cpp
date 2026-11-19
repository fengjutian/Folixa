// wave21：表单自动识别（无域表单页）+ 设计助手画版面与跨页。
//
// 这两件事最容易出的错是"看着对、其实错"：
//   * 识别：把正文段落、页眉页脚也报成字段——误报比漏报更糟，
//     用户点"全部创建"就会得到一份乱表单；
//   * 排版：字段互相重叠、跑出页面、放不下时硬挤在一页里。
// 所以这里逐条钉住行为（数量、位置、页号），不是只断言"没崩"。
//
// 夹具全部手写：Type0 / Identity-H + ToUnicode 的中文页，
// 线条与方框直接画在内容流里。不依赖外部夹具文件，
// 线条画在哪、标签写在哪，断言里能一一对上。

#include "pdf/forms/FormAutoDetect.h"
#include "pdf/forms/FormDesignParser.h"
#include "pdf/forms/FormFieldWriter.h"
#include "pdf/kernel/PdfiumAdapter.h"

#include <QHash>
#include <QRectF>
#include <QSet>
#include <QTest>
#include <QVector>

using aipdf::pdf::DetectedFieldKind;
using aipdf::pdf::DetectedFormField;
using aipdf::pdf::DesignedFormField;
using aipdf::pdf::FormDetectionResult;
using aipdf::pdf::FormFieldInsert;
using aipdf::pdf::FormFieldKind;
using aipdf::pdf::FormFieldWriteResult;
using aipdf::pdf::FormLayoutResult;
using aipdf::pdf::PlacedFormField;

namespace {

// ------------------------------------------------------------ 夹具构造 ----------

struct TextRun {
    double x = 0.0;
    double y = 0.0;
    double size = 12.0;
    QString text;
};

struct LineRun {
    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 0.0;
    double y2 = 0.0;
};

struct RectRun {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

struct PageSpec {
    QSizeF size{595.0, 842.0};
    QVector<TextRun> texts;
    QVector<LineRun> lines;
    QVector<RectRun> rects;
};

// 四位十六进制（不足补零）：CID 与 ToUnicode 的目标码都是这个形状。
QByteArray hex4(int value) {
    return QString::number(value, 16).toUpper().rightJustified(4, QLatin1Char('0')).toLatin1();
}

// 造一份多页 PDF：中文用 Type0 / Identity-H + ToUnicode，
// 线与框直接写进内容流。
//
// 为什么不内嵌字体：文本提取只依赖 ToUnicode，够验证"标签能被读出来"这条链路；
// 内嵌字体会把夹具撑到几十 KB，拖慢每一个用例。
QByteArray buildPdf(const QVector<PageSpec>& pages) {
    QHash<uint, int> cidOf;
    QVector<uint> cids;  // 下标 + 1 就是 CID
    auto cidFor = [&](uint codePoint) {
        const auto it = cidOf.constFind(codePoint);
        if (it != cidOf.constEnd()) return it.value();
        const int cid = cidOf.size() + 1;
        cidOf.insert(codePoint, cid);
        cids.append(codePoint);
        return cid;
    };

    QVector<QByteArray> contents;
    for (const PageSpec& page : pages) {
        QByteArray content = "0.8 w\n";
        for (const LineRun& line : page.lines) {
            content += QByteArray::number(line.x1, 'f', 2) + " " + QByteArray::number(line.y1, 'f', 2) +
                       " m " + QByteArray::number(line.x2, 'f', 2) + " " + QByteArray::number(line.y2, 'f', 2) +
                       " l S\n";
        }
        for (const RectRun& rect : page.rects) {
            content += QByteArray::number(rect.x, 'f', 2) + " " + QByteArray::number(rect.y, 'f', 2) +
                       " " + QByteArray::number(rect.width, 'f', 2) + " " +
                       QByteArray::number(rect.height, 'f', 2) + " re S\n";
        }
        for (const TextRun& text : page.texts) {
            QByteArray hex;
            const QVector<uint> codePoints = text.text.toUcs4();
            for (uint codePoint : codePoints) {
                hex += hex4(cidFor(codePoint));
                hex += " ";
            }
            content += "BT /F1 " + QByteArray::number(text.size, 'f', 2) + " Tf " +
                       QByteArray::number(text.x, 'f', 2) + " " + QByteArray::number(text.y, 'f', 2) +
                       " Td <" + hex.trimmed() + "> Tj ET\n";
        }
        contents.append(content);
    }

    QString bfChars;
    for (int index = 0; index < cids.size(); ++index) {
        const uint codePoint = cids.at(index);
        QString target;
        if (codePoint > 0xFFFF) {
            const char32_t unit = static_cast<char32_t>(codePoint);
            const QString utf16 = QString::fromUcs4(&unit, 1);
            for (const QChar ch : utf16) {
                target += hex4(ch.unicode());
            }
        } else {
            target = QString::fromLatin1(hex4(static_cast<int>(codePoint)));
        }
        bfChars += QStringLiteral("<%1> <%2>\n").arg(QString::fromLatin1(hex4(index + 1)), target);
    }
    const QString cmap =
        QStringLiteral("/CIDInit /ProcSet findresource begin\n"
                       "12 dict begin\n"
                       "begincmap\n"
                       "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> def\n"
                       "/CMapName /Adobe-Identity-UCS def\n"
                       "/CMapType 2 def\n"
                       "1 begincodespacerange\n"
                       "<0000> <FFFF>\n"
                       "endcodespacerange\n"
                       "%1 beginbfchar\n"
                       "%2"
                       "endbfchar\n"
                       "endcmap\n"
                       "CMapName currentdict /CMap defineresource pop\n"
                       "end\n"
                       "end\n")
            .arg(cids.size())
            .arg(bfChars);

    // 对象号：1 目录、2 页树、3 字体、4 子字体、5 ToUnicode、6 字体描述，
    // 7 之后每页两个对象（页面 + 内容流）。
    const int pageObjectBase = 7;
    QVector<QPair<int, QByteArray>> objects;
    objects.append({1, QByteArray("<< /Type /Catalog /Pages 2 0 R >>")});

    QString kids;
    for (int index = 0; index < pages.size(); ++index) {
        kids += QStringLiteral("%1 0 R ").arg(pageObjectBase + index * 2);
    }
    objects.append({2, QStringLiteral("<< /Type /Pages /Kids [%1] /Count %2 >>")
                           .arg(kids.trimmed())
                           .arg(pages.size())
                           .toLatin1()});
    objects.append({3, QByteArray("<< /Type /Font /Subtype /Type0 /BaseFont /STSong "
                                  "/Encoding /Identity-H /DescendantFonts [4 0 R] /ToUnicode 5 0 R >>")});
    objects.append({4, QByteArray("<< /Type /Font /Subtype /CIDFontType2 /BaseFont /STSong "
                                  "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) "
                                  "/Supplement 0 >> /FontDescriptor 6 0 R /DW 1000 >>")});
    objects.append({5, QStringLiteral("<< /Length %1 >>\nstream\n%2\nendstream")
                           .arg(cmap.toUtf8().size())
                           .arg(cmap)
                           .toLatin1()});
    objects.append({6, QByteArray("<< /Type /FontDescriptor /FontName /STSong /Flags 4 "
                                  "/FontBBox [0 0 1000 1000] /ItalicAngle 0 /Ascent 880 "
                                  "/Descent -120 /CapHeight 880 /StemV 80 >>")});

    for (int index = 0; index < pages.size(); ++index) {
        const PageSpec& page = pages.at(index);
        objects.append({pageObjectBase + index * 2,
                        QStringLiteral("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %1 %2] "
                                       "/Resources << /Font << /F1 3 0 R >> >> /Contents %3 0 R >>")
                            .arg(page.size.width(), 0, 'f', 2)
                            .arg(page.size.height(), 0, 'f', 2)
                            .arg(pageObjectBase + index * 2 + 1)
                            .toLatin1()});
        objects.append({pageObjectBase + index * 2 + 1,
                        QStringLiteral("<< /Length %1 >>\nstream\n%2\nendstream")
                            .arg(contents.at(index).size())
                            .arg(QString::fromLatin1(contents.at(index)))
                            .toLatin1()});
    }

    QByteArray out = QByteArray("%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");
    QHash<int, int> offsets;
    for (const auto& object : objects) {
        offsets.insert(object.first, out.size());
        out += QStringLiteral("%1 0 obj\n").arg(object.first).toLatin1();
        out += object.second;
        out += QByteArray("\nendobj\n");
    }
    const int xrefOffset = out.size();
    const int count = offsets.size() + 1;
    out += QStringLiteral("xref\n0 %1\n0000000000 65535 f \n").arg(count).toLatin1();
    for (int id = 1; id < count; ++id) {
        out += QStringLiteral("%1 00000 n \n")
                   .arg(offsets.value(id, 0), 10, QLatin1Char('0'))
                   .toLatin1();
    }
    out += QStringLiteral("trailer\n<< /Size %1 /Root 1 0 R >>\nstartxref\n%2\n%%EOF\n")
               .arg(count)
               .arg(xrefOffset)
               .toLatin1();
    return out;
}

// 一张"无域表单页"：页眉 + 三条下划线填空 + 一个方框 + 正文段落 + 页脚。
//
// 页眉与页脚都故意贴着一条横线：没有"页眉页脚要排除"这条规则，
// 它们就会被当成标签，把整页多报两个字段。
PageSpec formPage() {
    PageSpec page;
    page.size = QSizeF(595.0, 842.0);

    // 页眉：文字在左，横线在右，同一行。
    page.texts.append({60.0, 812.0, 14.0, QStringLiteral("员工信息登记表")});
    page.lines.append({200.0, 806.0, 535.0, 806.0});

    // 三个下划线填空：标签在左，横线在右。
    page.texts.append({60.0, 700.0, 12.0, QStringLiteral("姓名：")});
    page.lines.append({110.0, 692.0, 300.0, 692.0});
    page.texts.append({60.0, 660.0, 12.0, QStringLiteral("日期：")});
    page.lines.append({110.0, 652.0, 300.0, 652.0});
    page.texts.append({60.0, 620.0, 12.0, QStringLiteral("金额：")});
    page.lines.append({110.0, 612.0, 300.0, 612.0});

    // 一个方框：标签在左边，框在右边。
    page.texts.append({60.0, 515.0, 12.0, QStringLiteral("备注：")});
    page.rects.append({110.0, 495.0, 190.0, 40.0});

    // 正文段落：不能被当成字段。
    page.texts.append({60.0, 430.0, 12.0, QStringLiteral("本表用于登记员工基本信息，请如实填写。")});
    page.texts.append({60.0, 410.0, 12.0, QStringLiteral("填写完毕后请交人力资源部门留存备查。")});

    // 页脚：文字在左，横线在右，同样贴着。
    page.texts.append({60.0, 42.0, 10.0, QStringLiteral("内部资料")});
    page.lines.append({150.0, 38.0, 300.0, 38.0});
    return page;
}

// 一张普通正文页：只有段落，没有线。
PageSpec plainPage() {
    PageSpec page;
    page.size = QSizeF(595.0, 842.0);
    page.texts.append({60.0, 700.0, 12.0, QStringLiteral("第一章 总则")});
    page.texts.append({60.0, 670.0, 12.0, QStringLiteral("第一条 为规范公司档案管理，制定本制度。")});
    page.texts.append({60.0, 645.0, 12.0, QStringLiteral("第二条 档案管理人员负责档案的收集与归档。")});
    page.texts.append({60.0, 620.0, 12.0, QStringLiteral("第三条 本制度自发布之日起施行。")});
    return page;
}

// 找出一个候选（按名字）。
const DetectedFormField* findCandidate(const QVector<DetectedFormField>& candidates,
                                       const QString& name) {
    for (const DetectedFormField& candidate : candidates) {
        if (candidate.name == name) return &candidate;
    }
    return nullptr;
}

}  // namespace

class Wave21FormTests : public QObject {
    Q_OBJECT

private slots:
    void detectsFieldsOnFieldlessFormPage();
    void detectsFieldsOnSinglePage();
    void candidateRectanglesFollowDrawnLines();
    void labelsArePairedWithTheRightCandidate();
    void plainTextPageHasNoCandidates();
    void existingFieldsAreStillEnumerated();
    void supplementaryCandidatesAreKeptApart();
    void detectsTableCellsFromGridLines();
    void infersFieldKindFromLabelAndShape();
    void detectedKindNamesAreStable();
    void invalidDocumentFailsWithReason();
    void layoutPlacesFieldsInsidePage();
    void layoutFlowsToSecondPageWhenPageIsFull();
    void layoutNeverOverlapsFields();
    void layoutIgnoresModelCoordinates();
    void layoutReportsExtraPagesNeeded();
    void layoutWithoutPageSizesFails();
};

// 无域表单页：线框暗示出来的候选字段必须被找出来，而且要跟画线的地方对齐。
void Wave21FormTests::detectsFieldsOnFieldlessFormPage() {
    const QByteArray pdf = buildPdf({formPage()});
    const FormDetectionResult result = aipdf::pdf::detectFormFields(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.pageCount, 1);
    // 这一页本来就没有域：现有域必须是空的。
    QVERIFY(result.existingFields.isEmpty());
    // 三条下划线 + 一个方框 = 四个候选。
    // 页眉、页脚、正文段落都不该被算进来。
    QCOMPARE(result.candidates.size(), 4);
    QVERIFY(result.supplementary.isEmpty());

    QSet<QString> names;
    for (const DetectedFormField& candidate : result.candidates) {
        names.insert(candidate.name);
    }
    QVERIFY(names.contains(QStringLiteral("姓名")));
    QVERIFY(names.contains(QStringLiteral("日期")));
    QVERIFY(names.contains(QStringLiteral("金额")));
    QVERIFY(names.contains(QStringLiteral("备注")));
    // 页眉页脚与正文不是字段。
    QVERIFY(!names.contains(QStringLiteral("员工信息登记表")));
    QVERIFY(!names.contains(QStringLiteral("内部资料")));
}

// 逐页识别：页号必须对得上（表单页在第 2 页，候选就该报第 2 页）。
void Wave21FormTests::detectsFieldsOnSinglePage() {
    const QByteArray pdf = buildPdf({plainPage(), formPage()});
    const FormDetectionResult result = aipdf::pdf::detectFormFieldsOnPage(pdf, 1);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.candidates.size(), 4);
    for (const DetectedFormField& candidate : result.candidates) {
        QCOMPARE(candidate.pageIndex, 1);
        // 置信度要落在 0..1，界面按它排序。
        QVERIFY(candidate.confidence > 0.0);
        QVERIFY(candidate.confidence <= 1.0);
    }

    // 越界的页号要明确失败，不许静悄悄地返回空。
    const FormDetectionResult missing = aipdf::pdf::detectFormFieldsOnPage(pdf, 5);
    QVERIFY(!missing.ok);
    QVERIFY(!missing.error.isEmpty());
}

// 候选区域必须跟夹具里画的线对齐：位置错了，用户看到的框就飘了。
//
// 注意坐标：PDF 用户空间里 y 向上，QRectF 的 top() 是**小**的那个 y，
// 也就是视觉上的下边——画在下划线上的区域，下边就是那条线。
void Wave21FormTests::candidateRectanglesFollowDrawnLines() {
    const QByteArray pdf = buildPdf({formPage()});
    const FormDetectionResult result = aipdf::pdf::detectFormFields(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));

    // "姓名"的下划线画在 (110, 692) 到 (300, 692)：输入区就是这条线上方的一小块。
    const DetectedFormField* name = findCandidate(result.candidates, QStringLiteral("姓名"));
    QVERIFY(name != nullptr);
    QVERIFY2(qAbs(name->rect.left() - 110.0) < 1.0,
             qPrintable(QStringLiteral("左边界差了：%1").arg(name->rect.left())));
    QVERIFY2(qAbs(name->rect.right() - 300.0) < 1.0,
             qPrintable(QStringLiteral("右边界差了：%1").arg(name->rect.right())));
    QVERIFY2(qAbs(name->rect.top() - 692.0) < 1.0,
             qPrintable(QStringLiteral("下边界差了：%1").arg(name->rect.top())));
    QVERIFY2(name->rect.height() > 0.0, "候选区域不能没有高度");

    // "备注"的方框画在 (110, 495) 190×40：候选区就是框里面。
    const DetectedFormField* remark = findCandidate(result.candidates, QStringLiteral("备注"));
    QVERIFY(remark != nullptr);
    QVERIFY2(qAbs(remark->rect.left() - 110.0) < 1.0,
             qPrintable(QStringLiteral("左边界差了：%1").arg(remark->rect.left())));
    QVERIFY2(qAbs(remark->rect.top() - 495.0) < 1.0,
             qPrintable(QStringLiteral("下边界差了：%1").arg(remark->rect.top())));
    QVERIFY2(qAbs(remark->rect.height() - 40.0) < 1.0,
             qPrintable(QStringLiteral("高度差了：%1").arg(remark->rect.height())));
}

// 标签与候选格的配对不能串位：三个下划线挨得很近，配错了就全错。
void Wave21FormTests::labelsArePairedWithTheRightCandidate() {
    const QByteArray pdf = buildPdf({formPage()});
    const FormDetectionResult result = aipdf::pdf::detectFormFields(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));

    const DetectedFormField* name = findCandidate(result.candidates, QStringLiteral("姓名"));
    const DetectedFormField* date = findCandidate(result.candidates, QStringLiteral("日期"));
    const DetectedFormField* amount = findCandidate(result.candidates, QStringLiteral("金额"));
    QVERIFY(name != nullptr);
    QVERIFY(date != nullptr);
    QVERIFY(amount != nullptr);

    // 标签原文要留着（冒号也在），字段名才是去掉冒号的。
    QCOMPARE(name->label, QStringLiteral("姓名："));
    QCOMPARE(name->pageIndex, 0);

    // 三个候选的纵坐标必须分开，而且顺序跟画上去的顺序一致。
    QVERIFY(name->rect.top() > date->rect.top());
    QVERIFY(date->rect.top() > amount->rect.top());
    QVERIFY2(qAbs(name->rect.top() - 692.0) < 1.0, "姓名的位置串了");
    QVERIFY2(qAbs(date->rect.top() - 652.0) < 1.0, "日期的位置串了");
    QVERIFY2(qAbs(amount->rect.top() - 612.0) < 1.0, "金额的位置串了");
}

// 普通正文页绝不能被报成表单——误报比漏报更糟：
// 用户会拿着这些候选去建域，多出来的字段比漏掉的字段更麻烦。
void Wave21FormTests::plainTextPageHasNoCandidates() {
    const QByteArray pdf = buildPdf({plainPage()});
    const FormDetectionResult result = aipdf::pdf::detectFormFields(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.candidates.size(), 0);
    QCOMPARE(result.supplementary.size(), 0);
    QCOMPARE(result.totalCandidateCount(), 0);
}

// 已经有域的页：既有域照旧枚举，行为一个字都不变。
void Wave21FormTests::existingFieldsAreStillEnumerated() {
    PageSpec page;
    page.texts.append({60.0, 700.0, 12.0, QStringLiteral("姓名：")});
    page.lines.append({110.0, 692.0, 300.0, 692.0});
    page.texts.append({60.0, 660.0, 12.0, QStringLiteral("日期：")});
    page.lines.append({110.0, 652.0, 300.0, 652.0});
    const QByteArray source = buildPdf({page});

    FormFieldInsert insert;
    insert.pageIndex = 0;
    insert.name = QStringLiteral("existingName");
    insert.rect = QRectF(110.0, 692.0, 190.0, 14.0);
    insert.kind = FormFieldKind::Text;
    const FormFieldWriteResult written = aipdf::pdf::appendFormField(source, insert);
    QVERIFY2(written.ok, qPrintable(written.error));

    const FormDetectionResult result = aipdf::pdf::detectFormFields(written.bytes);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.existingFields.size(), 1);
    QCOMPARE(result.existingFields.first().name, QStringLiteral("existingName"));
    QCOMPARE(result.existingFields.first().pageIndex, 0);
}

// 已经有域的页上，"线框暗示但还没建域"的位置要单独列，不能混进候选里。
void Wave21FormTests::supplementaryCandidatesAreKeptApart() {
    PageSpec page;
    page.texts.append({60.0, 700.0, 12.0, QStringLiteral("姓名：")});
    page.lines.append({110.0, 692.0, 300.0, 692.0});
    page.texts.append({60.0, 660.0, 12.0, QStringLiteral("日期：")});
    page.lines.append({110.0, 652.0, 300.0, 652.0});
    const QByteArray source = buildPdf({page});

    FormFieldInsert insert;
    insert.pageIndex = 0;
    insert.name = QStringLiteral("existingName");
    insert.rect = QRectF(110.0, 692.0, 190.0, 14.0);
    insert.kind = FormFieldKind::Text;
    const FormFieldWriteResult written = aipdf::pdf::appendFormField(source, insert);
    QVERIFY2(written.ok, qPrintable(written.error));

    const FormDetectionResult result = aipdf::pdf::detectFormFields(written.bytes);
    QVERIFY2(result.ok, qPrintable(result.error));
    // 已经有域的页：候选不进 candidates。
    QVERIFY(result.candidates.isEmpty());
    // 被已有域盖住的那个位置不算数，另一个位置进补充候选。
    QCOMPARE(result.supplementary.size(), 1);
    QCOMPARE(result.supplementary.first().name, QStringLiteral("日期"));
}

// 表格：格子由横线竖线围出来，左边那格的文字就是这一格的标签。
void Wave21FormTests::detectsTableCellsFromGridLines() {
    PageSpec page;
    // 两行两列的表格：左边一列是标签，右边一列是空的。
    page.lines.append({60.0, 700.0, 360.0, 700.0});
    page.lines.append({60.0, 660.0, 360.0, 660.0});
    page.lines.append({60.0, 620.0, 360.0, 620.0});
    page.lines.append({60.0, 700.0, 60.0, 620.0});
    page.lines.append({200.0, 700.0, 200.0, 620.0});
    page.lines.append({360.0, 700.0, 360.0, 620.0});
    page.texts.append({70.0, 675.0, 12.0, QStringLiteral("姓名")});
    page.texts.append({70.0, 635.0, 12.0, QStringLiteral("日期")});

    const QByteArray pdf = buildPdf({page});
    const FormDetectionResult result = aipdf::pdf::detectFormFields(pdf);
    QVERIFY2(result.ok, qPrintable(result.error));
    // 两个空格各一个候选；左边写着字的格子不算。
    QCOMPARE(result.candidates.size(), 2);
    const DetectedFormField* name = findCandidate(result.candidates, QStringLiteral("姓名"));
    const DetectedFormField* date = findCandidate(result.candidates, QStringLiteral("日期"));
    QVERIFY(name != nullptr);
    QVERIFY(date != nullptr);
    QVERIFY2(qAbs(name->rect.left() - 200.0) < 1.0, "格子的左边界不对");
    QVERIFY2(qAbs(name->rect.top() - 660.0) < 1.0, "格子的下边界不对");
    QVERIFY2(qAbs(date->rect.top() - 620.0) < 1.0, "格子的下边界不对");
}

// 类型推断：看标签文字，也看框的形状。
void Wave21FormTests::infersFieldKindFromLabelAndShape() {
    using aipdf::pdf::inferFieldKind;
    QCOMPARE(inferFieldKind(QStringLiteral("日期："), QRectF(0, 0, 190, 14)), DetectedFieldKind::Date);
    QCOMPARE(inferFieldKind(QStringLiteral("金额："), QRectF(0, 0, 190, 14)), DetectedFieldKind::Number);
    QCOMPARE(inferFieldKind(QStringLiteral("电话："), QRectF(0, 0, 190, 14)), DetectedFieldKind::Number);
    QCOMPARE(inferFieldKind(QStringLiteral("是否同意："), QRectF(0, 0, 190, 14)),
             DetectedFieldKind::CheckBox);
    QCOMPARE(inferFieldKind(QStringLiteral("备注："), QRectF(0, 0, 190, 40)), DetectedFieldKind::Text);
    // 小方框是勾选框，跟标签说什么没关系。
    QCOMPARE(inferFieldKind(QStringLiteral("同意"), QRectF(0, 0, 12, 12)), DetectedFieldKind::CheckBox);
}

void Wave21FormTests::detectedKindNamesAreStable() {
    QCOMPARE(aipdf::pdf::detectedKindName(DetectedFieldKind::Text), QStringLiteral("text"));
    QCOMPARE(aipdf::pdf::detectedKindName(DetectedFieldKind::Number), QStringLiteral("number"));
    QCOMPARE(aipdf::pdf::detectedKindName(DetectedFieldKind::Date), QStringLiteral("date"));
    QCOMPARE(aipdf::pdf::detectedKindName(DetectedFieldKind::CheckBox), QStringLiteral("checkbox"));
}

// 坏输入必须明确失败，不许静悄悄地返回空结果。
void Wave21FormTests::invalidDocumentFailsWithReason() {
    const FormDetectionResult result =
        aipdf::pdf::detectFormFields(QByteArray("this is not a pdf at all"));
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());

    const FormDetectionResult empty = aipdf::pdf::detectFormFields(QByteArray());
    QVERIFY(!empty.ok);
    QVERIFY(!empty.error.isEmpty());
}

// 版面：每个字段都要落在页面里面，不能跑到页边距外面去。
void Wave21FormTests::layoutPlacesFieldsInsidePage() {
    QVector<DesignedFormField> fields;
    for (int index = 0; index < 3; ++index) {
        DesignedFormField field;
        field.name = QStringLiteral("field%1").arg(index + 1);
        fields.append(field);
    }
    const QVector<QSizeF> pages = {QSizeF(595.0, 842.0)};
    const FormLayoutResult layout = aipdf::pdf::layoutDesignedFields(fields, pages, 0);
    QVERIFY2(layout.ok, qPrintable(layout.error));
    QCOMPARE(layout.placements.size(), 3);
    QCOMPARE(layout.pagesUsed, 1);
    for (const PlacedFormField& placement : layout.placements) {
        QVERIFY2(placement.rect.left() >= 0.0, "字段跑到了页面左边外面");
        QVERIFY2(placement.rect.right() <= 595.0, "字段跑到了页面右边外面");
        QVERIFY2(placement.rect.top() >= 0.0, "字段跑到了页面下边外面");
        QVERIFY2(placement.rect.bottom() <= 842.0, "字段跑到了页面上边外面");
        QVERIFY(placement.rect.width() > 0.0);
        QVERIFY(placement.rect.height() > 0.0);
    }
}

// 一页放不下时必须顺延到下一页，而不是硬挤在一页里。
void Wave21FormTests::layoutFlowsToSecondPageWhenPageIsFull() {
    QVector<DesignedFormField> fields;
    for (int index = 0; index < 5; ++index) {
        DesignedFormField field;
        field.name = QStringLiteral("field%1").arg(index + 1);
        fields.append(field);
    }
    const QVector<QSizeF> pages = {QSizeF(200.0, 200.0), QSizeF(200.0, 200.0)};
    const FormLayoutResult layout = aipdf::pdf::layoutDesignedFields(fields, pages, 0);
    QVERIFY2(layout.ok, qPrintable(layout.error));
    QCOMPARE(layout.placements.size(), 5);
    QVERIFY2(layout.pagesUsed == 2, qPrintable(QStringLiteral("用了几页：%1").arg(layout.pagesUsed)));
    // 最后一个字段必须落到第 2 页（页号从 0 起就是 1）。
    QCOMPARE(layout.placements.last().pageIndex, 1);
    QCOMPARE(layout.placements.last().pageOffset, 1);
    // 前面的字段留在第 1 页。
    QCOMPARE(layout.placements.first().pageIndex, 0);
    // 两页都够用，不需要补页。
    QCOMPARE(layout.extraPagesNeeded, 0);
}

// 同一页里的字段不能互相重叠。
void Wave21FormTests::layoutNeverOverlapsFields() {
    QVector<DesignedFormField> fields;
    for (int index = 0; index < 12; ++index) {
        DesignedFormField field;
        field.name = QStringLiteral("field%1").arg(index + 1);
        fields.append(field);
    }
    const QVector<QSizeF> pages = {QSizeF(200.0, 200.0), QSizeF(200.0, 200.0), QSizeF(200.0, 200.0)};
    const FormLayoutResult layout = aipdf::pdf::layoutDesignedFields(fields, pages, 0);
    QVERIFY2(layout.ok, qPrintable(layout.error));
    QCOMPARE(layout.placements.size(), 12);
    for (int i = 0; i < layout.placements.size(); ++i) {
        for (int j = i + 1; j < layout.placements.size(); ++j) {
            const PlacedFormField& first = layout.placements.at(i);
            const PlacedFormField& second = layout.placements.at(j);
            if (first.pageIndex != second.pageIndex) continue;
            QVERIFY2(!first.rect.intersects(second.rect),
                     qPrintable(QStringLiteral("第 %1 与第 %2 个字段重叠了").arg(i + 1).arg(j + 1)));
        }
    }
}

// 版面是确定性计算：模型给的坐标不参与排布。
void Wave21FormTests::layoutIgnoresModelCoordinates() {
    QVector<DesignedFormField> first;
    QVector<DesignedFormField> second;
    for (int index = 0; index < 4; ++index) {
        DesignedFormField field;
        field.name = QStringLiteral("field%1").arg(index + 1);
        field.relX = 0.9;
        field.relY = 0.9;
        field.relWidth = 0.05;
        field.relHeight = 0.01;
        first.append(field);

        DesignedFormField other = field;
        other.relX = 0.0;
        other.relY = 0.0;
        other.relWidth = 0.5;
        other.relHeight = 0.2;
        second.append(other);
    }
    const QVector<QSizeF> pages = {QSizeF(595.0, 842.0)};
    const FormLayoutResult a = aipdf::pdf::layoutDesignedFields(first, pages, 0);
    const FormLayoutResult b = aipdf::pdf::layoutDesignedFields(second, pages, 0);
    QVERIFY(a.ok);
    QVERIFY(b.ok);
    QCOMPARE(a.placements.size(), b.placements.size());
    for (int index = 0; index < a.placements.size(); ++index) {
        QVERIFY2(a.placements.at(index).rect == b.placements.at(index).rect,
                 "模型给的坐标影响了版面");
    }
}

// 页面不够时要如实说差几页：调用方得先补页才能建域。
void Wave21FormTests::layoutReportsExtraPagesNeeded() {
    QVector<DesignedFormField> fields;
    for (int index = 0; index < 5; ++index) {
        DesignedFormField field;
        field.name = QStringLiteral("field%1").arg(index + 1);
        fields.append(field);
    }
    const QVector<QSizeF> pages = {QSizeF(200.0, 200.0)};
    const FormLayoutResult layout = aipdf::pdf::layoutDesignedFields(fields, pages, 0);
    QVERIFY2(layout.ok, qPrintable(layout.error));
    QCOMPARE(layout.placements.size(), 5);
    QVERIFY2(layout.extraPagesNeeded >= 1,
             qPrintable(QStringLiteral("还差 %1 页").arg(layout.extraPagesNeeded)));
    QVERIFY2(layout.placements.last().pageIndex >= 1, "最后一项应该排在第一页之后");
}

// 没有页面尺寸就排不了版面：必须明确失败，不能悄悄给一个假版面。
void Wave21FormTests::layoutWithoutPageSizesFails() {
    QVector<DesignedFormField> fields;
    DesignedFormField field;
    field.name = QStringLiteral("field1");
    fields.append(field);
    const FormLayoutResult layout = aipdf::pdf::layoutDesignedFields(fields, {}, 0);
    QVERIFY(!layout.ok);
    QVERIFY(!layout.error.isEmpty());

    const FormLayoutResult empty = aipdf::pdf::layoutDesignedFields({}, {QSizeF(595.0, 842.0)}, 0);
    QVERIFY(!empty.ok);
    QVERIFY(!empty.error.isEmpty());
}

QTEST_MAIN(Wave21FormTests)
#include "Wave21FormTests.moc"
