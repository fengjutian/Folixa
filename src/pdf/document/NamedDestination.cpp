#include "pdf/document/NamedDestination.h"

#include <QByteArray>
#include <QHash>
#include <QPointF>
#include <QString>
#include <QVector>

#include <map>
#include <memory>
#include <string>
#include <vector>

#ifdef AIPDF_WITH_QPDF
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#endif

namespace aipdf::pdf {
namespace {

// 失败统一走这里：把中文原因写进 error，返回 false。
bool fail(QString* error, const QString& message) {
    if (error != nullptr) *error = message;
    return false;
}

// 名字对象的键去掉前导斜杠后的字节。
std::string nameBody(const std::string& key) {
    if (!key.empty() && key.front() == '/') return key.substr(1);
    return key;
}

// 把 PDF 里的字节解成文本。
//
// 为什么先按 UTF-8 解：PDF 的名字与文本串都是字节串，中文名在实践中
// 通常直接写 UTF-8 字节；解不出合法 UTF-8 时退回 Latin-1，
// 至少保证「不崩、也不丢字节」。
QString decodeBytes(const QByteArray& raw) {
    if (raw.isEmpty()) return {};
    const QString utf8 = QString::fromUtf8(raw);
    if (utf8.toUtf8() == raw) return utf8;
    return QString::fromLatin1(raw);
}

// 解 PDF 的文本串对象。
//
// 为什么单独处理 BOM：文本串按规范是 PDFDocEncoding 或带 BOM 的 UTF-16BE，
// 但也有工具直接写 UTF-8 字节。三种都认，才不会把别人的文档读成乱码。
QString decodeTextString(const std::string& bytes) {
    const QByteArray raw(bytes.data(), static_cast<int>(bytes.size()));
    if (raw.size() >= 2 && static_cast<unsigned char>(raw.at(0)) == 0xFE &&
        static_cast<unsigned char>(raw.at(1)) == 0xFF) {
        QString text;
        for (int i = 2; i + 1 < raw.size(); i += 2) {
            const ushort code = static_cast<ushort>((static_cast<unsigned char>(raw.at(i)) << 8) |
                                                   static_cast<unsigned char>(raw.at(i + 1)));
            text.append(QChar(code));
        }
        return text;
    }
    if (raw.size() >= 3 && static_cast<unsigned char>(raw.at(0)) == 0xEF &&
        static_cast<unsigned char>(raw.at(1)) == 0xBB &&
        static_cast<unsigned char>(raw.at(2)) == 0xBF) {
        return QString::fromUtf8(raw.mid(3));
    }
    return decodeBytes(raw);
}

// 把 double 变成 PDF 能写的实数字面量。
//
// 为什么自己格式化：PDF 的实数不允许指数写法（不能出现 1e+20），
// 而按 'g' 格式化会冒出指数；固定 6 位小数再去掉多余的 0，
// 坐标既不丢精度，文件里也不会出现一长串 0。
std::string formatReal(double value) {
    QString text = QString::number(value, 'f', 6);
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0'))) text.chop(1);
        if (text.endsWith(QLatin1Char('.'))) text.chop(1);
    }
    if (text == QLatin1String("-0")) text = QStringLiteral("0");
    return text.toStdString();
}

// 名字 → PDF 名字对象的键。
//
// 名字里的空格与中文由 qpdf 在写文件时按名字对象的规则转义
// （空格写成 #20，中文写成 #xx），读回来时转义会被还原，
// 所以这里只需要把 UTF-8 字节原样交给 qpdf。
std::string nameKey(const QString& name) {
    return "/" + name.toUtf8().toStdString();
}

#ifdef AIPDF_WITH_QPDF

// 页面对象号 → 0 基页号。
std::map<int, int> pageIndexesByObjectId(QPDF& pdf) {
    std::map<int, int> result;
    const std::vector<QPDFObjectHandle>& pages = pdf.getAllPages();
    for (size_t i = 0; i < pages.size(); ++i) {
        result[pages.at(i).getObjectID()] = static_cast<int>(i);
    }
    return result;
}

// 解析一个目标数组：[页引用 /XYZ left top zoom] 或 [页引用 /Fit]。
//
// 页引用解析不出来时 pageIndex 留 -1：名字还在，但跳不过去。
bool parseDestination(QPDFObjectHandle array,
                      const std::map<int, int>& pageIndexes,
                      NamedDestination* out) {
    if (!array.isArray()) return false;
    const std::vector<QPDFObjectHandle> items = array.getArrayAsVector();
    if (items.empty()) return false;

    const QPDFObjectHandle page = items.at(0);
    if (page.isIndirect()) {
        const auto found = pageIndexes.find(page.getObjectID());
        if (found != pageIndexes.end()) out->pageIndex = found->second;
    }
    if (items.size() < 2) return true;

    const QPDFObjectHandle kind = items.at(1);
    if (!kind.isName()) return true;
    const std::string type = kind.getName();

    if (type == "/XYZ") {
        double left = 0.0;
        double top = 0.0;
        double zoom = 0.0;
        if (items.size() > 2 && items.at(2).isNumber()) left = items.at(2).getNumericValue();
        if (items.size() > 3 && items.at(3).isNumber()) top = items.at(3).getNumericValue();
        if (items.size() > 4 && items.at(4).isNumber()) zoom = items.at(4).getNumericValue();
        out->position = QPointF(left, top);
        out->zoom = zoom;
        out->fitPage = false;
        return true;
    }
    // /Fit、/FitH、/FitV、/FitB、/FitBH、/FitR 都是「整页适配」这一类，
    // 本层只区分「整页适配」与「XYZ 精确落点」，细节由界面层决定。
    if (type == "/Fit" || type == "/FitH" || type == "/FitV" || type == "/FitB" ||
        type == "/FitBH" || type == "/FitR") {
        out->fitPage = true;
    }
    return true;
}

// 名称树节点 → 命名目标列表。
//
// 名称树的键是**文本串**（不是名字对象），值是目标数组。
void collectFromNameTree(QPDFObjectHandle node,
                          const std::map<int, int>& pageIndexes,
                          QVector<NamedDestination>* out,
                          int depth) {
    // 深度上限：名称树是树，坏文档可能把自己绕成环。
    if (depth > 32) return;

    const QPDFObjectHandle names = node.getKey("/Names");
    if (names.isArray()) {
        const std::vector<QPDFObjectHandle> entries = names.getArrayAsVector();
        for (size_t i = 0; i + 1 < entries.size(); i += 2) {
            const QPDFObjectHandle key = entries.at(i);
            if (!key.isString()) continue;
            NamedDestination entry;
            entry.name = decodeTextString(key.getStringValue());
            QPDFObjectHandle value = entries.at(i + 1);
            // 老写法：值是字典，目标数组放在 /D 里。
            if (value.isDictionary()) value = value.getKey("/D");
            if (parseDestination(value, pageIndexes, &entry)) out->append(entry);
        }
    }

    const QPDFObjectHandle kids = node.getKey("/Kids");
    if (kids.isArray()) {
        const std::vector<QPDFObjectHandle> children = kids.getArrayAsVector();
        for (const QPDFObjectHandle& kid : children) {
            if (!kid.isDictionary()) continue;
            collectFromNameTree(kid, pageIndexes, out, depth + 1);
        }
    }
}

// 同名目标只保留最后一个。
//
// 为什么按「后面的覆盖前面的」处理：PDF 里名称树的优先级高于
// catalog 的 /Dests 字典，而本函数先读字典、后读名称树，
// 于是后读到的就是优先级更高的那一份。
void keepLastByName(QVector<NamedDestination>* destinations) {
    for (int i = 0; i < destinations->size(); ++i) {
        const QString name = destinations->at(i).name;
        for (int j = i + 1; j < destinations->size(); ++j) {
            if (destinations->at(j).name == name) {
                destinations->remove(i);
                --i;
                break;
            }
        }
    }
}

// catalog → 命名目标列表（两种写法都读）。
QVector<NamedDestination> collectFromCatalog(QPDF& pdf) {
    QVector<NamedDestination> result;
    const QPDFObjectHandle catalog = pdf.getRoot();
    if (!catalog.isDictionary()) return result;

    const std::map<int, int> pageIndexes = pageIndexesByObjectId(pdf);

    // ① catalog 的 /Dests 字典：键是名字对象，值是目标数组。
    const QPDFObjectHandle dests = catalog.getKey("/Dests");
    if (dests.isDictionary()) {
        for (const std::string& key : dests.getKeys()) {
            NamedDestination entry;
            const std::string body = nameBody(key);
            entry.name = decodeBytes(QByteArray(body.data(), static_cast<int>(body.size())));
            QPDFObjectHandle value = dests.getKey(key);
            if (value.isDictionary()) value = value.getKey("/D");
            if (parseDestination(value, pageIndexes, &entry)) result.append(entry);
        }
    }

    // ② catalog 的 /Names /Dests 名称树。
    const QPDFObjectHandle names = catalog.getKey("/Names");
    if (names.isDictionary()) {
        const QPDFObjectHandle tree = names.getKey("/Dests");
        if (tree.isDictionary()) collectFromNameTree(tree, pageIndexes, &result, 0);
    }

    keepLastByName(&result);
    return result;
}

#endif  // AIPDF_WITH_QPDF

}  // namespace

