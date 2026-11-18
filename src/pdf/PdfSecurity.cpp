#include "pdf/PdfSecurity.h"

#include "pdf/PdfRevisionCounter.h"

#include <QRandomGenerator>
#include <QSet>
#include <QStringList>
#include <QVector>

#include <deque>
#include <memory>
#include <string>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFTokenizer.hh>
#include <qpdf/QPDFWriter.hh>
#endif

namespace aipdf::pdf {

namespace {

QString toLocal(const QString& text) {
    return text;
}

// 隐藏数据类别的 id。
//
// 为什么用 ASCII 字面量而不是中文：这些 id 会出现在界面的勾选状态、测试断言和
// 日志里；中文标题由 app 层按 id 拼（见交付报告里的 i18n 清单）。
// 领域层不出现 tr()，也不该出现给人看的句子。
namespace category_id {
constexpr const char* kMetadata = "metadata";
constexpr const char* kIncrementalHistory = "incremental-history";
constexpr const char* kEmbeddedFiles = "embedded-files";
constexpr const char* kFileAttachments = "file-attachments";
constexpr const char* kScripts = "scripts";
constexpr const char* kActions = "actions";
constexpr const char* kAnnotationActions = "annotation-actions";
constexpr const char* kFieldActions = "field-actions";
constexpr const char* kFormData = "form-data";
constexpr const char* kThumbnails = "thumbnails";
constexpr const char* kAnnotations = "annotations";
constexpr const char* kInvisibleText = "invisible-text";
constexpr const char* kLayers = "layers";
constexpr const char* kXfa = "xfa";
constexpr const char* kSignatures = "signatures";
constexpr const char* kMetadataStreams = "metadata-streams";
}  // namespace category_id

// 受支持的类别与它们的固定顺序。
//
// 界面按这个顺序渲染勾选列表：顺序变了，用户上次勾的哪几项就对不上位置了。
const QVector<QString>& supportedCategoryIds() {
    static const QVector<QString> ids = {
        QString::fromLatin1(category_id::kMetadata),
        QString::fromLatin1(category_id::kIncrementalHistory),
        QString::fromLatin1(category_id::kEmbeddedFiles),
        QString::fromLatin1(category_id::kFileAttachments),
        QString::fromLatin1(category_id::kScripts),
        QString::fromLatin1(category_id::kActions),
        QString::fromLatin1(category_id::kAnnotationActions),
        QString::fromLatin1(category_id::kFieldActions),
        QString::fromLatin1(category_id::kFormData),
        QString::fromLatin1(category_id::kThumbnails),
        QString::fromLatin1(category_id::kAnnotations),
        QString::fromLatin1(category_id::kInvisibleText),
        QString::fromLatin1(category_id::kLayers),
        QString::fromLatin1(category_id::kXfa),
        QString::fromLatin1(category_id::kSignatures),
        QString::fromLatin1(category_id::kMetadataStreams),
    };
    return ids;
}

#ifdef AIPDF_WITH_QPDF

// 打开一份可能是加密的 PDF，并说清楚「为什么打不开」。
//
// 为什么要有这层：加密文档不给密码时 qpdf 直接抛异常（invalid password），
// 于是枚举与清理都拿不到任何信息——用户看到的是一句「隐藏数据枚举失败」，
// 根本不知道「其实只要有密码就能查」。
//
// 判定顺序：先带密码试（对明文文档无害，qpdf 会忽略它），再不带密码试。
// 两次都失败才报错；错密码与文件损坏必须分开说，因为处理方式完全不同。
enum class OpenOutcome {
    Ok,
    NeedsPassword,  // 加密了，而且没给密码
    WrongPassword,  // 加密了，密码不对
    Damaged,        // 解析失败，与密码无关
};

OpenOutcome openForInspection(std::shared_ptr<QPDF>& pdf,
                              const QByteArray& bytes,
                              const QString& password,
                              bool* encrypted,
                              std::string* detail) {
    const QByteArray utf8 = password.toUtf8();
    const char* passwordArg = utf8.isEmpty() ? nullptr : utf8.constData();

    const auto attempt = [&bytes, detail](const char* pwd) -> std::shared_ptr<QPDF> {
        auto candidate = QPDF::create();
        try {
            candidate->processMemoryFile("hidden-data.pdf", bytes.constData(),
                                         static_cast<size_t>(bytes.size()), pwd);
        } catch (const std::exception& exception) {
            if (detail != nullptr && detail->empty()) {
                *detail = exception.what();
            }
            return nullptr;
        }
        return candidate;
    };

    // 第一次：带密码试。
    if (std::shared_ptr<QPDF> candidate = attempt(passwordArg)) {
        pdf = candidate;
        *encrypted = candidate->isEncrypted();
        return OpenOutcome::Ok;
    }
    // 第二次：不带密码试（明文文档、用户密码为空的加密文档走这条路）。
    if (std::shared_ptr<QPDF> candidate = attempt(nullptr)) {
        pdf = candidate;
        *encrypted = candidate->isEncrypted();
        return OpenOutcome::Ok;
    }

    // 两次都失败：分清「密码不对」与「文件坏了」。
    //
    // 判据是 qpdf 的错误文本：解密失败时报的是 invalid password，
    // 别的解析错误不会带这个词。
    if (detail != nullptr && detail->find("password") != std::string::npos) {
        return password.isEmpty() ? OpenOutcome::NeedsPassword : OpenOutcome::WrongPassword;
    }
    return OpenOutcome::Damaged;
}

// 打开失败时给用户看的中文原因。
QString describeOpenFailure(OpenOutcome outcome, const std::string& detail) {
    switch (outcome) {
    case OpenOutcome::NeedsPassword:
        return toLocal(QStringLiteral("文档已加密，需要正确的打开密码才能检查隐藏信息"));
    case OpenOutcome::WrongPassword:
        return toLocal(QStringLiteral("文档已加密，输入的密码不正确"));
    case OpenOutcome::Damaged:
    default:
        return toLocal(QStringLiteral("无法解析文档：%1")
                            .arg(QString::fromUtf8(detail.c_str())));
    }
}

// qpdf 的 R3 打印枚举：无 / 低分辨率 / 完整。
qpdf_r3_print_e printMode(const PdfPermissions& permissions) {
    if (!permissions.allowPrinting) {
        return qpdf_r3p_none;
    }
    return permissions.allowHighQualityPrinting ? qpdf_r3p_full : qpdf_r3p_low;
}

QString randomOwnerPassword() {
    // 32 个十六进制字符：不需要可记忆，只要不可猜。
    QString result;
    result.reserve(32);
    for (int i = 0; i < 4; ++i) {
        const quint32 value = QRandomGenerator::system()->generate();
        result += QStringLiteral("%1").arg(value, 8, 16, QLatin1Char('0'));
    }
    return result;
}

// 这个批注还挂着嵌入文件吗？
//
// /FS → /EF → 至少一个流：说明文件还在（要么是我们没清掉的，要么是别的附件）。
// 文件已经被清掉时 /EF 没了，批注只剩空壳，这时应该把批注一起清掉。
bool carriesEmbeddedFile(const QPDFObjectHandle& annot) {
    const QPDFObjectHandle spec = annot.getKey("/FS");
    if (!spec.isDictionary()) {
        return false;
    }
    const QPDFObjectHandle embeddedFiles = spec.getKey("/EF");
    if (!embeddedFiles.isDictionary()) {
        return false;
    }
    for (const std::string& key : embeddedFiles.getKeys()) {
        if (embeddedFiles.getKey(key).isStream()) {
            return true;
        }
    }
    return false;
}

// 各类隐藏数据的计数结果。每一类的口径写在采集它的代码旁边。
struct HiddenDataCounts {
    int metadata = 0;
    int metadataStreams = 0;
    int incrementalHistory = 0;
    int embeddedFiles = 0;
    int fileAttachments = 0;
    int scripts = 0;
    int actions = 0;
    int annotationActions = 0;
    int fieldActions = 0;
    int formData = 0;
    int thumbnails = 0;
    int annotations = 0;
    int totalAnnotations = 0;  // 只放进 detail，不单独成类
    int invisibleText = 0;
    int layers = 0;
    int xfa = 0;
    int signatures = 0;
};

// 名称树里的条目数。
//
// 依据：名称树的 /Names 数组是「名字、值」成对出现，/Kids 指向子节点。
// 逐节点走一遍，不要求名称树「合法」——损坏的名称树恰恰是最该被列出来的情况。
int countNameTreeEntries(const QPDFObjectHandle& node, int depth = 0) {
    if (!node.isDictionary() || depth > 64) {
        return 0;
    }
    int count = 0;
    const QPDFObjectHandle names = node.getKey("/Names");
    if (names.isArray()) {
        count += static_cast<int>(names.getArrayAsVector().size() / 2);
    }
    const QPDFObjectHandle kids = node.getKey("/Kids");
    if (kids.isArray()) {
        for (const QPDFObjectHandle& kid : kids.getArrayAsVector()) {
            count += countNameTreeEntries(kid, depth + 1);
        }
    }
    return count;
}

// 采集 AcroForm 的字段字典（含 /Kids 里的子字段）。
//
// 依据：字段树挂在 /AcroForm /Fields 下；带 /T（部分名字）或 /FT（字段类型）
// 的才是字段，/Kids 里也可能只是控件批注，不能当成字段。
void collectFormFields(const QPDFObjectHandle& node,
                       QVector<QPDFObjectHandle>& out,
                       int depth = 0) {
    if (!node.isDictionary() || depth > 64) {
        return;
    }
    if (node.hasKey("/T") || node.hasKey("/FT")) {
        out.push_back(node);
    }
    const QPDFObjectHandle kids = node.getKey("/Kids");
    if (!kids.isArray()) {
        return;
    }
    for (const QPDFObjectHandle& kid : kids.getArrayAsVector()) {
        if (kid.isDictionary() &&
            (kid.hasKey("/T") || kid.hasKey("/FT") || kid.hasKey("/Kids"))) {
            collectFormFields(kid, out, depth + 1);
        }
    }
}

// 这个字段是不是签名字段？
bool isSignatureField(const QPDFObjectHandle& field) {
    const QPDFObjectHandle type = field.getKey("/FT");
    return type.isName() && type.getName() == "/Sig";
}

// 批注是不是「隐藏批注」或「携带数据的批注」？
//
// 依据：/F 标志的第 1、2 位（Invisible、Hidden）表示批注默认不显示；
// 另外几种子类型（屏幕、影片、声音、富媒体、3D）本身就是数据载体，
// 它们往往还挂着动作和脚本，属于隐藏信息而不是阅读内容。
bool isHiddenAnnotation(const QPDFObjectHandle& annot) {
    if (!annot.isDictionary()) {
        return false;
    }
    const QPDFObjectHandle flags = annot.getKey("/F");
    if (flags.isInteger() && (flags.getIntValue() & 3) != 0) {
        return true;
    }
    const QPDFObjectHandle subtype = annot.getKey("/Subtype");
    if (subtype.isName()) {
        const std::string name = subtype.getName();
        return name == "/Screen" || name == "/Movie" || name == "/Sound" ||
               name == "/RichMedia" || name == "/3D";
    }
    return false;
}

// 是不是附件批注？
bool isFileAttachmentAnnotation(const QPDFObjectHandle& annot) {
    if (!annot.isDictionary()) {
        return false;
    }
    const QPDFObjectHandle subtype = annot.getKey("/Subtype");
    return subtype.isName() && subtype.getName() == "/FileAttachment";
}

// 字典里 /AA 动作的条数。
//
// /AA 是一个字典，键是触发器（/E 进入、/X 离开、/D 放下、/Fo 聚焦……），
// 值是动作本身。所以「条数」= 触发器个数，也就是这份文档里埋了多少个自动动作。
int countActionEntries(const QPDFObjectHandle& dict) {
    if (!dict.isDictionary()) {
        return 0;
    }
    return static_cast<int>(dict.getKeys().size());
}

// 这个批注是不是表单控件（widget）？
//
// 为什么必须分开：表单控件的 /AA（校验、格式化脚本的挂点）与普通批注的 /AA
// （点击、进入时触发的动作）是两回事，必须分成两类，否则「只清批注动作」
// 会顺手把表单的校验脚本也删掉。
//
// 依据：控件要么与字段合并（自己带 /FT），要么通过 /Parent 挂在字段下面。
bool isFormWidget(const QPDFObjectHandle& annot) {
    if (!annot.isDictionary()) {
        return false;
    }
    if (annot.hasKey("/FT")) {
        return true;  // 字段与控件合并成同一个字典
    }
    QPDFObjectHandle node = annot.getKey("/Parent");
    for (int depth = 0; depth < 8 && node.isDictionary(); ++depth) {
        if (node.hasKey("/T") || node.hasKey("/FT")) {
            return true;
        }
        node = node.getKey("/Parent");
    }
    return false;
}

// 内容流里「不可见文字」的扫描器。
//
// 依据：PDF 用 Tr 算子设置文字渲染模式，3 = 不可见——OCR 文字层就是靠它藏在
// 扫描件底下。算子都是「操作数在前、算子在后」，所以 Tr 的操作数就是它前面
// 那个整数。
//
// 为什么自己扫：不可见文字只存在于内容流里，删别的键都删不掉它。
// qpdf 的 TokenFilter 能在写出时改写内容流，正好对得上。
class InvisibleTextScanner : public QPDFObjectHandle::TokenFilter {
  public:
    explicit InvisibleTextScanner(bool strip) : strip_(strip) {}
    ~InvisibleTextScanner() override = default;

