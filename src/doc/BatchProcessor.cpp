#include "doc/BatchProcessor.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace aipdf::doc {

namespace {

constexpr auto kKeyVersion = "version";
constexpr auto kKeyItems = "items";
constexpr auto kKeyTotalDurationMs = "totalDurationMs";
constexpr auto kKeySucceededCount = "succeededCount";
constexpr auto kKeyFailedCount = "failedCount";
constexpr auto kKeySkippedCount = "skippedCount";
constexpr auto kKeyAborted = "aborted";
constexpr auto kKeyInputPath = "inputPath";
constexpr auto kKeyOutputPath = "outputPath";
constexpr auto kKeySucceeded = "succeeded";
constexpr auto kKeySkipped = "skipped";
constexpr auto kKeyDurationMs = "durationMs";
constexpr auto kKeyError = "error";
constexpr auto kKeyInputBytes = "inputBytes";
constexpr auto kKeyOutputBytes = "outputBytes";
constexpr auto kKeySteps = "steps";
constexpr auto kKeyStepName = "name";

constexpr int kFormatVersion = 1;

void setError(QString* error, const QString& message) {
    if (error != nullptr) {
        *error = message;
    }
}

// 取一个字符串字段：缺失或类型不对都要报错，不给默认值蒙混过去。
bool readStringField(const QJsonObject& object,
                     const QString& key,
                     QString* value,
                     QString* error) {
    const QJsonValue jsonValue = object.value(key);
    if (jsonValue.isUndefined() || jsonValue.isNull()) {
        setError(error, QStringLiteral("JSON 缺少字段：%1").arg(key));
        return false;
    }
    if (!jsonValue.isString()) {
        setError(error, QStringLiteral("JSON 字段类型不对（应为字符串）：%1").arg(key));
        return false;
    }
    *value = jsonValue.toString();
    return true;
}

bool readBoolField(const QJsonObject& object,
                   const QString& key,
                   bool* value,
                   QString* error) {
    const QJsonValue jsonValue = object.value(key);
    if (jsonValue.isUndefined() || jsonValue.isNull()) {
        setError(error, QStringLiteral("JSON 缺少字段：%1").arg(key));
        return false;
    }
    if (!jsonValue.isBool()) {
        setError(error, QStringLiteral("JSON 字段类型不对（应为布尔）：%1").arg(key));
        return false;
    }
    *value = jsonValue.toBool();
    return true;
}

// 整数一律走 JSON number：JSON 没有整数类型，读的时候要自己确认它是整数。
bool readIntField(const QJsonObject& object,
                  const QString& key,
                  qint64* value,
                  QString* error) {
    const QJsonValue jsonValue = object.value(key);
    if (jsonValue.isUndefined() || jsonValue.isNull()) {
        setError(error, QStringLiteral("JSON 缺少字段：%1").arg(key));
        return false;
    }
    if (!jsonValue.isDouble()) {
        setError(error, QStringLiteral("JSON 字段类型不对（应为数字）：%1").arg(key));
        return false;
    }
    const double raw = jsonValue.toDouble();
    if (raw < -9.0e15 || raw > 9.0e15) {
        setError(error, QStringLiteral("JSON 字段数值超出范围：%1").arg(key));
        return false;
    }
    *value = static_cast<qint64>(raw);
    return true;
}

// 计数由条目推导，保证报告里的数字和列表永远对得上。
void recount(BatchReport& report) {
    report.succeededCount = 0;
    report.failedCount = 0;
    report.skippedCount = 0;
    for (const BatchItemResult& item : report.items) {
        if (item.succeeded) {
            ++report.succeededCount;
        } else if (item.skipped) {
            ++report.skippedCount;
        } else {
            ++report.failedCount;
        }
    }
}

// 决定某个条目的产出路径。
//
// 为什么输出目录为空时也要算路径：报告里要记「本来会写到哪」，
// 只是空目录表示不落盘而已。
QString resolveOutputPath(const QString& inputPath,
                          const QStringList& outputPaths,
                          int index,
                          const QString& outputDirectory) {
    QString name;
    if (index >= 0 && index < outputPaths.size()) {
        name = outputPaths.at(index);
    }
    if (name.isEmpty()) {
        const QFileInfo info(inputPath);
        name = info.fileName();
        if (name.isEmpty()) {
            name = QStringLiteral("output_%1.bin").arg(index + 1);
        }
    }
    // 绝对路径按它自己来：调用方可能想把产物放到别的地方。
    if (QFileInfo(name).isAbsolute()) {
        return name;
    }
    return QDir(outputDirectory).filePath(name);
}

// 把 Qt 的 4 空格缩进换成 2 空格。
//
// 为什么非要 2 空格：这份报告是给人看的，常常直接贴到 issue 或工单里；
// Qt 默认的 4 空格嵌两层以后行就拉得很长，2 空格更紧凑。
QByteArray reindentToTwoSpaces(const QByteArray& indented) {
    QByteArray result;
    result.reserve(indented.size());
    const QList<QByteArray> lines = indented.split('\n');
    for (int index = 0; index < lines.size(); ++index) {
        if (index > 0) {
            result.append('\n');
        }
        const QByteArray& line = lines.at(index);
        int spaces = 0;
        while (spaces < line.size() && line.at(spaces) == ' ') {
            ++spaces;
        }
        result.append(QByteArray(spaces / 2, ' '));
        result.append(line.mid(spaces));
    }
    return result;
}

}  // namespace