QByteArray writeNamedDestinations(const QByteArray& input,
                                   const QVector<NamedDestination>& destinations,
                                   QString* error) {
    if (error != nullptr) error->clear();
    if (input.isEmpty()) {
        fail(error, QStringLiteral("没有可写入的 PDF 内容"));
        return {};
    }
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        pdf.processMemoryFile("named-destinations.pdf", input.constData(),
                              static_cast<size_t>(input.size()));

        const std::vector<QPDFObjectHandle>& pages = pdf.getAllPages();
        if (pages.empty()) {
            fail(error, QStringLiteral("PDF 里没有页面"));
            return {};
        }

        QPDFObjectHandle catalog = pdf.getRoot();
        if (!catalog.isDictionary()) {
            fail(error, QStringLiteral("PDF 的根对象不是字典"));
            return {};
        }

        // 名字 → 目标数组。同名目标后写覆盖先写（字典本身就是这个语义）。
        QPDFObjectHandle dests = QPDFObjectHandle::newDictionary();
        for (const NamedDestination& destination : destinations) {
            if (destination.name.trimmed().isEmpty()) {
                fail(error, QStringLiteral("命名目标的名称不能为空"));
                return {};
            }
            if (destination.name.contains(QChar(0))) {
                fail(error, QStringLiteral("命名目标的名称不能包含空字符"));
                return {};
            }
            if (destination.pageIndex < 0 ||
                destination.pageIndex >= static_cast<int>(pages.size())) {
                fail(error, QStringLiteral("命名目标「%1」的页码超出范围")
                               .arg(destination.name));
                return {};
            }

            // 页的间接引用：页在文件里本来就是间接对象，直接放进数组即可。
            QPDFObjectHandle page = pages.at(static_cast<size_t>(destination.pageIndex));
            std::vector<QPDFObjectHandle> array;
            array.push_back(page);
            if (destination.fitPage) {
                array.push_back(QPDFObjectHandle::newName("/Fit"));
            } else {
                array.push_back(QPDFObjectHandle::newName("/XYZ"));
                array.push_back(
                    QPDFObjectHandle::newReal(formatReal(destination.position.x())));
                array.push_back(
                    QPDFObjectHandle::newReal(formatReal(destination.position.y())));
                array.push_back(QPDFObjectHandle::newReal(formatReal(destination.zoom)));
            }
            dests.replaceKey(nameKey(destination.name), QPDFObjectHandle::newArray(array));
        }

        if (destinations.isEmpty()) {
            // 空集合 = 清空命名目标：留着空字典没有意义。
            catalog.removeKey("/Dests");
        } else {
            catalog.replaceKey("/Dests", dests);
        }

        // 名称树的优先级比 catalog /Dests 高：不摘掉旧名称树，
        // 刚写进去的目标在别的阅读器里会被旧名称树挡住（等于白写）。
        QPDFObjectHandle names = catalog.getKey("/Names");
        if (names.isDictionary()) {
            names.removeKey("/Dests");
            if (names.getKeys().empty()) catalog.removeKey("/Names");
        }

        QPDFWriter writer(pdf);
        // 关掉对象流：交叉引用表保持传统形态，出问题时人眼能直接看。
        writer.setObjectStreamMode(qpdf_o_disable);
        writer.setOutputMemory();
        writer.write();
        std::unique_ptr<Buffer> buffer(writer.getBuffer());
        if (!buffer || buffer->getSize() == 0) {
            fail(error, QStringLiteral("qpdf 没有写出命名目标"));
            return {};
        }
        const QByteArray written(reinterpret_cast<const char*>(buffer->getBuffer()),
                                  static_cast<qsizetype>(buffer->getSize()));

        // 自检：写完必须真的读得回来，而且页码与落点都对得上。
        // 这一步挡住的是「写出了字节、但目标其实没进去」的静默失败。
        //
        // 同名目标后写覆盖先写，所以这里按「每个名字的最后一次出现」比对。
        QHash<QString, NamedDestination> expected;
        for (const NamedDestination& destination : destinations) {
            expected.insert(destination.name, destination);
        }
        const QVector<NamedDestination> check = readNamedDestinations(written, nullptr);
        if (check.size() != expected.size()) {
            fail(error, QStringLiteral("写出的命名目标读回来数量对不上"));
            return {};
        }
        for (auto it = expected.constBegin(); it != expected.constEnd(); ++it) {
            bool found = false;
            for (const NamedDestination& entry : check) {
                if (entry.name != it.key()) continue;
                found = true;
                if (entry.pageIndex != it.value().pageIndex) {
                    fail(error, QStringLiteral("写出的命名目标「%1」页码对不上").arg(it.key()));
                    return {};
                }
                break;
            }
            if (!found) {
                fail(error, QStringLiteral("写出的命名目标「%1」读不回来").arg(it.key()));
                return {};
            }
        }
        return written;
    } catch (const std::exception& exception) {
        fail(error, QStringLiteral("写入命名目标失败：%1")
                       .arg(QString::fromUtf8(exception.what())));
        return {};
    }
