#include "pdf/PreflightChecker.h"

#include "pdf/ContentStream.h"

#include <QHash>
#include <QSet>
#include <QStringList>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include <algorithm>
#include <cmath>
#include <exception>
#include <map>
#include <set>
#include <vector>
#endif

namespace aipdf::pdf {

namespace {

// 印刷常用的图片分辨率下限（dpi）。
//
// 为什么是 150：低于这个值，网点/线条在纸上的细节会糊掉，
// 印厂一般也会按这个线退单。
constexpr double kMinImageDpi = 150.0;

// 遍历对象图时的深度上限：坏文件里可能有非常深的嵌套。
//
// 这个上限现在只管「覆盖范围」，不再承担防爆栈的职责 ——
// 遍历已经改成显式栈（堆上的），深度多少都不会爆栈。
// 1024 层远超过任何正常文档的嵌套深度，正常文件不会撞到它。
constexpr int kMaxObjectDepth = 1024;

// 扫描内容流时 Form XObject 的嵌套深度上限。
constexpr int kMaxFormDepth = 4;

// 外部引用最多列几条结论：一份文档里可能有几百个外链，全列出来没人看得完。
constexpr int kMaxExternalFindings = 20;

// 小数位：页面尺寸、dpi 这类数值保留两位，够定位问题，又不会被浮点噪声刷屏。
QString numberText(double value) {
    return QString::number(value, 'f', 2);
}

}  // namespace

QString PreflightReport::summary() const {
    int infoCount = 0;
    for (const PreflightFinding& finding : findings) {
        if (finding.severity == PreflightSeverity::Info) ++infoCount;
    }
    return QStringLiteral("印前检查完成：共 %1 条结论（错误 %2 条、警告 %3 条、提示 %4 条）")
        .arg(findings.size())
        .arg(errorCount)
        .arg(warningCount)
        .arg(infoCount);
}

#ifdef AIPDF_WITH_QPDF

namespace {

// 把 /BaseFont 里的子集前缀（"ABCDEF+"）去掉。
//
// 为什么：子集前缀只是文件里对字体的编号，真正说明「这是哪个字体」的是后半段。
QString baseFontName(const QPDFObjectHandle& font) {
    QPDFObjectHandle base = font.getKey("/BaseFont");
    if (!base.isName()) return QStringLiteral("（未命名字体）");
    QString name = QString::fromUtf8(base.getName().c_str());
    if (name.size() > 7 && name.at(6) == QLatin1Char('+')) {
        bool subsetTag = true;
        for (int i = 0; i < 6; ++i) {
            const QChar ch = name.at(i);
            if (!(ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) &&
                !(ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))) {
                subsetTag = false;
                break;
            }
        }
        if (subsetTag) name = name.mid(7);
    }
    return name;
}

// --------------------------------------------------------------- 矩阵 ----

// PDF 的二维仿射矩阵 [a b c d e f]。
//
// 与 TagTreeWriter 用同一套约定：行向量式乘法，multiply(left, right)
// 的意思是「先作用 right，再作用 left」。
struct Matrix {
    double a = 1.0;
    double b = 0.0;
    double c = 0.0;
    double d = 1.0;
    double e = 0.0;
    double f = 0.0;
};

Matrix multiply(const Matrix& left, const Matrix& right) {
    Matrix result;
    result.a = left.a * right.a + left.b * right.c;
    result.b = left.a * right.b + left.b * right.d;
    result.c = left.c * right.a + left.d * right.c;
    result.d = left.c * right.b + left.d * right.d;
    result.e = left.e * right.a + left.f * right.c + right.e;
    result.f = left.e * right.b + left.f * right.d + right.f;
    return result;
}

// 一个图像被画到页面上的位置（用当时的 CTM 表达）。
struct ImagePlacement {
    QString name;       // XObject 的名字（资源字典里的键）
    double scaleX = 1.0;  // 单位正方形在 x 方向被放大的倍数（= 显示宽度，单位：点）
    double scaleY = 1.0;
};

// 把内容流里的 token 压成「有效 token」的下标表：空白与注释不算操作数。
QVector<int> significantIndexes(const QVector<ContentToken>& tokens) {
    QVector<int> indexes;
    indexes.reserve(tokens.size());
    for (int i = 0; i < tokens.size(); ++i) {
        const ContentTokenType type = tokens.at(i).type;
        if (type == ContentTokenType::Whitespace || type == ContentTokenType::Comment) continue;
        indexes.append(i);
    }
    return indexes;
}

// 取一个对象的字典部分。
//
// 为什么需要这一步：qpdf 里「流对象」（图像、Form XObject、内容流）
// 与「字典对象」是两种类型——isDictionary() 对流返回 false，
// 而且在流对象上直接 getKey("/Subtype") 拿不到东西（要通过 getDict() 进字典）。
QPDFObjectHandle dictOf(const QPDFObjectHandle& object) {
    return object.isStream() ? object.getDict() : object;
}

// 资源字典里的键（qpdf 给的是带斜杠的 "/Im1"），去掉斜杠后当名字用。
//
// 为什么要去斜杠：内容流里的 Do 操作数写的是 "/Im1"（带斜杠），
// 而 qpdf 的 getKeys() 也带斜杠——两边不一致就会查不到。
QString resourceName(const std::string& key) {
    QString name = QString::fromUtf8(key.c_str());
    if (name.startsWith(QLatin1Char('/'))) name = name.mid(1);
    return name;
}

// 内容流里的资源字典：名字 → XObject 对象。
QHash<QString, QPDFObjectHandle> xObjectsOf(const QPDFObjectHandle& resources) {
    QHash<QString, QPDFObjectHandle> result;
    if (!resources.isDictionary()) return result;
    const QPDFObjectHandle xobjects = resources.getKey("/XObject");
    if (!xobjects.isDictionary()) return result;
    for (const std::string& name : xobjects.getKeys()) {
        result.insert(resourceName(name), xobjects.getKey(name));
    }
    return result;
}

// 扫一条内容流，记录所有图像的绘制位置。
//
// 为什么自己扫内容流：图片的「显示尺寸」只存在于内容流里（/Width 是像素数，
// 显示多大由画它的那条 cm 矩阵决定）。不扫内容流就只能拿页面尺寸去猜，
// 猜出来的 dpi 是假的——印前检查最怕给出看着精确、其实没依据的数字。
void scanContentForImages(const QByteArray& content,
                          const QHash<QString, QPDFObjectHandle>& xobjects,
                          const Matrix& baseCtm,
                          int depth,
                          QSet<int>* visitedForms,
                          QVector<ImagePlacement>* placements) {
    if (depth > kMaxFormDepth) return;
    const ContentParseResult parsed = ContentStream::tokenize(content);
    const QVector<ContentToken>& tokens = parsed.tokens;
    const QVector<int> significant = significantIndexes(tokens);

    Matrix ctm = baseCtm;
    QVector<Matrix> stack;

    auto numberBefore = [&tokens, &significant](int position, int count,
                                                QVector<double>* values) -> bool {
        values->clear();
        if (position < count) return false;
        for (int n = 0; n < count; ++n) {
            const ContentToken& token = tokens.at(significant.at(position - 1 - n));
            if (token.type != ContentTokenType::Number) return false;
            bool ok = false;
            const double value = token.raw.toDouble(&ok);
            if (!ok) return false;
            values->prepend(value);
        }
        return true;
    };

    for (int p = 0; p < static_cast<int>(significant.size()); ++p) {
        const int tokenIndex = significant.at(p);
        const ContentToken& token = tokens.at(tokenIndex);
        if (token.type != ContentTokenType::Operator) continue;
        const QByteArray& op = token.raw;

        if (op == "q") {
            stack.append(ctm);
        } else if (op == "Q") {
            if (!stack.isEmpty()) ctm = stack.takeLast();
        } else if (op == "cm") {
            QVector<double> values;
            if (!numberBefore(p, 6, &values)) continue;
            Matrix matrix;
            matrix.a = values.at(0);
            matrix.b = values.at(1);
            matrix.c = values.at(2);
            matrix.d = values.at(3);
            matrix.e = values.at(4);
            matrix.f = values.at(5);
            // PDF 的约定：后写的 cm 先作用到用户空间上，
            // 所以新矩阵放在左边（与 TagTreeWriter 的乘法约定一致）。
            ctm = multiply(matrix, ctm);
        } else if (op == "Do") {
            if (p == 0) continue;
            const ContentToken& nameToken = tokens.at(significant.at(p - 1));
            if (nameToken.type != ContentTokenType::Name) continue;
            const QString name = QString::fromUtf8(nameToken.raw.mid(1));
            const auto found = xobjects.constFind(name);
            if (found == xobjects.constEnd()) continue;
            const QPDFObjectHandle object = found.value();
            const QPDFObjectHandle dictionary = dictOf(object);
            if (!dictionary.isDictionary()) continue;

            const QPDFObjectHandle subtype = dictionary.getKey("/Subtype");
            if (!subtype.isName()) continue;
            const std::string subtypeName = subtype.getName();

            if (subtypeName == "/Image") {
                ImagePlacement placement;
                placement.name = name;
                placement.scaleX = std::hypot(ctm.a, ctm.b);
                placement.scaleY = std::hypot(ctm.c, ctm.d);
                placements->append(placement);
            } else if (subtypeName == "/Form") {
                // 表单 XObject 里的图像也算：它自己的内容流用的是它自己的资源。
                const int objectId = object.isIndirect() ? object.getObjectID() : -1;
                if (objectId >= 0) {
                    if (visitedForms->contains(objectId)) continue;
                    visitedForms->insert(objectId);
                }
                Matrix inner = ctm;
                const QPDFObjectHandle formMatrix = dictionary.getKey("/Matrix");
                if (formMatrix.isArray() && formMatrix.getArrayAsVector().size() == 6) {
                    const std::vector<QPDFObjectHandle> items = formMatrix.getArrayAsVector();
                    Matrix matrix;
                    matrix.a = items.at(0).getNumericValue();
                    matrix.b = items.at(1).getNumericValue();
                    matrix.c = items.at(2).getNumericValue();
                    matrix.d = items.at(3).getNumericValue();
                    matrix.e = items.at(4).getNumericValue();
                    matrix.f = items.at(5).getNumericValue();
                    inner = multiply(matrix, ctm);
                }
                QPDFObjectHandle formContent = dictionary.getKey("/Contents");
                if (!formContent.isStream()) continue;
                std::shared_ptr<Buffer> data = formContent.getStreamData(qpdf_dl_generalized);
                if (!data) continue;
                const QByteArray bytes(reinterpret_cast<const char*>(data->getBuffer()),
                                      static_cast<int>(data->getSize()));
                const QHash<QString, QPDFObjectHandle> innerXObjects =
                    xObjectsOf(dictionary.getKey("/Resources"));
                scanContentForImages(bytes, innerXObjects, inner, depth + 1, visitedForms,
                                     placements);
            }
        }
    }
}

// 一个字体是否内嵌了字体文件。
//
// 为什么三个键都查：/FontFile 是 Type1，/FontFile2 是 TrueType，
// /FontFile3 是 CFF/OpenType——内嵌在哪个键里取决于字体类型。
bool fontIsEmbedded(const QPDFObjectHandle& font, QString* embeddedAs) {
    QVector<QPDFObjectHandle> descriptors;
    const QPDFObjectHandle subtype = font.getKey("/Subtype");
    if (subtype.isName() && subtype.getName() == "/Type0") {
        // 复合字体的字体文件挂在 /DescendantFonts 里的每一个子字体上。
        const QPDFObjectHandle descendants = font.getKey("/DescendantFonts");
        if (descendants.isArray()) {
            for (const QPDFObjectHandle& item : descendants.getArrayAsVector()) {
                descriptors.append(item.getKey("/FontDescriptor"));
            }
        }
    } else {
        descriptors.append(font.getKey("/FontDescriptor"));
    }
    for (const QPDFObjectHandle& descriptor : descriptors) {
        if (!descriptor.isDictionary()) continue;
        const QPDFObjectHandle file = descriptor.getKey("/FontFile");
        if (file.isStream()) {
            if (embeddedAs != nullptr) *embeddedAs = QStringLiteral("/FontFile");
            return true;
        }
        const QPDFObjectHandle file2 = descriptor.getKey("/FontFile2");
        if (file2.isStream()) {
            if (embeddedAs != nullptr) *embeddedAs = QStringLiteral("/FontFile2");
            return true;
        }
        const QPDFObjectHandle file3 = descriptor.getKey("/FontFile3");
        if (file3.isStream()) {
            if (embeddedAs != nullptr) *embeddedAs = QStringLiteral("/FontFile3");
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------- 动作（JS/外链）----

// 一个动作命中。
struct ActionHit {
    QString code;    // "javascript" 或 "external-reference"
    QString title;
    QString detail;
};

// 扫整棵对象树，找 JavaScript 动作与外部引用动作。
//
// 为什么扫整棵树而不只看 catalog：JavaScript 可以挂在 /OpenAction、
// /AA（附加动作）、批注的 /A、名字树里……只看 catalog 会漏掉大半。
// 直接对象（不是间接对象）也要跟着递归进去，否则挂在数组里的动作会漏掉。
class ActionScanner {
public:
    // 显式栈遍历，不用递归。
    //
    // 为什么不用递归：坏文件里一条几千层的间接对象链会把递归栈爆掉
    // （实测 chain=5000 时进程退出码 0xC00000FD，直接崩，印前检查已经接进界面）。
    // 递归版无论怎么传深度都救不了 —— 爆栈的根因是「递归深度 = 对象链长度」，
    // 而对象链长度由文件说了算。显式栈放在堆上就没有这个上限，
    // 防环靠 visited_（按对象号去重）。
    //
    // 子节点倒着进栈、出栈时正好是原顺序，所以遍历次序与递归版完全一致。
    void scan(const QPDFObjectHandle& root) {
        QVector<QPair<QPDFObjectHandle, int>> pending;
        pending.append(qMakePair(root, 0));
        while (!pending.isEmpty()) {
            const QPair<QPDFObjectHandle, int> current = pending.takeLast();
            const QPDFObjectHandle& handle = current.first;
            const int depth = current.second;
            if (depth > kMaxObjectDepth) continue;
            if (handle.isIndirect()) {
                const QPDFObjGen objGen = handle.getObjGen();
                if (visited_.count(objGen) > 0) continue;
                visited_.insert(objGen);
            }
            if (handle.isDictionary()) {
                inspectDictionary(handle, depth, &pending);
            } else if (handle.isArray()) {
                const std::vector<QPDFObjectHandle> items = handle.getArrayAsVector();
                for (int i = static_cast<int>(items.size()) - 1; i >= 0; --i) {
                    pending.append(qMakePair(items.at(static_cast<size_t>(i)), depth + 1));
                }
            } else if (handle.isStream()) {
                // 流对象的字典也要看：/S /JavaScript 这类动作有可能挂在流字典上。
                pending.append(qMakePair(handle.getDict(), depth + 1));
            }
        }
    }

    const QVector<ActionHit>& hits() const { return hits_; }

private:
    void inspectDictionary(const QPDFObjectHandle& dictionary, int depth,
                           QVector<QPair<QPDFObjectHandle, int>>* pending) {
        const QPDFObjectHandle kind = dictionary.getKey("/S");
        const bool hasKind = kind.isName();
        const std::string kindName = hasKind ? kind.getName() : std::string();

        if (hasKind && kindName == "/JavaScript") {
            addHit(QStringLiteral("javascript"), QStringLiteral("含 JavaScript 动作"),
                   QStringLiteral("文档里有 /JavaScript 动作：%1")
                       .arg(describeScript(dictionary)));
        } else if (dictionary.hasKey("/JS")) {
            // 有些文件不写 /S，直接给 /JS——同样按 JavaScript 处理。
            addHit(QStringLiteral("javascript"), QStringLiteral("含 JavaScript 动作"),
                   QStringLiteral("文档里有 /JS 动作：%1").arg(describeScript(dictionary)));
        } else if (hasKind && kindName == "/URI") {
            addHit(QStringLiteral("external-reference"), QStringLiteral("含外部引用"),
                   QStringLiteral("文档里有外部链接动作（/URI）：%1")
                       .arg(describeUri(dictionary)));
        } else if (hasKind && kindName == "/Launch") {
            addHit(QStringLiteral("external-reference"), QStringLiteral("含外部引用"),
                   QStringLiteral("文档里有启动外部程序的动作（/Launch）：%1")
                       .arg(describeUri(dictionary)));
        } else if (hasKind && (kindName == "/GoToR" || kindName == "/SubmitForm" ||
                               kindName == "/ImportData")) {
            addHit(QStringLiteral("external-reference"), QStringLiteral("含外部引用"),
                   QStringLiteral("文档里有指向外部文件的动作（%1）：%2")
                       .arg(QString::fromUtf8(kindName.c_str()))
                       .arg(describeUri(dictionary)));
        }

        // 深度要接着往下传，不能写死 1：
        // 写死 1 等于把深度上限的守卫废掉，坏文件里一条很长的间接对象链
        // 会被当成一条浅链来扫。
        // 倒着压栈，出栈时才是原来的次序。
        const std::set<std::string> keys = dictionary.getKeys();
        for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
            pending->append(qMakePair(dictionary.getKey(*it), depth + 1));
        }
    }

    // 动作里的目标描述（/F 文件名或 /URI 地址）。
    static QString describeUri(const QPDFObjectHandle& dictionary) {
        const QPDFObjectHandle uri = dictionary.getKey("/URI");
        if (uri.isString()) {
            return QString::fromUtf8(uri.getUTF8Value().c_str());
        }
        const QPDFObjectHandle file = dictionary.getKey("/F");
        if (file.isString()) {
            return QString::fromUtf8(file.getUTF8Value().c_str());
        }
        return QStringLiteral("（未写明目标）");
    }

    static QString describeScript(const QPDFObjectHandle& dictionary) {
        const QPDFObjectHandle script = dictionary.getKey("/JS");
        if (!script.isString()) return QStringLiteral("（脚本内容未写明）");
        QString text = QString::fromUtf8(script.getUTF8Value().c_str());
        text.replace(QLatin1Char('\n'), QLatin1Char(' '));
        text.replace(QLatin1Char('\r'), QLatin1Char(' '));
        if (text.size() > 60) text = text.left(60) + QStringLiteral("…");
        return text;
    }

    void addHit(const QString& code, const QString& title, const QString& detail) {
        const QString key = code + QLatin1Char('|') + detail;
        if (seen_.contains(key)) return;
        if (code == QLatin1String("external-reference") &&
            externalCount_ >= kMaxExternalFindings) {
            return;
        }
        if (code == QLatin1String("external-reference")) ++externalCount_;
        seen_.insert(key);
        ActionHit hit;
        hit.code = code;
        hit.title = title;
        hit.detail = detail;
        hits_.append(hit);
    }

    std::set<QPDFObjGen> visited_;
    QSet<QString> seen_;
    QVector<ActionHit> hits_;
    int externalCount_ = 0;
};

// --------------------------------------------------------------- 页面 ----

// 一页的检查上下文。
struct PageContext {
    int index = 0;
    QPDFObjectHandle page;
    QPDFObjectHandle resources;
};

// 取页面的 /Resources（带继承：页面上没有就往 /Pages 上找）。
QPDFObjectHandle resourcesOf(const QPDFObjectHandle& page) {
    return QPDFPageObjectHelper(page).getAttribute("/Resources", false);
}

// 收集文档里用到的字体（按对象去重，保留第一次出现的页号）。
void collectFonts(const QPDFObjectHandle& resources,
                  QVector<QPair<QPDFObjectHandle, QString>>* fonts) {
    if (!resources.isDictionary()) return;
    const QPDFObjectHandle fontDict = resources.getKey("/Font");
    if (!fontDict.isDictionary()) return;
    for (const std::string& key : fontDict.getKeys()) {
        fonts->append(qMakePair(fontDict.getKey(key), resourceName(key)));
    }
}

// 收集文档里用到的图像 XObject。
void collectImages(const QPDFObjectHandle& resources,
                   QVector<QPair<QPDFObjectHandle, QString>>* images) {
    if (!resources.isDictionary()) return;
    const QPDFObjectHandle xobjects = resources.getKey("/XObject");
    if (!xobjects.isDictionary()) return;
    for (const std::string& key : xobjects.getKeys()) {
        const QPDFObjectHandle object = xobjects.getKey(key);
        const QPDFObjectHandle dictionary = dictOf(object);
        if (!dictionary.isDictionary()) continue;
        const QPDFObjectHandle subtype = dictionary.getKey("/Subtype");
        if (!subtype.isName() || subtype.getName() != "/Image") continue;
        images->append(qMakePair(object, resourceName(key)));
    }
}

// 一页的内容流拼起来（多段内容流按顺序接）。
QByteArray pageContent(QPDFObjectHandle page) {
    QByteArray content;
    for (QPDFObjectHandle& stream : page.getPageContents()) {
        if (!stream.isStream()) continue;
        std::shared_ptr<Buffer> data = stream.getStreamData(qpdf_dl_generalized);
        if (!data) continue;
        content.append(reinterpret_cast<const char*>(data->getBuffer()),
                       static_cast<int>(data->getSize()));
        content.append('\n');
    }
    return content;
}

// 图像的有效分辨率（dpi）。显示尺寸推不出来时返回 0（表示「判不了」）。
double effectiveDpi(const QPDFObjectHandle& image, double scaleX, double scaleY) {
    const QPDFObjectHandle dictionary = dictOf(image);
    const QPDFObjectHandle width = dictionary.getKey("/Width");
    const QPDFObjectHandle height = dictionary.getKey("/Height");
    if (!width.isInteger() || !height.isInteger()) return 0.0;
    const double pixelWidth = static_cast<double>(width.getIntValue());
    const double pixelHeight = static_cast<double>(height.getIntValue());
    if (pixelWidth <= 0.0 || pixelHeight <= 0.0) return 0.0;
    if (scaleX <= 0.0 || scaleY <= 0.0) return 0.0;
    const double dpiX = pixelWidth * 72.0 / scaleX;
    const double dpiY = pixelHeight * 72.0 / scaleY;
    return std::min(dpiX, dpiY);
}

}  // namespace

#endif  // AIPDF_WITH_QPDF

PreflightReport runPreflightCheck(const QByteArray& pdfBytes, QString* error) {
    PreflightReport report;

    // 失败也要给出一份「能看懂的报告」：调用方只看 ok 与 error 就够，
    // 但报告里保留一条 Error 结论，界面上不用另外拼文案。
    const auto fail = [&report, error](const QString& reason) {
        report = PreflightReport();
        PreflightFinding finding;
        finding.code = QStringLiteral("document-unreadable");
        finding.title = QStringLiteral("无法解析这份 PDF");
        finding.detail = reason;
        finding.severity = PreflightSeverity::Error;
        finding.pageIndex = -1;
        report.findings.append(finding);
        report.errorCount = 1;
        report.warningCount = 0;
        report.ok = false;
        if (error != nullptr) *error = reason;
        return report;
    };

    if (pdfBytes.isEmpty()) {
        return fail(QStringLiteral("内容是空的，没有可检查的 PDF 字节"));
    }
    // 先看文件头：不是 PDF 就别让解析器去猜了（qpdf 对带前导垃圾的 PDF 很宽容，
    // 会把它「修好」再返回，那不是我们要的检查结果）。
    const int headerLimit = qMin(pdfBytes.size(), 1024);
    if (!pdfBytes.left(headerLimit).contains("%PDF-")) {
        return fail(QStringLiteral("这段内容不是 PDF（文件头里没有 %PDF-）"));
    }

#ifndef AIPDF_WITH_QPDF
    return fail(QStringLiteral("本次构建没有启用 qpdf，做不了印前检查"));
#else
    try {
        QPDF pdf;
        // qpdf 的警告不往 stderr 上倒：这里要的是自己的结论，不是解析器的抱怨。
        pdf.setSuppressWarnings(true);
        pdf.processMemoryFile("preflight.pdf", pdfBytes.constData(),
                              static_cast<size_t>(pdfBytes.size()));

        const std::vector<QPDFObjectHandle> pages = pdf.getAllPages();

        // 1. 加密。
        if (pdf.isEncrypted()) {
            PreflightFinding finding;
            finding.code = QStringLiteral("encrypted");
            finding.title = QStringLiteral("文档带加密");
            finding.detail = QStringLiteral(
                "这份 PDF 使用标准安全处理器加密；印前流程通常需要输出解密版本，"
                "否则 RIP 拿不到页面内容。");
            finding.severity = PreflightSeverity::Warning;
            finding.pageIndex = -1;
            report.findings.append(finding);
        }

        // 2. JavaScript 与外部引用（扫整棵对象树）。
        ActionScanner scanner;
        scanner.scan(pdf.getRoot());
        for (const ActionHit& hit : scanner.hits()) {
            PreflightFinding finding;
            finding.code = hit.code;
            finding.title = hit.title;
            finding.detail = hit.detail;
            finding.severity = hit.code == QLatin1String("javascript")
                                   ? PreflightSeverity::Error
                                   : PreflightSeverity::Warning;
            finding.pageIndex = -1;
            report.findings.append(finding);
        }

        // 3. 页面尺寸是否一致。
        QString firstSize;
        QString secondSize;
        int firstSizePage = -1;
        int secondSizePage = -1;
        for (size_t i = 0; i < pages.size(); ++i) {
            QPDFPageObjectHelper helper(pages[i]);
            const QPDFObjectHandle mediaBox = helper.getMediaBox();
            if (!mediaBox.isArray()) continue;
            const std::vector<QPDFObjectHandle> items = mediaBox.getArrayAsVector();
            if (items.size() != 4) continue;
            const double width = items.at(2).getNumericValue() - items.at(0).getNumericValue();
            const double height = items.at(3).getNumericValue() - items.at(1).getNumericValue();
            const QString size = numberText(width) + QStringLiteral(" x ") + numberText(height);
            if (firstSize.isEmpty()) {
                firstSize = size;
                firstSizePage = static_cast<int>(i);
            } else if (size != firstSize && secondSize.isEmpty()) {
                secondSize = size;
                secondSizePage = static_cast<int>(i);
            }
        }
        if (!secondSize.isEmpty()) {
            PreflightFinding finding;
            finding.code = QStringLiteral("page-size-mismatch");
            finding.title = QStringLiteral("页面尺寸不一致");
            finding.detail = QStringLiteral("第 %1 页是 %2，第 %3 页是 %4；"
                                           "拼版前需要统一页面尺寸。")
                                .arg(firstSizePage + 1)
                                .arg(firstSize)
                                .arg(secondSizePage + 1)
                                .arg(secondSize);
            finding.severity = PreflightSeverity::Info;
            finding.pageIndex = -1;
            report.findings.append(finding);
        }

        // 4. 逐页检查：字体内嵌、图片分辨率、透明度组。
        QSet<int> reportedFonts;
        QSet<int> reportedImages;
        int transparencyPages = 0;
        int firstTransparencyPage = -1;

        for (size_t i = 0; i < pages.size(); ++i) {
            const int pageIndex = static_cast<int>(i);
            const QPDFObjectHandle page = pages[i];
            const QPDFObjectHandle resources = resourcesOf(page);

            // 4.1 字体是否内嵌。
            QVector<QPair<QPDFObjectHandle, QString>> fonts;
            collectFonts(resources, &fonts);
            for (const auto& font : fonts) {
                const QPDFObjectHandle& object = font.first;
                const int objectId = object.isIndirect() ? object.getObjectID() : -1;
                if (objectId >= 0 && reportedFonts.contains(objectId)) continue;
                if (objectId >= 0) reportedFonts.insert(objectId);
                QString embeddedAs;
                if (fontIsEmbedded(object, &embeddedAs)) continue;
                PreflightFinding finding;
                finding.code = QStringLiteral("font-not-embedded");
                finding.title = QStringLiteral("字体没有内嵌");
                finding.detail = QStringLiteral(
                                    "第 %1 页用到的字体 %2（资源名 /%3）没有内嵌字体文件"
                                    "（/FontFile、/FontFile2、/FontFile3 都没有）；"
                                    "换一台没装同样字体的机器会变成替代字体，版面可能走样。")
                                    .arg(pageIndex + 1)
                                    .arg(baseFontName(object))
                                    .arg(font.second);
                finding.severity = PreflightSeverity::Warning;
                finding.pageIndex = pageIndex;
                report.findings.append(finding);
            }

            // 4.2 图片分辨率。
            QVector<QPair<QPDFObjectHandle, QString>> images;
            collectImages(resources, &images);
            if (!images.isEmpty()) {
                const QByteArray content = pageContent(page);
                QVector<ImagePlacement> placements;
                QSet<int> visitedForms;
                scanContentForImages(content, xObjectsOf(resources), Matrix(), 0, &visitedForms,
                                     &placements);
                for (const auto& image : images) {
                    const QPDFObjectHandle& object = image.first;
                    const int objectId = object.isIndirect() ? object.getObjectID() : -1;
                    if (objectId >= 0 && reportedImages.contains(objectId)) continue;
                    if (objectId >= 0) reportedImages.insert(objectId);
                    // 找出这张图被画在哪：画了几次、每次的显示尺寸可能不同。
                    double bestDpi = 0.0;
                    bool judged = false;
                    for (const ImagePlacement& placement : placements) {
                        if (placement.name != image.second) continue;
                        const double dpi = effectiveDpi(object, placement.scaleX, placement.scaleY);
                        if (dpi <= 0.0) continue;
                        judged = true;
                        if (bestDpi == 0.0 || dpi < bestDpi) bestDpi = dpi;
                    }
                    if (!judged) continue;
                    if (bestDpi >= kMinImageDpi) continue;
                    const QPDFObjectHandle imageDict = dictOf(object);
                    const QPDFObjectHandle width = imageDict.getKey("/Width");
                    const QPDFObjectHandle height = imageDict.getKey("/Height");
                    PreflightFinding finding;
                    finding.code = QStringLiteral("image-resolution-low");
                    finding.title = QStringLiteral("图片分辨率过低");
                    finding.detail = QStringLiteral(
                                        "第 %1 页的图片 /%2 有效分辨率只有 %3 dpi"
                                        "（%4 x %5 像素），低于印刷常用的 %6 dpi 下限。")
                                        .arg(pageIndex + 1)
                                        .arg(image.second)
                                        .arg(numberText(bestDpi))
                                        .arg(width.isInteger() ? width.getIntValue() : 0)
                                        .arg(height.isInteger() ? height.getIntValue() : 0)
                                        .arg(numberText(kMinImageDpi));
                    finding.severity = PreflightSeverity::Warning;
                    finding.pageIndex = pageIndex;
                    report.findings.append(finding);
                }
            }

            // 4.3 透明度组。
            if (page.getKey("/Group").isDictionary()) {
                ++transparencyPages;
                if (firstTransparencyPage < 0) firstTransparencyPage = pageIndex;
            }
        }

        if (transparencyPages > 0) {
            PreflightFinding finding;
            finding.code = QStringLiteral("transparency-group");
            finding.title = QStringLiteral("存在透明度组");
            finding.detail = QStringLiteral("共有 %1 页带 /Group 透明度组（从第 %2 页开始）；"
                                           "部分 RIP 会在拼版时按自己的方式渲染透明区域。")
                                .arg(transparencyPages)
                                .arg(firstTransparencyPage + 1);
            finding.severity = PreflightSeverity::Info;
            finding.pageIndex = firstTransparencyPage;
            report.findings.append(finding);
        }

        // 计数统一在最后按结论的实际级别重算一遍：
        // 边加结论边加计数很容易漏一处，重算则永远与结论一致。
        report.errorCount = 0;
        report.warningCount = 0;
        for (const PreflightFinding& finding : report.findings) {
            if (finding.severity == PreflightSeverity::Error) ++report.errorCount;
            if (finding.severity == PreflightSeverity::Warning) ++report.warningCount;
        }
        report.ok = report.errorCount == 0;
        if (error != nullptr) error->clear();
        return report;
    } catch (const std::exception& exception) {
        return fail(QStringLiteral("解析这份 PDF 失败：%1")
                        .arg(QString::fromUtf8(exception.what())));
    }
#endif
}

}  // namespace aipdf::pdf