    void handleToken(QPDFTokenizer::Token const& token) override {
        const QPDFTokenizer::token_type_e type = token.getType();
        if (type == QPDFTokenizer::tt_space || type == QPDFTokenizer::tt_comment) {
            // 空白与注释在清除时统一丢掉：token 之间我们自己补空格，
            // 这样「12 34」不会被粘成「1234」。
            return;
        }
        if (token.isWord()) {
            handleOperator(token);
            return;
        }
        // 操作数：先攒着——它可能正是某个显示算子的操作数，清除时要一起拿走。
        pending_.push_back(token);
        if (pending_.size() > 3) {
            flushOldest();
        }
    }

    void handleEOF() override { flushAll(); }

    int invisibleTextCount() const { return count_; }

  private:
    void handleOperator(QPDFTokenizer::Token const& token) {
        const std::string value = token.getValue();
        if (value == "Tr") {
            renderMode_ = pendingRenderMode();
        } else if (value == "q") {
            renderModeStack_.push_back(renderMode_);
        } else if (value == "Q") {
            if (!renderModeStack_.empty()) {
                renderMode_ = renderModeStack_.back();
                renderModeStack_.pop_back();
            }
        } else if (value == "Tj" || value == "TJ" || value == "'" || value == "\"") {
            if (renderMode_ == 3) {
                ++count_;
                if (strip_) {
                    // " 的操作数是三个（字距、字符间距、字符串），其余都是一个。
                    dropOperands(value == "\"" ? 3 : 1);
                    return;  // 算子本身也不写出去
                }
            }
        }
        flushAll();
        if (strip_) {
            // 算子前面也要有分隔符：不然「24」和「Tf」会粘成「24Tf」。
            write(" ");
            writeToken(token);
        }
    }

