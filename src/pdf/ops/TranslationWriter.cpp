#include "pdf/ops/TranslationWriter.h"

#include "pdf/ops/StreamingEdit.h"

#include "pdf/content/TextReflow.h"

#include "i18n/Translate.h"

#include <QFont>
#include <QFontMetricsF>

namespace aipdf::pdf {
namespace {

// 去掉空白后的文字（比较「字还在不在」时用：换行会吃掉行末的空格，
// 那不是丢字）。
QString normalizedText(const QString& text) {
    QString result;
    for (const QChar ch : text) {
        if (!ch.isSpace()) result.append(ch);
    }
    return result;
}

// 标准 14 号字体表示不了的字符（非 Latin-1）。
bool hasNonLatin1(const QString& text) {
    for (const QChar ch : text) {
        if (ch.unicode() > 0xFF) return true;
    }
    return false;
}

// 标准 14 号字体表示不了的字符写成 '?' 后的样子（原文本来就是这么写的
// 时候，用它来核对）。
QString latin1Placeholder(const QString& text) {
    QString result;
    result.reserve(text.size());
    for (const QChar ch : text) {
        result.append(ch.unicode() > 0xFF ? QLatin1Char('?') : ch);
    }
    return result;
}

// 一行的行距（点）。口径与 pdf::TextReflow 一致。
double lineStepFor(const TextStyle& style) {
    QFont font(style.family);
    font.setPointSizeF(qMax(1.0, style.fontSize));
    const QFontMetricsF metrics(font);
    return qMax(metrics.height(), style.fontSize * style.lineHeight);
}

}  // namespace

TranslationWriteResult translateDocument(const QByteArray& pdfBytes, TranslationProvider* provider,
                                         const TranslationOptions& options) {
    TranslationWriteResult result;
    if (pdfBytes.isEmpty()) {
        result.report.error = i18n::tr("aipdf::pdf::TranslationWriter", "输入的 PDF 是空的");
        return result;
    }
    if (provider == nullptr) {
        result.report.error =
            i18n::tr("aipdf::pdf::TranslationWriter", "没有翻译 Provider，这一篇没法翻译");
        return result;
    }
    result.report.providerId = provider->id();
    if (!provider->isConfigured()) {
        result.report.error = provider->notConfiguredReason();
        return result;
    }
    if (provider->isDemo()) {
        result.report.error = i18n::tr(
            "aipdf::pdf::TranslationWriter",
            "翻译 Provider 是演示模式，演示出来的示例不能写回文档");
        return result;
    }

    QString error;
    QVector<ContentTextBlock> blocks;
    if (!readContentTextBlocks(pdfBytes, &blocks, &error)) {
        result.report.error = error;
        return result;
    }
    const QVector<TextParagraph> paragraphs = groupBlocksIntoParagraphs(pdfBytes, blocks, &error);
    const QVector<QRectF> pageBoxes = readPageMediaBoxes(pdfBytes, &error);
    result.report.pageCount = pageBoxes.size();

    QVector<BlockRewrite> rewrites;
    for (int paragraphIndex = 0; paragraphIndex < paragraphs.size(); ++paragraphIndex) {
        const TextParagraph& paragraph = paragraphs.at(paragraphIndex);
        if (paragraph.text.trimmed().isEmpty()) continue;
        if (!options.pageIndexes.isEmpty() && !options.pageIndexes.contains(paragraph.pageIndex)) {
            continue;
        }
        TranslationUnit unit;
        unit.pageIndex = paragraph.pageIndex;
        unit.blockId = paragraph.blockIds.value(0, -1);
        unit.sourceText = paragraph.text;
        result.report.unitCount += 1;
        result.report.sourceCharCount += paragraph.text.size();

        // ---- 调用 Provider ----
        QString translation;
        QString providerError;
        if (!provider->translate(paragraph.text, options.targetLanguage, &translation,
                                 &providerError)) {
            unit.reason = providerError.isEmpty()
                              ? i18n::tr("aipdf::pdf::TranslationWriter", "翻译 Provider 调用失败")
                              : providerError;
            result.report.failedCount += 1;
            result.report.units.append(unit);
            continue;
        }
        const QString trimmed = translation.trimmed();
        if (trimmed.isEmpty()) {
            unit.reason =
                i18n::tr("aipdf::pdf::TranslationWriter", "翻译 Provider 返回的是空内容，没有译文");
            result.report.failedCount += 1;
            result.report.units.append(unit);
            continue;
        }
        if (trimmed == paragraph.text.trimmed()) {
            unit.reason = i18n::tr(
                "aipdf::pdf::TranslationWriter",
                "翻译 Provider 返回的内容与原文一样，不像是译文，这一段保留原文");
            result.report.failedCount += 1;
            result.report.units.append(unit);
            continue;
        }
        if (hasNonLatin1(translation)) {
            unit.reason = i18n::tr(
                "aipdf::pdf::TranslationWriter",
                "译文里有当前字体表示不了的字符（要写中文译文得先嵌入中文字体），这一段保留原文");
            result.report.failedCount += 1;
            result.report.units.append(unit);
            continue;
        }
        unit.translatedText = translation;
        result.report.translatedCount += 1;
        result.report.translatedCharCount += translation.size();

        // ---- 排版：译文按原来的栏宽重新换行 ----
        TextStyle measureStyle;
        measureStyle.family = paragraph.fontFamily;
        measureStyle.fontSize = paragraph.style.fontSize > 0.0 ? paragraph.style.fontSize : 12.0;
        measureStyle.lineHeight = 1.2;
        const double width = qMax(20.0, paragraph.bounds.width());
        const ReflowResult laid =
            TextReflow::layoutParagraph(translation, QRectF(0.0, 0.0, width, 1.0e6), measureStyle);
        if (laid.lines.isEmpty() || laid.overflow) {
            unit.reason = i18n::tr("aipdf::pdf::TranslationWriter",
                                   "译文排不出来，这一段保留原文");
            result.report.failedCount += 1;
            result.report.units.append(unit);
            continue;
        }

        // ---- 放不放得下 ----
        // 既不能超出页面，也不能压到下一段：译文变长时往下长，
        // 长到下一段头上就是把两段压在一起了。
        const double lineStep = lineStepFor(measureStyle);
        QFont font(measureStyle.family);
        font.setPointSizeF(qMax(1.0, measureStyle.fontSize));
        const QFontMetricsF metrics(font);
        const double lowest =
            paragraph.firstBaseline - (laid.lines.size() - 1) * lineStep - metrics.descent();
        const double bottomLimit = paragraphBottomLimit(paragraphs, paragraphIndex, pageBoxes);
        if (lowest < bottomLimit) {
            if (options.keepOriginalOnOverflow) {
                unit.reason = i18n::tr("aipdf::pdf::TranslationWriter",
                                       "译文比原文长，原来的位置放不下（会压到下一段或超出页面），"
                                       "这一段保留原文");
                result.report.failedCount += 1;
                result.report.units.append(unit);
                continue;
            }
            result.report.notes.append(
                i18n::tr("aipdf::pdf::TranslationWriter", "第 %1 页上有一段译文超出了原来的位置")
                    .arg(paragraph.pageIndex + 1));
        }

        BlockRewrite rewrite;
        rewrite.pageIndex = paragraph.pageIndex;
        rewrite.blockIds = paragraph.blockIds;
        rewrite.firstBaseline = paragraph.firstBaseline;
        rewrite.firstX = paragraph.firstX;
        rewrite.lineStep = lineStep;
        rewrite.leftAligned = true;
        for (const ReflowLine& line : laid.lines) {
            TextPiece piece;
            piece.text = line.text;
            piece.style = paragraph.style;
            rewrite.lines.append(QVector<TextPiece>{piece});
        }
        rewrites.append(rewrite);
        unit.written = true;
        result.report.units.append(unit);
    }

    if (rewrites.isEmpty()) {
        // 逐条原因已经写在 units 里了，总结句再带上第一条：
        // 只说「没有译文可写回」的话，调用方还得回头翻报告才知道为什么。
        QString reason;
        for (const TranslationUnit& unit : result.report.units) {
            if (!unit.reason.isEmpty()) {
                reason = unit.reason;
                break;
            }
        }
        result.report.error =
            i18n::tr("aipdf::pdf::TranslationWriter", "没有一段译文可以写回，文档保持原样");
        if (!reason.isEmpty()) {
            result.report.error += QStringLiteral("（") + reason + QStringLiteral("）");
        }
        return result;
    }

    const QByteArray written = applyBlockRewrites(pdfBytes, rewrites, &error);
    if (written.isEmpty()) {
        result.report.error = error;
        return result;
    }

    // ---- 门禁：用独立解析器（PDFium）重新打开、重新抽文字 ----
    int pageCount = 0;
    const QStringList pageTexts = extractPageTexts(written, &pageCount, &error);
    if (!error.isEmpty()) {
        result.report.error = i18n::tr("aipdf::pdf::TranslationWriter", "写回后打不开：%1").arg(error);
        return result;
    }
    if (pageCount != result.report.pageCount) {
        result.report.error = i18n::tr("aipdf::pdf::TranslationWriter", "写回后页数变了（原来 %1 页，现在 %2 页）")
                                   .arg(result.report.pageCount)
                                   .arg(pageCount);
        return result;
    }
    for (const TranslationUnit& unit : result.report.units) {
        const QString pageText = normalizedText(pageTexts.value(unit.pageIndex));
        if (unit.written) {
            if (!pageText.contains(normalizedText(unit.translatedText))) {
                result.report.error = i18n::tr("aipdf::pdf::TranslationWriter",
                                               "第 %1 页上第 %2 段译文没有落到 PDF 里，已放弃写回")
                                           .arg(unit.pageIndex + 1)
                                           .arg(unit.blockId + 1);
                return result;
            }
            continue;
        }
        const QString expected = normalizedText(unit.sourceText);
        const QString degraded = normalizedText(latin1Placeholder(unit.sourceText));
        if (!pageText.contains(expected) && !pageText.contains(degraded)) {
            result.report.error = i18n::tr("aipdf::pdf::TranslationWriter",
                                           "第 %1 页上第 %2 段的原文不见了，已放弃写回")
                                       .arg(unit.pageIndex + 1)
                                       .arg(unit.blockId + 1);
            return result;
        }
    }
    result.report.writtenCount = rewrites.size();
    result.bytes = written;
    return result;
}

}  // namespace aipdf::pdf