BatchReport runBatch(const QStringList& inputPaths,
                     const QStringList& outputPaths,
                     const QVector<BatchStep>& steps,
                     bool continueOnError,
                     const QString& outputDirectory) {
    BatchReport report;
    QElapsedTimer batchTimer;
    batchTimer.start();

    // 整批都没有可执行的步骤时，什么都不用做：
    // 与其读一遍每个文件再原样写回去（几百兆白读白写），不如直接记成跳过。
    bool hasExecutableStep = false;
    for (const BatchStep& step : steps) {
        if (step.transform) {
            hasExecutableStep = true;
            break;
        }
    }

    const bool writeOutput = !outputDirectory.isEmpty();

    for (int index = 0; index < inputPaths.size(); ++index) {
        const QString inputPath = inputPaths.at(index);

        BatchItemResult item;
        item.inputPath = inputPath;

        QElapsedTimer itemTimer;
        itemTimer.start();

        // 收尾动作：登记条目，并根据 continueOnError 决定要不要停下整批。
        const auto finish = [&](bool stopBatch) {
            item.durationMs = itemTimer.elapsed();
            report.items.append(item);
            if (stopBatch) {
                report.aborted = true;
            }
        };

        if (!hasExecutableStep) {
            item.skipped = true;
            const QFileInfo info(inputPath);
            if (info.exists() && info.isFile()) {
                item.inputBytes = info.size();
            }
            finish(false);
            continue;
        }

        QFile inputFile(inputPath);
        if (!inputFile.open(QIODevice::ReadOnly)) {
            item.error = QStringLiteral("读取输入文件失败：%1").arg(inputPath);
            finish(!continueOnError);
            if (!continueOnError) {
                break;
            }
            continue;
        }
        const QByteArray input = inputFile.readAll();
        inputFile.close();
        item.inputBytes = input.size();

        // 依次跑每一步：某一步失败就停在这个文件上，后面的步骤不再执行。
        QByteArray current = input;
        bool failed = false;
        for (const BatchStep& step : steps) {
            if (!step.transform) {
                // 空变换 = 这一步跳过，字节原样往下传。
                continue;
            }
            QString stepError;
            const QByteArray produced = step.transform(current, &stepError);
            if (!stepError.isEmpty()) {
                failed = true;
                item.error = QStringLiteral("步骤「%1」失败：%2").arg(step.name, stepError);
                break;
            }
            // 每一步的进出字节数都记下来：报告要能回答「是哪一步让文件变大的」。
            BatchStepBytes stepBytes;
            stepBytes.name = step.name;
            stepBytes.inputBytes = current.size();
            stepBytes.outputBytes = produced.size();
            item.stepBytes.append(stepBytes);
            current = produced;
        }

        if (failed) {
            finish(!continueOnError);
            if (!continueOnError) {
                break;
            }
            continue;
        }

        // 变换产出的字节数先记上：即使不落盘，报告也要说清「这一步产出多大」，
        // 否则「只跑变换」的模式下报告里全是 0，看不出任何东西。
        item.outputBytes = current.size();

        if (writeOutput) {
            const QString outputPath =
                resolveOutputPath(inputPath, outputPaths, index, outputDirectory);
            const QDir parentDirectory = QFileInfo(outputPath).absoluteDir();
            if (!parentDirectory.exists() && !QDir().mkpath(parentDirectory.absolutePath())) {
                item.error = QStringLiteral("创建输出目录失败：%1").arg(parentDirectory.absolutePath());
                finish(!continueOnError);
                if (!continueOnError) {
                    break;
                }
                continue;
            }
            QFile outputFile(outputPath);
            if (!outputFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                item.error = QStringLiteral("写入输出文件失败：%1").arg(outputPath);
                finish(!continueOnError);
                if (!continueOnError) {
                    break;
                }
                continue;
            }
            const bool written = outputFile.write(current) == current.size();
            outputFile.close();
            if (!written) {
                item.error = QStringLiteral("写入输出文件失败：%1").arg(outputPath);
                finish(!continueOnError);
                if (!continueOnError) {
                    break;
                }
                continue;
            }
            item.outputPath = outputPath;
        }

        item.succeeded = true;
        finish(false);
    }

    report.totalDurationMs = batchTimer.elapsed();
    recount(report);
    return report;
}