    int pendingRenderMode() const {
        if (pending_.empty()) {
            return renderMode_;
        }
        const QPDFTokenizer::Token& token = pending_.back();
        if (!token.isInteger()) {
            return renderMode_;
        }
        try {
            return std::stoi(token.getValue());
        } catch (const std::exception&) {
            return renderMode_;
        }
    }

    void dropOperands(int count) {
        for (int i = 0; i < count && !pending_.empty(); ++i) {
            pending_.pop_back();
        }
    }

    void flushOldest() {
        if (pending_.empty()) {
            return;
        }
        if (strip_) {
            write(" ");
            writeToken(pending_.front());
        }
        pending_.pop_front();
    }

    void flushAll() {
        for (const QPDFTokenizer::Token& token : pending_) {
            if (strip_) {
                write(" ");
                writeToken(token);
            }
        }
        pending_.clear();
    }

    bool strip_ = false;
    int count_ = 0;
    int renderMode_ = 0;
    std::vector<int> renderModeStack_;
    std::deque<QPDFTokenizer::Token> pending_;
};

// 按谓词删掉页面上的批注。
using AnnotationPredicate = bool (*)(const QPDFObjectHandle&);

void removeAnnotations(const std::vector<QPDFPageObjectHelper>& pages,
                       AnnotationPredicate predicate) {
    for (const QPDFPageObjectHelper& page : pages) {
        QPDFObjectHandle pageDict = page.getObjectHandle();
        const QPDFObjectHandle annots = pageDict.getKey("/Annots");
        if (!annots.isArray()) {
            continue;
        }
        std::vector<QPDFObjectHandle> kept;
        bool removed = false;
        for (const QPDFObjectHandle& annot : annots.getArrayAsVector()) {
            if (predicate(annot)) {
                removed = true;
                continue;
            }
            kept.push_back(annot);
        }
        if (removed) {
            pageDict.replaceKey("/Annots", QPDFObjectHandle::newArray(kept));
        }
    }
}

// 删掉预览图（缩略图）引用。
//
// 依据：PDF 规范没有规定缩略图放在哪，所以只认两个可验证的约定——
// 页面 / catalog / 批注上的 /Thumb 键，以及页面 /Resources 里以 /Thumb
// 开头的图像名。
void removeThumbnails(QPDFObjectHandle root,
                      std::vector<QPDFPageObjectHelper>& pages) {
    const auto clearThumb = [](QPDFObjectHandle dict) {
        if (dict.isDictionary() && dict.getKey("/Thumb").isStream()) {
            dict.removeKey("/Thumb");
        }
    };
    clearThumb(root);
    for (QPDFPageObjectHelper& page : pages) {
        QPDFObjectHandle pageDict = page.getObjectHandle();
        clearThumb(pageDict);
        const QPDFObjectHandle annots = pageDict.getKey("/Annots");
        if (annots.isArray()) {
            for (const QPDFObjectHandle& annot : annots.getArrayAsVector()) {
                clearThumb(annot);
            }
        }
        const QPDFObjectHandle resources = page.getAttribute("/Resources", false);
        if (!resources.isDictionary()) {
            continue;
        }
        QPDFObjectHandle xobjects = resources.getKey("/XObject");
        if (!xobjects.isDictionary()) {
            continue;
        }
        std::vector<std::string> toRemove;
        for (const std::string& key : xobjects.getKeys()) {
            if (key.rfind("/Thumb", 0) == 0) {
                toRemove.push_back(key);
            }
        }
        for (const std::string& key : toRemove) {
            xobjects.removeKey(key);
        }
    }
}

// 删掉图层（可选内容组）配置。
//
// 除了 catalog 的 /OCProperties，页面资源里的 /Properties 也要一起清：
// 留着它，图层的名字（常写着「内部」「机密」这类字样）还在文件里。
void removeLayers(QPDFObjectHandle root,
                  std::vector<QPDFPageObjectHelper>& pages) {
    root.removeKey("/OCProperties");
    for (QPDFPageObjectHelper& page : pages) {
        QPDFObjectHandle resources = page.getAttribute("/Resources", false);
        if (!resources.isDictionary()) {
            continue;
        }
        QPDFObjectHandle properties = resources.getKey("/Properties");
        if (!properties.isDictionary()) {
            continue;
        }
        std::vector<std::string> toRemove;
        for (const std::string& key : properties.getKeys()) {
            const QPDFObjectHandle value = properties.getKey(key);
            if (!value.isDictionary()) {
                continue;
            }
            const QPDFObjectHandle type = value.getKey("/Type");
            if (type.isName() &&
                (type.getName() == "/OCG" || type.getName() == "/OCMD")) {
                toRemove.push_back(key);
            }
        }
        for (const std::string& key : toRemove) {
            properties.removeKey(key);
        }
    }
}

// 清掉字段里已经填过的值。
//
// signatures = true 时只清签名字段，否则只清普通字段：两类不能互相影响，
// 否则「只清一类、别的类不变」这条性质就不成立。
void clearFieldValues(const QPDFObjectHandle& root, bool signatures) {
    const QPDFObjectHandle acroForm = root.getKey("/AcroForm");
    if (!acroForm.isDictionary()) {
        return;
    }
    QVector<QPDFObjectHandle> fields;
    const QPDFObjectHandle fieldArray = acroForm.getKey("/Fields");
    if (fieldArray.isArray()) {
        for (const QPDFObjectHandle& field : fieldArray.getArrayAsVector()) {
            collectFormFields(field, fields);
        }
    }
    for (const QPDFObjectHandle& field : fields) {
        if (isSignatureField(field) != signatures) {
            continue;
        }
        QPDFObjectHandle mutableField = field;
        mutableField.removeKey("/V");
        // 控件批注可能带着同一份值（字段与控件合并成一个对象时就是同一个对象）。
        const QPDFObjectHandle kids = field.getKey("/Kids");
        if (kids.isArray()) {
            for (const QPDFObjectHandle& kid : kids.getArrayAsVector()) {
                const QPDFObjectHandle subtype = kid.getKey("/Subtype");
                if (kid.isDictionary() && subtype.isName() &&
                    subtype.getName() == "/Widget") {
                    QPDFObjectHandle mutableKid = kid;
                    mutableKid.removeKey("/V");
                }
            }
        }
    }
}

// 把各类隐藏数据的条数数出来。
//
// 每一类的判断依据都写在对应的那几行旁边——不是「看起来像有」。
HiddenDataCounts collectHiddenDataCounts(QPDF& pdf, const QByteArray& rawBytes) {
    HiddenDataCounts counts;
    const QPDFObjectHandle root = pdf.getRoot();
    const QPDFObjectHandle trailer = pdf.getTrailer();

    // metadata：trailer 的 /Info 文档信息字典（条目数 = 字典里的键数）。
    const QPDFObjectHandle info = trailer.getKey("/Info");
    if (info.isDictionary()) {
        const auto keys = info.getKeys();
        counts.metadata = keys.empty() ? 1 : static_cast<int>(keys.size());
    }

    // metadata-streams：catalog 与页面上的 /Metadata（XMP 之类）流。
    if (root.getKey("/Metadata").isStream()) {
        ++counts.metadataStreams;
    }

    // incremental-history：这份文档自己的修订数减一。
    //
    // 依据：被增量更新过的 PDF，每多一版就多一节 xref。
    // 为什么不能数字节里的 %%EOF：把整份 PDF 当附件嵌进来时，内层 PDF 的
    // %%EOF 也在字节里，会被多算一截。改走文档自己的 xref 链（/Prev），
    // 内层 PDF 的偏移在这个坐标系里是乱的，走不到它们。
    counts.incrementalHistory = qMax(0, countPdfRevisions(rawBytes) - 1);

    // embedded-files / scripts：名称树里的条目数。
    const QPDFObjectHandle names = root.getKey("/Names");
    if (names.isDictionary()) {
        counts.embeddedFiles = countNameTreeEntries(names.getKey("/EmbeddedFiles"));
        counts.scripts = countNameTreeEntries(names.getKey("/JavaScript"));
    }

    // file-attachments：catalog 的 /AF 关联文件数组。
    const QPDFObjectHandle associated = root.getKey("/AF");
    if (associated.isArray()) {
        counts.fileAttachments += static_cast<int>(associated.getArrayAsVector().size());
    }

    // actions：自动动作（不需要用户点一下就会执行的那种）。
    //
    // 依据：catalog 的 /OpenAction，以及 catalog 与页面上的 /AA。
    // 批注自己的 /AA 不算进这一类——它是「用户点了某个控件才触发」的行为，
    // 性质与文档自动动作不同，混在一起会让「只清一类、别的类不变」这条性质不成立。
    //
    // 批注级与字段级的 /AA 各自成类（annotation-actions / field-actions）：
    // 它们是用户点某个控件时才触发的动作，而且必须分开——
    // 「清批注动作」不该顺手删掉表单的校验脚本。
    if (root.hasKey("/OpenAction")) {
        ++counts.actions;
    }
    counts.actions += countActionEntries(root.getKey("/AA"));

    std::vector<QPDFPageObjectHelper> pages =
        QPDFPageDocumentHelper::get(pdf).getAllPages();
    for (QPDFPageObjectHelper& page : pages) {
        QPDFObjectHandle pageDict = page.getObjectHandle();
        const QPDFObjectHandle annots = pageDict.getKey("/Annots");
        if (annots.isArray()) {
            for (const QPDFObjectHandle& annot : annots.getArrayAsVector()) {
                ++counts.totalAnnotations;
                if (isFileAttachmentAnnotation(annot)) {
                    ++counts.fileAttachments;
                } else if (isHiddenAnnotation(annot)) {
                    ++counts.annotations;
                }
                // 批注级 /AA：表单控件的不算（那是字段级那一类）。
                if (!isFormWidget(annot)) {
                    counts.annotationActions += countActionEntries(annot.getKey("/AA"));
                }
            }
        }
        counts.actions += countActionEntries(pageDict.getKey("/AA"));
        if (pageDict.getKey("/Metadata").isStream()) {
            ++counts.metadataStreams;
        }
        if (pageDict.getKey("/Thumb").isStream()) {
            ++counts.thumbnails;
        }
        const QPDFObjectHandle resources = page.getAttribute("/Resources", false);
        if (resources.isDictionary()) {
            const QPDFObjectHandle xobjects = resources.getKey("/XObject");
            if (xobjects.isDictionary()) {
                for (const std::string& key : xobjects.getKeys()) {
                    if (key.rfind("/Thumb", 0) == 0) {
                        ++counts.thumbnails;
                    }
                }
            }
        }
        // invisible-text：内容流里不可见渲染模式下的文字。
        try {
            InvisibleTextScanner scanner(false);
            page.filterContents(&scanner);
            counts.invisibleText += scanner.invisibleTextCount();
        } catch (const std::exception&) {
            // 内容流坏掉时不能把整份清单判成失败：这一类先记 0。
        }
    }

    // layers：catalog /OCProperties 里定义的图层（OCG）。
    const QPDFObjectHandle ocProperties = root.getKey("/OCProperties");
    if (ocProperties.isDictionary()) {
        const QPDFObjectHandle ocgs = ocProperties.getKey("/OCGs");
        int count = 0;
        if (ocgs.isArray()) {
            count = static_cast<int>(ocgs.getArrayAsVector().size());
        }
        counts.layers = count > 0 ? count : 1;
    }

    // form-data / signatures / xfa：AcroForm。
    const QPDFObjectHandle acroForm = root.getKey("/AcroForm");
    if (acroForm.isDictionary()) {
        QVector<QPDFObjectHandle> fields;
        const QPDFObjectHandle fieldArray = acroForm.getKey("/Fields");
        if (fieldArray.isArray()) {
            for (const QPDFObjectHandle& field : fieldArray.getArrayAsVector()) {
                collectFormFields(field, fields);
            }
        }
        for (const QPDFObjectHandle& field : fields) {
            const bool hasValue = !field.getKey("/V").isNull();
            if (isSignatureField(field)) {
                if (hasValue) {
                    ++counts.signatures;
                }
            } else if (hasValue) {
                ++counts.formData;
            }
            // 字段级 /AA：字段字典自己的，外加纯控件（没有自己的名字/类型）的。
            // 带 /T 或 /FT 的子字段自己就在 fields 里，单独再数一遍就重复了。
            counts.fieldActions += countActionEntries(field.getKey("/AA"));
            const QPDFObjectHandle kids = field.getKey("/Kids");
            if (kids.isArray()) {
                for (const QPDFObjectHandle& kid : kids.getArrayAsVector()) {
                    if (kid.isDictionary() && !kid.hasKey("/T") && !kid.hasKey("/FT")) {
                        counts.fieldActions += countActionEntries(kid.getKey("/AA"));
                    }
                }
            }
        }
        const QPDFObjectHandle xfa = acroForm.getKey("/XFA");
        if (xfa.isArray()) {
            counts.xfa = static_cast<int>(xfa.getArrayAsVector().size());
        } else if (!xfa.isNull()) {
            counts.xfa = 1;
        }
    }

    return counts;
}

// 把计数结果整理成清单。
QVector<HiddenDataCategory> buildCategories(const HiddenDataCounts& counts) {
    QVector<HiddenDataCategory> categories;
    categories.reserve(supportedCategoryIds().size());
    const auto add = [&categories](const char* id, int count, const QString& detail) {
        HiddenDataCategory category;
        category.id = QString::fromLatin1(id);
        category.itemCount = count;
        category.detail = detail;
        categories.push_back(category);
    };
    add(category_id::kMetadata, counts.metadata,
        QStringLiteral("trailer /Info entries: %1").arg(counts.metadata));
    add(category_id::kIncrementalHistory, counts.incrementalHistory,
        QStringLiteral("extra revisions (xref chain - 1): %1").arg(counts.incrementalHistory));
    add(category_id::kEmbeddedFiles, counts.embeddedFiles,
        QStringLiteral("/Names /EmbeddedFiles entries: %1").arg(counts.embeddedFiles));
    add(category_id::kFileAttachments, counts.fileAttachments,
        QStringLiteral("/FileAttachment annots + /AF entries: %1").arg(counts.fileAttachments));
    add(category_id::kScripts, counts.scripts,
        QStringLiteral("/Names /JavaScript entries: %1").arg(counts.scripts));
    add(category_id::kActions, counts.actions,
        QStringLiteral("/OpenAction + /AA actions: %1").arg(counts.actions));
    add(category_id::kAnnotationActions, counts.annotationActions,
        QStringLiteral("annotation /AA triggers: %1").arg(counts.annotationActions));
    add(category_id::kFieldActions, counts.fieldActions,
        QStringLiteral("form field /AA triggers: %1").arg(counts.fieldActions));
    add(category_id::kFormData, counts.formData,
        QStringLiteral("AcroForm fields with a value: %1").arg(counts.formData));
    add(category_id::kThumbnails, counts.thumbnails,
        QStringLiteral("/Thumb refs + /Thumb* images: %1").arg(counts.thumbnails));
    add(category_id::kAnnotations, counts.annotations,
        QStringLiteral("hidden or data-carrying annots: %1 of %2 total")
            .arg(counts.annotations)
            .arg(counts.totalAnnotations));
    add(category_id::kInvisibleText, counts.invisibleText,
        QStringLiteral("text ops with render mode 3: %1").arg(counts.invisibleText));
    add(category_id::kLayers, counts.layers,
        QStringLiteral("/OCProperties /OCGs: %1").arg(counts.layers));
    add(category_id::kXfa, counts.xfa,
        QStringLiteral("/AcroForm /XFA packets: %1").arg(counts.xfa));
    add(category_id::kSignatures, counts.signatures,
        QStringLiteral("signature fields carrying a signature: %1").arg(counts.signatures));
    add(category_id::kMetadataStreams, counts.metadataStreams,
        QStringLiteral("/Metadata streams: %1").arg(counts.metadataStreams));
    return categories;
}

#else

QString missingQpdfError() {
    return QStringLiteral("本次构建没有启用 qpdf，加密与权限位不可用");
}

QString missingQpdfEmbeddedFilesError() {
    return QStringLiteral("本次构建没有启用 qpdf，嵌入文件清理不可用");
}

#endif

}  // namespace

bool encryptionAvailable() {
#ifdef AIPDF_WITH_QPDF
    return true;
#else
    return false;
#endif
}

QByteArray encryptPdf(const QByteArray& input,
                      const PdfEncryptionOptions& options,
                      QString* error) {
    if (input.isEmpty()) {
        if (error != nullptr) {
            *error = toLocal(QStringLiteral("没有可加密的内容"));
        }
        return {};
    }
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        pdf.processMemoryFile("input.pdf",
                              input.constData(),
                              static_cast<size_t>(input.size()));

        // 空所有者密码 = 随机生成：避免「留空就等于不设限」这种看起来安全、
        // 实际谁都能改权限的假保护。用户密码留空是另一回事：那表示允许无密码打开。
        const QByteArray userPassword = options.userPassword.toUtf8();
        QByteArray ownerPassword = options.ownerPassword.toUtf8();
        if (ownerPassword.isEmpty()) {
            ownerPassword = randomOwnerPassword().toUtf8();
        }

        QPDFWriter writer(pdf);
        writer.setOutputMemory();
        writer.setR6EncryptionParameters(
            userPassword.constData(),
            ownerPassword.constData(),
            options.permissions.allowAccessibility,
            options.permissions.allowCopying,
            options.permissions.allowAssembly,
            options.permissions.allowAnnotations,
            options.permissions.allowFormFilling,
            options.permissions.allowModifying,
            printMode(options.permissions),
            options.encryptMetadata);
        writer.write();

        std::unique_ptr<Buffer> buffer(writer.getBuffer());
        if (!buffer || buffer->getSize() == 0) {
            if (error != nullptr) {
                *error = toLocal(QStringLiteral("qpdf 没有写出加密结果"));
            }
            return {};
        }
        QByteArray output(reinterpret_cast<const char*>(buffer->getBuffer()),
                          static_cast<int>(buffer->getSize()));

        // 自检：必须能用用户密码重新打开，且真的是加密文件。
        // 这一步挡住的是「写出了字节，但密码对不上」的静默失败。
        QPDF reopened;
        reopened.processMemoryFile("encrypted.pdf",
                                   output.constData(),
                                   static_cast<size_t>(output.size()),
                                   userPassword.constData());
        int revision = 0;
        int permissionBits = 0;
        if (!reopened.isEncrypted(revision, permissionBits) || revision < 6) {
            if (error != nullptr) {
                *error = toLocal(QStringLiteral("加密自检失败：写出的文件不是 AES-256 加密文档"));
            }
            return {};
        }
        return output;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = toLocal(QStringLiteral("加密失败：%1")
                                 .arg(QString::fromUtf8(exception.what())));
        }
        return {};
    }