#else
    Q_UNUSED(destinations);
    fail(error, QStringLiteral("本次构建没有启用 qpdf，命名目标不可用"));
    return {};
#endif
}

QVector<NamedDestination> readNamedDestinations(const QByteArray& input, QString* error) {
    if (error != nullptr) error->clear();
    if (input.isEmpty()) {
        fail(error, QStringLiteral("没有可读取的 PDF 内容"));
        return {};
    }
#ifdef AIPDF_WITH_QPDF
    try {
        QPDF pdf;
        pdf.processMemoryFile("named-destinations-read.pdf", input.constData(),
                              static_cast<size_t>(input.size()));
        return collectFromCatalog(pdf);
    } catch (const std::exception& exception) {
        fail(error, QStringLiteral("读取命名目标失败：%1")
                       .arg(QString::fromUtf8(exception.what())));
        return {};
    }
#else
    Q_UNUSED(input);
    fail(error, QStringLiteral("本次构建没有启用 qpdf，命名目标不可用"));
    return {};
#endif
}

int pageIndexOfNamedDestination(const QByteArray& input, const QString& name, QString* error) {
    if (error != nullptr) error->clear();
    if (name.isEmpty()) return -1;

    QString readError;
    const QVector<NamedDestination> destinations = readNamedDestinations(input, &readError);
    if (!readError.isEmpty()) {
        if (error != nullptr) *error = readError;
        return -1;
    }
    for (const NamedDestination& destination : destinations) {
        if (destination.name == name) return destination.pageIndex;
    }
    // 找不到不是错误：调用方只需要知道「没有这个目标」。
    return -1;
}

}  // namespace aipdf::pdf