pdf::PdfEncryptionOptions defaultEncryptOptions(const QString& userPassword) {
    // 权限位用 pdf::PdfPermissions 的默认值：与「加密与权限」对话框一致，
    // 这里不另发明一套权限语义。
    pdf::PdfEncryptionOptions options;
    options.userPassword = userPassword;
    return options;
}

BatchStep makeEncryptStep(const QString& stepName, const pdf::PdfEncryptionOptions& options,
                          const QString& sourcePassword) {
    BatchStep step;
    step.name = stepName;
    step.transform = [options, sourcePassword](const QByteArray& input, QString* error) -> QByteArray {
        if (input.isEmpty()) {
            if (error != nullptr) {
                *error = QStringLiteral("没有可加密的内容");
            }
            return {};
        }
        if (options.userPassword.isEmpty()) {
            // 空密码的文件任何人都能直接打开，那不叫加密。
            // 这里明确失败，不产出「看起来加密了、其实没设防」的文件。
            if (error != nullptr) {
                *error = QStringLiteral("加密步骤需要设置打开密码：没有密码的文件谁都能直接打开");
            }
            return {};
        }
#ifdef AIPDF_WITH_QPDF
        // 源文件本身已加密：先解开旧的，才能按新设置加密。
        // 直接对加密字节调用 encryptPdf() 只会得到一句「invalid password」。
        QByteArray plain = input;
        if (pdf::requiresPasswordToOpen(input, nullptr)) {
            QString decryptError;
            plain = pdf::decryptPdf(input, sourcePassword, &decryptError);
            if (plain.isEmpty()) {
                if (error != nullptr) {
                    *error = decryptError.isEmpty()
                                 ? QStringLiteral("源文件已加密，但没有它的打开密码，无法重新加密")
                                 : decryptError;
                }
                return {};
            }
        }
        QString encryptError;
        const QByteArray encrypted = pdf::encryptPdf(plain, options, &encryptError);
        if (encrypted.isEmpty()) {
            if (error != nullptr) {
                *error = encryptError.isEmpty() ? QStringLiteral("加密失败") : encryptError;
            }
            return {};
        }

        // 读回自检 1：必须能用打开密码重新打开，且确实是 AES-256（R6）。
        int revision = 0;
        int permissionBits = 0;
        QString inspectError;
        if (!pdf::inspectEncryption(encrypted, options.userPassword, &revision, &permissionBits,
                                    &inspectError)
            || revision < 6) {
            if (error != nullptr) {
                *error = QStringLiteral("加密自检失败：写出的文件不能用打开密码打开，或不是 AES-256 加密");
            }
            return {};
        }

        // 读回自检 2：打开密码非空时，文件必须真的「要密码」——
        // 错密码能打开，就等于没加密。
        QString wrongPasswordError;
        if (pdf::inspectEncryption(encrypted, options.userPassword + QStringLiteral("-不对的密码"),
                                   nullptr, nullptr, &wrongPasswordError)) {
            if (error != nullptr) {
                *error = QStringLiteral("加密自检失败：写出的文件用错误的密码也能打开");
            }
            return {};
        }
        return encrypted;
#else
        if (error != nullptr) {
            *error = QStringLiteral("本次构建没有启用 qpdf，无法加密");
        }
        return {};
#endif
    };
    return step;
}