#else
    Q_UNUSED(options);
    if (error != nullptr) {
        *error = missingQpdfError();
    }
    return {};
#endif
}

QByteArray sanitizeHiddenData(const QByteArray& input, QString* error, const QString& password) {
    if (input.isEmpty()) {
        if (error != nullptr) *error = toLocal(QStringLiteral("没有可清理的内容"));
        return {};
    }
#ifdef AIPDF_WITH_QPDF
    std::shared_ptr<QPDF> pdf;
    bool encrypted = false;
    std::string detail;
    const OpenOutcome outcome = openForInspection(pdf, input, password, &encrypted, &detail);
    if (outcome != OpenOutcome::Ok) {
        if (error != nullptr) {
            *error = describeOpenFailure(outcome, detail);
        }
        return {};
    }
    try {
        QPDFObjectHandle trailer = pdf->getTrailer();
        trailer.removeKey("/Info");

        QPDFObjectHandle root = pdf->getRoot();
        for (const char* key : {"/Metadata", "/PieceInfo", "/OpenAction", "/AA"}) {
            root.removeKey(key);
        }
        if (root.hasKey("/Names")) {
            QPDFObjectHandle names = root.getKey("/Names");
            if (names.isDictionary()) {
                names.removeKey("/JavaScript");
                names.removeKey("/EmbeddedFiles");
            }
        }
        for (QPDFObjectHandle page : pdf->getAllPages()) {
            page.removeKey("/AA");
            page.removeKey("/PieceInfo");
            page.removeKey("/Metadata");
        }

        QPDFWriter writer(*pdf);
        writer.setOutputMemory();
        writer.setObjectStreamMode(qpdf_o_generate);
        if (encrypted) {
            // 加密文档清理完仍然加密：清成明文等于替用户拆了锁。
            writer.setPreserveEncryption(true);
        }
        writer.write();
        std::unique_ptr<Buffer> buffer(writer.getBuffer());
        if (!buffer || buffer->getSize() == 0) {
            if (error != nullptr) *error = toLocal(QStringLiteral("qpdf 没有写出清理结果"));
            return {};
        }
        return QByteArray(reinterpret_cast<const char*>(buffer->getBuffer()),
                          static_cast<int>(buffer->getSize()));
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = toLocal(QStringLiteral("隐藏信息清理失败：%1")
                                 .arg(QString::fromUtf8(exception.what())));
        }
        return {};
    }
#else
    Q_UNUSED(password);
    if (error != nullptr) *error = missingQpdfError();
    return {};
#endif
}

QByteArray removeEmbeddedFiles(const QByteArray& input, QString* error, const QString& password) {
    if (input.isEmpty()) {
        if (error != nullptr) *error = toLocal(QStringLiteral("没有可清理的内容"));
        return {};
    }
#ifdef AIPDF_WITH_QPDF
    std::shared_ptr<QPDF> pdf;
    bool encrypted = false;
    std::string detail;
    const OpenOutcome outcome = openForInspection(pdf, input, password, &encrypted, &detail);
    if (outcome != OpenOutcome::Ok) {
        if (error != nullptr) {
            *error = describeOpenFailure(outcome, detail);
        }
        return {};
    }
    try {
        QPDFObjectHandle root = pdf->getRoot();

        // 1) 名称树里的嵌入文件：用 qpdf 官方的接口。
        //    removeEmbeddedFile() 不只是把名字从树上摘掉，它还会「掏空」文件规格字典，
        //    于是批注里指向同一个文件规格的引用一起失效——qpdf 自己的注释就是这么说的：
        //    这才叫附件真的从文件里消失，而不只是从附件列表里消失。
        try {
            QPDFEmbeddedFileDocumentHelper& embedded =
                QPDFEmbeddedFileDocumentHelper::get(*pdf);
            std::vector<std::string> names;
            for (const auto& entry : embedded.getEmbeddedFiles()) {
                names.push_back(entry.first);
            }
            for (const std::string& name : names) {
                embedded.removeEmbeddedFile(name);
            }
        } catch (const std::exception&) {
            // 名称树结构不合法（qpdf 的校验过不去）时不能整个放弃：
            // 下面的手工清理照样能把索引与引用点断掉。
        }

        // 2) 名称树索引：即便上一步没能走通，也要把索引摘掉，
        //    否则文件在阅读器里仍然「可见、可点」。
        if (root.hasKey("/Names")) {
            QPDFObjectHandle names = root.getKey("/Names");
            if (names.isDictionary()) {
                names.removeKey("/EmbeddedFiles");
            }
        }

        // 3) catalog 的 /AF（Associated Files）：把文件规格拉进文档的另一条路。
        root.removeKey("/AF");

        // 4) 页面上的 /FileAttachment 批注：附件本体也是嵌入文件。
        //    只删名称树索引时，批注里的 /FS 会让整条链路继续可达，
        //    文件本体照样被写进输出文件——这正是 sanitizeHiddenData 留下的缺口。
        //    文件被清掉之后，批注只剩一个空壳（挂附件的批注没有附件就没有意义），
        //    所以连批注一起删掉。
        for (QPDFObjectHandle page : pdf->getAllPages()) {
            QPDFObjectHandle annots = page.getKey("/Annots");
            if (!annots.isArray()) {
                continue;
            }
            std::vector<QPDFObjectHandle> kept;
            bool removed = false;
            for (const QPDFObjectHandle& annot : annots.getArrayAsVector()) {
                const QPDFObjectHandle subtype = annot.getKey("/Subtype");
                if (annot.isDictionary() && subtype.isName() &&
                    subtype.getName() == "/FileAttachment" && !carriesEmbeddedFile(annot)) {
                    removed = true;
                    continue;
                }
                kept.push_back(annot);
            }
            if (removed) {
                page.replaceKey("/Annots", QPDFObjectHandle::newArray(kept));
            }
        }

        QPDFWriter writer(*pdf);
        writer.setOutputMemory();
        writer.setObjectStreamMode(qpdf_o_generate);
        // 明确要求「不可达的对象不写出去」。
        // qpdf 12.4.2 的默认行为本来就是丢弃（保留要显式开 --preserve-unreferenced），
        // 这里仍然显式写一遍：断了引用却把流本体写出去，等于「声称已清理、实际没清」。
        writer.setPreserveUnreferencedObjects(false);
        if (encrypted) {
            // 加密文档清完嵌入文件后仍然加密：清成明文等于替用户拆了锁。
            writer.setPreserveEncryption(true);
        }
        writer.write();
        std::unique_ptr<Buffer> buffer(writer.getBuffer());
        if (!buffer || buffer->getSize() == 0) {
            if (error != nullptr) *error = toLocal(QStringLiteral("qpdf 没有写出清理结果"));
            return {};
        }
        return QByteArray(reinterpret_cast<const char*>(buffer->getBuffer()),
                          static_cast<int>(buffer->getSize()));
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = toLocal(QStringLiteral("嵌入文件清理失败：%1")
                                 .arg(QString::fromUtf8(exception.what())));
        }
        return {};
    }