QByteArray BatchReport::toJson() const {
    QJsonArray itemArray;
    for (const BatchItemResult& item : items) {
        QJsonObject object;
        object.insert(QString::fromLatin1(kKeyInputPath), item.inputPath);
        object.insert(QString::fromLatin1(kKeyOutputPath), item.outputPath);
        object.insert(QString::fromLatin1(kKeySucceeded), item.succeeded);
        object.insert(QString::fromLatin1(kKeySkipped), item.skipped);
        object.insert(QString::fromLatin1(kKeyDurationMs), static_cast<double>(item.durationMs));
        object.insert(QString::fromLatin1(kKeyError), item.error);
        object.insert(QString::fromLatin1(kKeyInputBytes), static_cast<double>(item.inputBytes));
        object.insert(QString::fromLatin1(kKeyOutputBytes), static_cast<double>(item.outputBytes));
        QJsonArray stepArray;
        for (const BatchStepBytes& stepBytes : item.stepBytes) {
            QJsonObject stepObject;
            stepObject.insert(QString::fromLatin1(kKeyStepName), stepBytes.name);
            stepObject.insert(QString::fromLatin1(kKeyInputBytes),
                              static_cast<double>(stepBytes.inputBytes));
            stepObject.insert(QString::fromLatin1(kKeyOutputBytes),
                              static_cast<double>(stepBytes.outputBytes));
            stepArray.append(stepObject);
        }
        object.insert(QString::fromLatin1(kKeySteps), stepArray);
        itemArray.append(object);
    }

    QJsonObject root;
    root.insert(QString::fromLatin1(kKeyVersion), kFormatVersion);
    root.insert(QString::fromLatin1(kKeyItems), itemArray);
    root.insert(QString::fromLatin1(kKeyTotalDurationMs), static_cast<double>(totalDurationMs));
    root.insert(QString::fromLatin1(kKeySucceededCount), succeededCount);
    root.insert(QString::fromLatin1(kKeyFailedCount), failedCount);
    root.insert(QString::fromLatin1(kKeySkippedCount), skippedCount);
    root.insert(QString::fromLatin1(kKeyAborted), aborted);

    // Indented：给人看的报告，缩进两格；Qt 默认不会把中文转义成 \uXXXX。
    return reindentToTwoSpaces(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool BatchReport::fromJson(const QByteArray& json, BatchReport* report, QString* error) {
    if (report == nullptr) {
        setError(error, QStringLiteral("报告输出参数为空"));
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setError(error, QStringLiteral("JSON 解析失败：%1").arg(parseError.errorString()));
        return false;
    }
    if (!document.isObject()) {
        setError(error, QStringLiteral("JSON 顶层不是对象"));
        return false;
    }

    const QJsonObject root = document.object();

    const QJsonValue itemsValue = root.value(QString::fromLatin1(kKeyItems));
    if (itemsValue.isUndefined() || itemsValue.isNull()) {
        setError(error, QStringLiteral("JSON 缺少字段：items"));
        return false;
    }
    if (!itemsValue.isArray()) {
        setError(error, QStringLiteral("JSON 字段类型不对（应为数组）：items"));
        return false;
    }

    // 先解到临时对象上，全部校验通过再交给调用方：
    // 半路失败时不能把半份报告留在调用方手里。
    BatchReport parsed;
    const QJsonArray itemArray = itemsValue.toArray();
    for (int index = 0; index < itemArray.size(); ++index) {
        const QJsonValue itemValue = itemArray.at(index);
        if (!itemValue.isObject()) {
            setError(error, QStringLiteral("JSON 条目类型不对（应为对象）：第 %1 条").arg(index + 1));
            return false;
        }
        const QJsonObject itemObject = itemValue.toObject();

        BatchItemResult item;
        if (!readStringField(itemObject, QString::fromLatin1(kKeyInputPath), &item.inputPath, error)) {
            return false;
        }
        if (!readStringField(itemObject, QString::fromLatin1(kKeyOutputPath), &item.outputPath, error)) {
            return false;
        }
        if (!readBoolField(itemObject, QString::fromLatin1(kKeySucceeded), &item.succeeded, error)) {
            return false;
        }
        if (!readBoolField(itemObject, QString::fromLatin1(kKeySkipped), &item.skipped, error)) {
            return false;
        }
        if (!readIntField(itemObject, QString::fromLatin1(kKeyDurationMs), &item.durationMs, error)) {
            return false;
        }
        if (!readStringField(itemObject, QString::fromLatin1(kKeyError), &item.error, error)) {
            return false;
        }
        if (!readIntField(itemObject, QString::fromLatin1(kKeyInputBytes), &item.inputBytes, error)) {
            return false;
        }
        if (!readIntField(itemObject, QString::fromLatin1(kKeyOutputBytes), &item.outputBytes, error)) {
            return false;
        }
        // 「steps」是可选的：旧版本的报告没有这一节，读回来时留空即可。
        // 但只要写了，就必须写对——字段缺失或类型不对都要报错，
        // 不能把一份坏掉的步骤明细当成「没有步骤」蒙混过去。
        if (itemObject.contains(QString::fromLatin1(kKeySteps))) {
            const QJsonValue stepsValue = itemObject.value(QString::fromLatin1(kKeySteps));
            if (!stepsValue.isArray()) {
                setError(error, QStringLiteral("JSON 字段类型不对（应为数组）：steps"));
                return false;
            }
            const QJsonArray stepArray = stepsValue.toArray();
            for (int stepIndex = 0; stepIndex < stepArray.size(); ++stepIndex) {
                const QJsonValue stepValue = stepArray.at(stepIndex);
                if (!stepValue.isObject()) {
                    setError(error, QStringLiteral("JSON 步骤类型不对（应为对象）：第 %1 步")
                                       .arg(stepIndex + 1));
                    return false;
                }
                const QJsonObject stepObject = stepValue.toObject();
                BatchStepBytes stepBytes;
                if (!readStringField(stepObject, QString::fromLatin1(kKeyStepName),
                                     &stepBytes.name, error)) {
                    return false;
                }
                if (!readIntField(stepObject, QString::fromLatin1(kKeyInputBytes),
                                  &stepBytes.inputBytes, error)) {
                    return false;
                }
                if (!readIntField(stepObject, QString::fromLatin1(kKeyOutputBytes),
                                  &stepBytes.outputBytes, error)) {
                    return false;
                }
                item.stepBytes.append(stepBytes);
            }
        }
        parsed.items.append(item);
    }

    if (!readIntField(root, QString::fromLatin1(kKeyTotalDurationMs), &parsed.totalDurationMs, error)) {
        return false;
    }

    // 计数允许缺省（外部脚本手写的报告可能不带），但带了就得是数字；
    // 最终值一律由条目重算，避免出现「成功 8 个、列表里却有 9 条」这种自相矛盾。
    qint64 ignoredCount = 0;
    if (root.contains(QString::fromLatin1(kKeySucceededCount))) {
        if (!readIntField(root, QString::fromLatin1(kKeySucceededCount), &ignoredCount, error)) {
            return false;
        }
    }
    if (root.contains(QString::fromLatin1(kKeyFailedCount))) {
        if (!readIntField(root, QString::fromLatin1(kKeyFailedCount), &ignoredCount, error)) {
            return false;
        }
    }
    if (root.contains(QString::fromLatin1(kKeySkippedCount))) {
        if (!readIntField(root, QString::fromLatin1(kKeySkippedCount), &ignoredCount, error)) {
            return false;
        }
    }
    if (root.contains(QString::fromLatin1(kKeyAborted))) {
        if (!readBoolField(root, QString::fromLatin1(kKeyAborted), &parsed.aborted, error)) {
            return false;
        }
    }

    recount(parsed);
    *report = parsed;
    setError(error, QString());
    return true;
}

}  // namespace aipdf::doc