#else
    Q_UNUSED(password);
    if (error != nullptr) *error = missingQpdfEmbeddedFilesError();
    return {};
#endif
}

QByteArray sanitizeHiddenDataFully(const QByteArray& input, QString* error, const QString& password) {
    if (input.isEmpty()) {
        if (error != nullptr) *error = toLocal(QStringLiteral("没有可清理的内容"));
        return {};
    }
    // 顺序不能反：removeEmbeddedFiles() 要靠名称树把嵌入文件找出来，
    // 先删索引就找不到了（EmbeddedFileTests 的 removeThenSanitizeKeepsItClean
    // 就是按这个顺序断言的）。
    QByteArray withoutFiles = removeEmbeddedFiles(input, error, password);
    if (withoutFiles.isEmpty()) {
        return {};
    }
    return sanitizeHiddenData(withoutFiles, error, password);
}

QVector<HiddenDataCategory> enumerateHiddenData(const QByteArray& input,
                                                 QString* error,
                                                 const QString& password) {
    QVector<HiddenDataCategory> categories;
    if (input.isEmpty()) {
        if (error != nullptr) {
            *error = toLocal(QStringLiteral("没有可检查的内容"));
        }
        return categories;
    }
#ifdef AIPDF_WITH_QPDF
    std::shared_ptr<QPDF> pdf;
    bool encrypted = false;
    std::string detail;
    const OpenOutcome outcome = openForInspection(pdf, input, password, &encrypted, &detail);
    if (outcome != OpenOutcome::Ok) {
        if (error != nullptr) {
            *error = describeOpenFailure(outcome, detail);
        }
        return {};
    }
    try {
        // 只解析不写盘：枚举不会改变文档的任何一个字节。
        const HiddenDataCounts counts = collectHiddenDataCounts(*pdf, input);
        categories = buildCategories(counts);
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = toLocal(QStringLiteral("隐藏数据枚举失败：%1")
                                 .arg(QString::fromUtf8(exception.what())));
        }
        return {};
    }
#else
    Q_UNUSED(password);
    if (error != nullptr) {
        *error = missingQpdfError();
    }
    return {};
#endif
    return categories;
}

bool isEncryptedDocument(const QByteArray& bytes, QString* error) {
    if (bytes.isEmpty()) {
        if (error != nullptr) {
            *error = toLocal(QStringLiteral("没有可检查的内容"));
        }
        return false;
    }
#ifdef AIPDF_WITH_QPDF
    std::shared_ptr<QPDF> pdf;
    bool encrypted = false;
    std::string detail;
    const OpenOutcome outcome = openForInspection(pdf, bytes, QString(), &encrypted, &detail);
    if (outcome == OpenOutcome::Ok) {
        return encrypted;
    }
    if (outcome == OpenOutcome::NeedsPassword || outcome == OpenOutcome::WrongPassword) {
        // 打不开但明确是密码问题：它就是加密文档。
        return true;
    }
    if (error != nullptr) {
        *error = describeOpenFailure(outcome, detail);
    }
    return false;
#else
    if (error != nullptr) {
        *error = missingQpdfError();
    }
    return false;
#endif
}

bool requiresPasswordToOpen(const QByteArray& bytes, QString* error) {
    if (bytes.isEmpty()) {
        if (error != nullptr) {
            *error = toLocal(QStringLiteral("没有可检查的内容"));
        }
        return false;
    }
#ifdef AIPDF_WITH_QPDF
    std::shared_ptr<QPDF> pdf;
    bool encrypted = false;
    std::string detail;
    const OpenOutcome outcome = openForInspection(pdf, bytes, QString(), &encrypted, &detail);
    if (outcome == OpenOutcome::Ok) {
        // 空密码就能打开：要么没加密，要么用户密码为空。
        return false;
    }
    if (outcome == OpenOutcome::NeedsPassword || outcome == OpenOutcome::WrongPassword) {
        return true;
    }
    if (error != nullptr) {
        *error = describeOpenFailure(outcome, detail);
    }
    return false;
#else
    if (error != nullptr) {
        *error = missingQpdfError();
    }
    return false;
#endif
}

QByteArray sanitizeHiddenDataSelective(const QByteArray& input,
                                       const QSet<QString>& categoryIds,
                                       QString* error,
                                       const QString& password) {
    if (input.isEmpty()) {
        if (error != nullptr) {
            *error = toLocal(QStringLiteral("没有可清理的内容"));
        }
        return {};
    }

    // 未知 id 明确失败：静默忽略会让用户以为「这一类已经清过了」。
    const QVector<QString>& known = supportedCategoryIds();
    QStringList unknown;
    for (const QString& id : categoryIds) {
        if (!known.contains(id)) {
            unknown.append(id);
        }
    }
    if (!unknown.isEmpty()) {
        if (error != nullptr) {
            *error = toLocal(QStringLiteral("未知的隐藏数据类别：%1")
                                 .arg(unknown.join(QLatin1String(", "))));
        }
        return {};
    }

    // 空集合 = 什么都不做。
    //
    // 为什么不能当成「全清」：界面上的勾选框一个都没勾，用户的意思是「先别动」，
    // 而不是「全清」。把空集合解释成全清，等于让用户在一个空对话框上按确定
    // 就把附件删了。
    if (categoryIds.isEmpty()) {
        if (error != nullptr) {
            *error = toLocal(QStringLiteral("没有勾选任何隐藏数据类别，文档保持原样"));
        }
        return input;
    }

#ifdef AIPDF_WITH_QPDF
    std::shared_ptr<QPDF> pdf;
    bool encrypted = false;
    std::string detail;
    const OpenOutcome outcome = openForInspection(pdf, input, password, &encrypted, &detail);
    if (outcome != OpenOutcome::Ok) {
        if (error != nullptr) {
            *error = describeOpenFailure(outcome, detail);
        }
        return {};
    }

    try {
        QPDFObjectHandle root = pdf->getRoot();
        QPDFObjectHandle trailer = pdf->getTrailer();
        std::vector<QPDFPageObjectHelper> pages =
            QPDFPageDocumentHelper::get(*pdf).getAllPages();

        // metadata：删掉文档信息字典。
        if (categoryIds.contains(QString::fromLatin1(category_id::kMetadata))) {
            trailer.removeKey("/Info");
        }

        // metadata-streams：删掉 XMP 之类的元数据流。
        if (categoryIds.contains(QString::fromLatin1(category_id::kMetadataStreams))) {
            root.removeKey("/Metadata");
            for (QPDFPageObjectHelper& page : pages) {
                QPDFObjectHandle pageDict = page.getObjectHandle();
                pageDict.removeKey("/Metadata");
            }
        }

        // scripts：删掉文档级 JavaScript 名称树。
        if (categoryIds.contains(QString::fromLatin1(category_id::kScripts))) {
            QPDFObjectHandle names = root.getKey("/Names");
            if (names.isDictionary()) {
                names.removeKey("/JavaScript");
            }
        }

        // embedded-files：删掉名称树里的嵌入文件条目。
        //
        // 用 qpdf 官方的接口：removeEmbeddedFile() 不只是摘名字，还会把文件规格
        // 掏空，于是指向同一个文件规格的附件引用一起失效——文件本体才算真的没了。
        if (categoryIds.contains(QString::fromLatin1(category_id::kEmbeddedFiles))) {
            try {
                QPDFEmbeddedFileDocumentHelper& embedded =
                    QPDFEmbeddedFileDocumentHelper::get(*pdf);
                std::vector<std::string> embeddedNames;
                for (const auto& entry : embedded.getEmbeddedFiles()) {
                    embeddedNames.push_back(entry.first);
                }
                for (const std::string& name : embeddedNames) {
                    embedded.removeEmbeddedFile(name);
                }
            } catch (const std::exception&) {
                // 名称树结构不合法时不能整个放弃：下面手工摘索引照样算数。
            }
            QPDFObjectHandle names = root.getKey("/Names");
            if (names.isDictionary()) {
                names.removeKey("/EmbeddedFiles");
            }
        }

        // file-attachments：删掉挂附件的批注与 catalog 的 /AF。
        //
        // 这里刻意不动 /Names /EmbeddedFiles：文档级嵌入文件是另一类，
        // 用户没勾它，附件面板里的文件就该留着。
        if (categoryIds.contains(QString::fromLatin1(category_id::kFileAttachments))) {
            root.removeKey("/AF");
            removeAnnotations(pages, isFileAttachmentAnnotation);
        }

        // actions：删掉自动动作。
        if (categoryIds.contains(QString::fromLatin1(category_id::kActions))) {
            root.removeKey("/OpenAction");
            root.removeKey("/AA");
            for (QPDFPageObjectHelper& page : pages) {
                QPDFObjectHandle pageDict = page.getObjectHandle();
                pageDict.removeKey("/AA");
            }
        }

        // annotation-actions：删掉批注上的 /AA。
        //
        // 只摘 /AA 这一个键，批注本身留着——批注里的正文是给人看的，不是隐藏数据。
        // 表单控件的 /AA 不动：那是字段级那一类，两类必须能分开清。
        if (categoryIds.contains(QString::fromLatin1(category_id::kAnnotationActions))) {
            for (QPDFPageObjectHelper& page : pages) {
                QPDFObjectHandle pageDict = page.getObjectHandle();
                const QPDFObjectHandle annots = pageDict.getKey("/Annots");
                if (!annots.isArray()) {
                    continue;
                }
                for (QPDFObjectHandle annot : annots.getArrayAsVector()) {
                    if (!annot.isDictionary() || isFormWidget(annot)) {
                        continue;
                    }
                    if (annot.hasKey("/AA")) {
                        annot.removeKey("/AA");
                    }
                }
            }
        }

        // field-actions：删掉表单域上的 /AA（校验、格式化脚本的挂点）。
        //
        // 字段字典与它的控件都要看：合并成同一个字典的字段，/AA 就在字段上；
        // 分开写的字段，/AA 可能挂在控件上。
        if (categoryIds.contains(QString::fromLatin1(category_id::kFieldActions))) {
            QVector<QPDFObjectHandle> fields;
            const QPDFObjectHandle acroForm = root.getKey("/AcroForm");
            if (acroForm.isDictionary()) {
                const QPDFObjectHandle fieldArray = acroForm.getKey("/Fields");
                if (fieldArray.isArray()) {
                    for (const QPDFObjectHandle& field : fieldArray.getArrayAsVector()) {
                        collectFormFields(field, fields);
                    }
                }
            }
            for (QPDFObjectHandle field : fields) {
                if (field.hasKey("/AA")) {
                    field.removeKey("/AA");
                }
                const QPDFObjectHandle kids = field.getKey("/Kids");
                if (kids.isArray()) {
                    for (QPDFObjectHandle kid : kids.getArrayAsVector()) {
                        if (kid.isDictionary() && kid.hasKey("/AA")) {
                            kid.removeKey("/AA");
                        }
                    }
                }
            }
        }

        // annotations：删掉隐藏批注与数据载体批注。
        if (categoryIds.contains(QString::fromLatin1(category_id::kAnnotations))) {
            removeAnnotations(pages, isHiddenAnnotation);
        }

        // thumbnails：删掉预览图引用。
        if (categoryIds.contains(QString::fromLatin1(category_id::kThumbnails))) {
            removeThumbnails(root, pages);
        }

        // layers：删掉图层配置与页面资源里的图层定义。
        if (categoryIds.contains(QString::fromLatin1(category_id::kLayers))) {
            removeLayers(root, pages);
        }

        // xfa：删掉 XFA 表单。
        if (categoryIds.contains(QString::fromLatin1(category_id::kXfa))) {
            QPDFObjectHandle acroForm = root.getKey("/AcroForm");
            if (acroForm.isDictionary()) {
                acroForm.removeKey("/XFA");
            }
        }

        // form-data：清掉表单字段里已经填过的值。
        if (categoryIds.contains(QString::fromLatin1(category_id::kFormData))) {
            clearFieldValues(root, false);
        }

        // signatures：清掉签名值。
        if (categoryIds.contains(QString::fromLatin1(category_id::kSignatures))) {
            clearFieldValues(root, true);
            QPDFObjectHandle acroForm = root.getKey("/AcroForm");
            if (acroForm.isDictionary()) {
                acroForm.removeKey("/SigFlags");
            }
        }

        // invisible-text：把不可见渲染模式下的文字从内容流里摘掉。
        //
        // 这一类只能改内容流本身，删别的键都删不掉它。
        if (categoryIds.contains(QString::fromLatin1(category_id::kInvisibleText))) {
            for (QPDFPageObjectHelper& page : pages) {
                page.addContentTokenFilter(std::make_shared<InvisibleTextScanner>(true));
            }
        }

        // incremental-history：不需要专门动作。
        //
        // 写出器本来就把历史拍平成一份：qpdf 只认最后一版修订，旧版本的对象
        // 压根不会进到输出字节里。选它等于「只把文件重写一遍」。

        QPDFWriter writer(*pdf);
        writer.setOutputMemory();
        writer.setObjectStreamMode(qpdf_o_generate);
        writer.setPreserveUnreferencedObjects(false);
        if (encrypted) {
            // 加密文档清理完**仍然加密**：沿用原来的密码与权限位。
            //
            // 清成明文等于替用户拆了锁——这是最不能出现的静默降级。
            // qpdf 默认就保留加密，这里显式写出来，免得以后有人改了写出器
            // 的模式（qdf / 内容归一化会关掉保留）就悄悄把锁拆了。
            writer.setPreserveEncryption(true);
        }
        writer.write();
        std::unique_ptr<Buffer> buffer(writer.getBuffer());
        if (!buffer || buffer->getSize() == 0) {
            if (error != nullptr) {
                *error = toLocal(QStringLiteral("qpdf 没有写出清理结果"));
            }
            return {};
        }
        const QByteArray output(reinterpret_cast<const char*>(buffer->getBuffer()),
                                static_cast<int>(buffer->getSize()));

        // 复验：勾了的类必须真的归零。
        //
        // 「声称清掉了、实际没清」是最坏的失败，所以写完立刻用同一套枚举再查一遍。
        // 加密文档要用同一个密码查：不然复验会栽在「打不开」上，
        // 看起来像清理失败，其实文件是好的。
        const QVector<HiddenDataCategory> after = enumerateHiddenData(output, nullptr, password);
        for (const HiddenDataCategory& category : after) {
            if (categoryIds.contains(category.id) && category.itemCount != 0) {
                if (error != nullptr) {
                    *error = toLocal(QStringLiteral("清理没有生效：%1 仍有 %2 项")
                                         .arg(category.id)
                                         .arg(category.itemCount));
                }
                return {};
            }
        }
        return output;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = toLocal(QStringLiteral("隐藏数据清理失败：%1")
                                 .arg(QString::fromUtf8(exception.what())));
        }
        return {};
    }
#else
    Q_UNUSED(password);
    if (error != nullptr) {
        *error = missingQpdfError();
    }
    return {};
#endif
}

QByteArray decryptPdf(const QByteArray& input, const QString& password, QString* error) {
    if (input.isEmpty()) {
        if (error != nullptr) {
            *error = toLocal(QStringLiteral("没有可解密的内容"));
        }
        return {};
    }
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        const QByteArray utf8 = password.toUtf8();
        pdf.processMemoryFile("decrypt.pdf", input.constData(),
                              static_cast<size_t>(input.size()),
                              utf8.isEmpty() ? nullptr : utf8.constData());
        if (!pdf.isEncrypted()) {
            // 本来就没加密：一个字节都不动。
            return input;
        }
        QPDFWriter writer(pdf);
        writer.setOutputMemory();
        // 关键：不保留加密。qpdf 默认会沿用原来的加密参数，那样解不开。
        writer.setPreserveEncryption(false);
        writer.write();
        std::unique_ptr<Buffer> buffer(writer.getBuffer());
        if (!buffer || buffer->getSize() == 0) {
            if (error != nullptr) {
                *error = toLocal(QStringLiteral("qpdf 没有写出解密结果"));
            }
            return {};
        }
        return QByteArray(reinterpret_cast<const char*>(buffer->getBuffer()),
                          static_cast<int>(buffer->getSize()));
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = toLocal(QStringLiteral("解密失败：%1")
                                 .arg(QString::fromUtf8(exception.what())));
        }
        return {};
    }
#else
    Q_UNUSED(password);
    if (error != nullptr) {
        *error = missingQpdfError();
    }
    return {};
#endif
}

bool verifyOwnerPassword(const QByteArray& input, const QString& password, QString* error) {
    if (input.isEmpty() || password.isEmpty()) {
        if (error != nullptr) *error = toLocal(QStringLiteral("所有者密码不能为空"));
        return false;
    }
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        const QByteArray utf8 = password.toUtf8();
        pdf.processMemoryFile("owner-check.pdf", input.constData(),
                              static_cast<size_t>(input.size()), utf8.constData());
        if (!pdf.isEncrypted()) {
            if (error != nullptr) *error = toLocal(QStringLiteral("文档没有加密"));
            return false;
        }
        if (!pdf.ownerPasswordMatched()) {
            if (error != nullptr) *error = toLocal(QStringLiteral("密码可以打开文档，但不是所有者密码"));
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) *error = toLocal(QString::fromUtf8(exception.what()));
        return false;
    }
#else
    Q_UNUSED(input);
    Q_UNUSED(password);
    if (error != nullptr) *error = missingQpdfError();
    return false;
#endif
}

bool inspectEncryption(const QByteArray& input,
                       const QString& password,
                       int* revision,
                       int* permissionBits,
                       QString* error) {
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        const QByteArray utf8Password = password.toUtf8();
        pdf.processMemoryFile("inspect.pdf",
                              input.constData(),
                              static_cast<size_t>(input.size()),
                              utf8Password.constData());
        int localRevision = 0;
        int localPermissions = 0;
        if (!pdf.isEncrypted(localRevision, localPermissions)) {
            if (revision != nullptr) {
                *revision = 0;
            }
            if (permissionBits != nullptr) {
                *permissionBits = 0;
            }
            return false;
        }
        if (revision != nullptr) {
            *revision = localRevision;
        }
        if (permissionBits != nullptr) {
            *permissionBits = localPermissions;
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = toLocal(QString::fromUtf8(exception.what()));
        }
        return false;
    }
#else
    Q_UNUSED(input);
    Q_UNUSED(password);
    Q_UNUSED(revision);
    Q_UNUSED(permissionBits);
    if (error != nullptr) {
        *error = missingQpdfError();
    }
    return false;
#endif
}

}  // namespace aipdf::pdf
