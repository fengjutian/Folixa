// OFD 转换器注册（支持列表 + 参数生成 + 哈希校验）的单元测试。
//
// 盯的是三件事：
//   1) 支持列表里每一项都**核实过**（有上游地址、有许可证说明、参数模板完整）；
//   2) 不同转换器的命令行约定**不一样**，认错就会把文件写错地方；
//   3) 哈希复核能把「同一个路径上的文件被换掉了」这件事认出来。
//
// 为什么哈希校验要有：程序要**执行**用户指定的外部可执行文件。
// 那个文件在两次运行之间被顶替了，程序不该毫无察觉地执行新内容。

#include "app/controllers/OfdConverters.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using aipdf::app::checkOfdConverterIntegrity;
using aipdf::app::copyOfdResultTo;
using aipdf::app::OfdConverterIntegrity;
using aipdf::app::ofdConverterProfileFor;
using aipdf::app::ofdSandboxLayout;
using aipdf::app::pickFreeOfdOutputPath;
using aipdf::app::pickFreePdfPath;
using aipdf::app::planOfdConversion;
using aipdf::app::prepareOfdConversion;
using aipdf::app::sha256OfFile;
using aipdf::app::supportedOfdConverters;

namespace {

// 写一个临时文件，返回它的路径。
QString writeTempFile(const QTemporaryDir& dir, const QString& name, const QByteArray& content)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return QString();
    }
    file.write(content);
    file.close();
    return path;
}

}  // namespace
class OfdConverterTests final : public QObject {
    Q_OBJECT

private slots:
    void supportListEntriesAreComplete();
    void profileIsRecognizedByProgramName();
    void ofd2pdfOnlyTakesTheInputFile();
    void ofdConverterTakesFormatAndBothPaths();
    void unknownToolFallsBackToGenericConvention();
    void planRejectsMissingInputAndOutput();
    void sandboxKeepsFileNameAndPutsOutputBesideIt();
    void sandboxMakesAllConverterTypesConvergeOnOneOutput();
    void converterCannotOverwriteTheUsersOwnPdf();
    void freeOutputPathSkipsNamesThatAlreadyExist();
    void sha256IsStableAndFollowsContent();
    void integrityTracksFirstUseUnchangedAndChanged();
    void missingConverterIsReported();
};

void OfdConverterTests::supportListEntriesAreComplete() {
    const auto profiles = supportedOfdConverters();
    QVERIFY(!profiles.isEmpty());

    QStringList ids;
    for (const auto& profile : profiles) {
        QVERIFY2(!profile.id.isEmpty(), "支持列表里有一项没有 id");
        QVERIFY2(!profile.displayName.isEmpty(), "支持列表里有一项没有显示名");
        QVERIFY2(!profile.argumentTemplate.isEmpty(),
                 qPrintable(QStringLiteral("转换器 %1 没有参数模板").arg(profile.id)));
        QVERIFY2(profile.sourceUrl.startsWith(QLatin1String("https://")),
                 qPrintable(QStringLiteral("转换器 %1 没写上游地址").arg(profile.id)));
        QVERIFY2(!profile.licenseNote.isEmpty(),
                 qPrintable(QStringLiteral("转换器 %1 没写许可证状况").arg(profile.id)));
        // 只收输入的转换器，模板里就不该出现 %2。
        if (!profile.acceptsOutputPath) {
            QVERIFY2(!profile.argumentTemplate.contains(QStringLiteral("%2")),
                     qPrintable(QStringLiteral("转换器 %1 不接受输出路径，模板里却有 %2")
                                    .arg(profile.id)));
        } else {
            QVERIFY2(profile.argumentTemplate.contains(QStringLiteral("%2")),
                     qPrintable(QStringLiteral("转换器 %1 接受输出路径，模板里却没有 %2")
                                    .arg(profile.id)));
        }
        QVERIFY2(!ids.contains(profile.id),
                 qPrintable(QStringLiteral("支持列表里有重复 id：%1").arg(profile.id)));
        ids.append(profile.id);
    }
}

void OfdConverterTests::profileIsRecognizedByProgramName() {
    QCOMPARE(ofdConverterProfileFor(QStringLiteral("ofd2pdf")).id, QStringLiteral("ofd2pdf"));
    // Windows 上文件系统不区分大小写：用户装的可能叫 Ofd2Pdf.exe。
    QCOMPARE(ofdConverterProfileFor(QStringLiteral("Ofd2Pdf.exe")).id, QStringLiteral("ofd2pdf"));
    QCOMPARE(ofdConverterProfileFor(QStringLiteral("/usr/local/bin/ofd-converter")).id,
             QStringLiteral("ofd-converter"));
    // 认不出来：返回空 profile（调用方按通用约定走）。
    QVERIFY(ofdConverterProfileFor(QStringLiteral("my-own-ofd-tool.exe")).id.isEmpty());
}

void OfdConverterTests::ofd2pdfOnlyTakesTheInputFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = writeTempFile(dir, QStringLiteral("invoice.ofd"), QByteArray("ofd"));
    QVERIFY(!input.isEmpty());
    const QString output = dir.filePath(QStringLiteral("invoice.pdf"));

    const auto plan = planOfdConversion(QStringLiteral("C:/tools/ofd2pdf.exe"), input, output);
    QVERIFY2(plan.isValid(), qPrintable(plan.error));

    // ofd2pdf 只收一个输入文件 —— 多给一个输出路径它不认。
    QCOMPARE(plan.arguments.size(), 1);
    QCOMPARE(plan.arguments.first(), QFileInfo(input).absoluteFilePath());

    // 输出落在输入旁边、同名不同后缀；调用方再搬走。
    QCOMPARE(plan.expectedOutput, QDir::cleanPath(dir.filePath(QStringLiteral("invoice.pdf"))));
}

void OfdConverterTests::ofdConverterTakesFormatAndBothPaths() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = writeTempFile(dir, QStringLiteral("invoice.ofd"), QByteArray("ofd"));
    const QString output = dir.filePath(QStringLiteral("out/invoice.pdf"));

    const auto plan = planOfdConversion(QStringLiteral("ofd-converter"), input, output);
    QVERIFY2(plan.isValid(), qPrintable(plan.error));

    QCOMPARE(plan.arguments.size(), 4);
    QCOMPARE(plan.arguments.at(0), QStringLiteral("-format"));
    QCOMPARE(plan.arguments.at(1), QStringLiteral("pdf"));
    QCOMPARE(plan.arguments.at(2), QFileInfo(input).absoluteFilePath());
    QCOMPARE(plan.arguments.at(3), QDir::cleanPath(QFileInfo(output).absoluteFilePath()));
    QCOMPARE(plan.expectedOutput, QDir::cleanPath(QFileInfo(output).absoluteFilePath()));
}

void OfdConverterTests::unknownToolFallsBackToGenericConvention() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = writeTempFile(dir, QStringLiteral("invoice.ofd"), QByteArray("ofd"));
    const QString output = dir.filePath(QStringLiteral("invoice.pdf"));

    // 认不出来的工具仍然能用：按「输入 输出」这个通用约定调。
    const auto plan = planOfdConversion(QStringLiteral("C:/tools/my-tool.exe"), input, output);
    QVERIFY2(plan.isValid(), qPrintable(plan.error));
    QCOMPARE(plan.arguments.size(), 2);
    QCOMPARE(plan.arguments.at(0), QFileInfo(input).absoluteFilePath());
    QCOMPARE(plan.arguments.at(1), QDir::cleanPath(QFileInfo(output).absoluteFilePath()));
    QCOMPARE(plan.expectedOutput, QDir::cleanPath(QFileInfo(output).absoluteFilePath()));
}

void OfdConverterTests::planRejectsMissingInputAndOutput() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString output = dir.filePath(QStringLiteral("out.pdf"));

    const auto noInput = planOfdConversion(QStringLiteral("ofd2pdf"),
                                           dir.filePath(QStringLiteral("missing.ofd")), output);
    QVERIFY(!noInput.isValid());
    QVERIFY(!noInput.error.isEmpty());

    const QString input = writeTempFile(dir, QStringLiteral("invoice.ofd"), QByteArray("ofd"));
    const auto noOutput = planOfdConversion(QStringLiteral("ofd2pdf"), input, QString());
    QVERIFY(!noOutput.isValid());
    QVERIFY(!noOutput.error.isEmpty());

    const auto noProgram = planOfdConversion(QString(), input, output);
    QVERIFY(!noProgram.isValid());
    QVERIFY(!noProgram.error.isEmpty());
}

void OfdConverterTests::sandboxKeepsFileNameAndPutsOutputBesideIt() {
    QTemporaryDir userDir;
    QVERIFY(userDir.isValid());
    QTemporaryDir sandboxDir;
    QVERIFY(sandboxDir.isValid());
    const QString input = writeTempFile(userDir, QStringLiteral("invoice.ofd"), QByteArray("ofd"));
    QVERIFY(!input.isEmpty());

    const auto layout = ofdSandboxLayout(sandboxDir.path(), input);
    // 输入副本**保持原文件名**：转换器按自己的规矩找产出（同名不同后缀），
    // 改了名字它就找不到了。
    QCOMPARE(layout.inputPath, QDir::cleanPath(sandboxDir.filePath(QStringLiteral("invoice.ofd"))));
    // 产出落在输入旁边 —— 但那是在**沙箱**里，不在用户目录。
    QCOMPARE(layout.outputPath, QDir::cleanPath(sandboxDir.filePath(QStringLiteral("invoice.pdf"))));
    QVERIFY(!layout.inputPath.startsWith(userDir.path()));
    QVERIFY(!layout.outputPath.startsWith(userDir.path()));
}

void OfdConverterTests::sandboxMakesAllConverterTypesConvergeOnOneOutput() {
    QTemporaryDir userDir;
    QVERIFY(userDir.isValid());
    QTemporaryDir sandboxDir;
    QVERIFY(sandboxDir.isValid());
    const QString input = writeTempFile(userDir, QStringLiteral("invoice.ofd"), QByteArray("ofd"));
    QVERIFY(!input.isEmpty());

    // 三种命令行约定，在沙箱里必须都指向**同一个**产出文件 ——
    // 否则「转换器把文件写在哪」说不清，搬运逻辑就没法写。
    const auto onlyInput = prepareOfdConversion(QStringLiteral("C:/tools/ofd2pdf.exe"), input,
                                                 sandboxDir.path());
    QVERIFY2(onlyInput.isValid(), qPrintable(onlyInput.error));
    QCOMPARE(onlyInput.plan.expectedOutput, onlyInput.layout.outputPath);

    const auto withOutput =
        prepareOfdConversion(QStringLiteral("ofd-converter"), input, sandboxDir.path());
    QVERIFY2(withOutput.isValid(), qPrintable(withOutput.error));
    QCOMPARE(withOutput.plan.expectedOutput, withOutput.layout.outputPath);

    const auto generic =
        prepareOfdConversion(QStringLiteral("C:/tools/my-tool.exe"), input, sandboxDir.path());
    QVERIFY2(generic.isValid(), qPrintable(generic.error));
    QCOMPARE(generic.plan.expectedOutput, generic.layout.outputPath);

    // 只收输入的那种，命令行里绝不能出现输出路径（它不认这个参数）。
    QCOMPARE(onlyInput.plan.arguments.size(), 1);
    QCOMPARE(onlyInput.plan.arguments.first(), onlyInput.layout.inputPath);

    // 输入副本已经复制进沙箱了（转换器看到的是那一份）。
    QVERIFY(QFileInfo::exists(onlyInput.layout.inputPath));
}

void OfdConverterTests::converterCannotOverwriteTheUsersOwnPdf() {
    QTemporaryDir userDir;
    QVERIFY(userDir.isValid());
    QTemporaryDir sandboxDir;
    QVERIFY(sandboxDir.isValid());

    // 用户目录里已经有同名的 .pdf（他自己的文件）和待转换的 .ofd。
    const QString input = writeTempFile(userDir, QStringLiteral("invoice.ofd"), QByteArray("ofd"));
    const QString ownPdf = writeTempFile(userDir, QStringLiteral("invoice.pdf"), QByteArray("MY OWN PDF"));
    QVERIFY(!input.isEmpty());
    QVERIFY(!ownPdf.isEmpty());

    // 目的地不能是那份已有的 .pdf，也不能撞上任何已存在的名字。
    const QString output = pickFreeOfdOutputPath(input);
    QVERIFY(output != ownPdf);
    QVERIFY(!QFileInfo::exists(output));

    // 转换器在沙箱里跑：它的产出落在沙箱，不在用户目录。
    const auto session =
        prepareOfdConversion(QStringLiteral("C:/tools/ofd2pdf.exe"), input, sandboxDir.path());
    QVERIFY2(session.isValid(), qPrintable(session.error));
    QVERIFY(!session.plan.expectedOutput.startsWith(userDir.path()));
    // 整条链走下来，目的地也不能是用户自己那份。
    QVERIFY(session.destination != ownPdf);
    QVERIFY(!QFileInfo::exists(session.destination));

    // 模拟转换器写出产物（它写的是沙箱里那份）：用户自己那份 .pdf 不能被动。
    writeTempFile(sandboxDir, QStringLiteral("invoice.pdf"), QByteArray("CONVERTED"));

    QFile own(ownPdf);
    QVERIFY(own.open(QIODevice::ReadOnly));
    QCOMPARE(own.readAll(), QByteArray("MY OWN PDF"));
}

void OfdConverterTests::freeOutputPathSkipsNamesThatAlreadyExist() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString input = writeTempFile(dir, QStringLiteral("invoice.ofd"), QByteArray("ofd"));
    QVERIFY(!input.isEmpty());

    // 没有同名 .pdf：就用它。
    QCOMPARE(pickFreeOfdOutputPath(input),
             QDir::cleanPath(dir.filePath(QStringLiteral("invoice.pdf"))));

    // 有了：换名。
    writeTempFile(dir, QStringLiteral("invoice.pdf"), QByteArray("own"));
    QCOMPARE(pickFreeOfdOutputPath(input),
             QDir::cleanPath(dir.filePath(QStringLiteral("invoice-converted.pdf"))));

    // 连 -converted 也占了：继续加序号。
    writeTempFile(dir, QStringLiteral("invoice-converted.pdf"), QByteArray("own2"));
    QCOMPARE(pickFreeOfdOutputPath(input),
             QDir::cleanPath(dir.filePath(QStringLiteral("invoice-converted2.pdf"))));

    // 落盘前再挑一次（挑名字到落盘之间转换器还在跑，那个名字可能被人占了）：
    // 绕开它，**而且不要删掉它** —— 这一轮修的就是「别弄丢用户自己的文件」。
    const QString occupied = QDir::cleanPath(dir.filePath(QStringLiteral("invoice.pdf")));
    const QString fresh = pickFreePdfPath(occupied);
    QVERIFY(fresh != occupied);
    QVERIFY(!QFileInfo::exists(fresh));
    QVERIFY(QFileInfo::exists(occupied));

    // 真正落盘：占着的那份不能被覆盖、也不能被删。
    QTemporaryDir sandboxDir;
    QVERIFY(sandboxDir.isValid());
    const QString produced = writeTempFile(sandboxDir, QStringLiteral("invoice.pdf"), QByteArray("CONVERTED"));
    QVERIFY(!produced.isEmpty());
    const QString written = copyOfdResultTo(produced, occupied);
    QVERIFY(!written.isEmpty());
    QVERIFY(written != occupied);

    QFile own(occupied);
    QVERIFY(own.open(QIODevice::ReadOnly));
    QCOMPARE(own.readAll(), QByteArray("own"));  // 用户那份没被动
    own.close();

    QFile result(written);
    QVERIFY(result.open(QIODevice::ReadOnly));
    QCOMPARE(result.readAll(), QByteArray("CONVERTED"));
}

void OfdConverterTests::sha256IsStableAndFollowsContent() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTempFile(dir, QStringLiteral("tool.exe"), QByteArray("first-content"));

    const QString first = sha256OfFile(path);
    QCOMPARE(first.size(), 64);
    QCOMPARE(first, sha256OfFile(path));
    QCOMPARE(first, first.toLower());

    // 内容变了，哈希必须跟着变 —— 否则这道校验是空的。
    const QString second = writeTempFile(dir, QStringLiteral("tool.exe"), QByteArray("second-content"));
    QVERIFY(first != sha256OfFile(second));

    // 读不出来的文件给空串，不能给一个「看起来像哈希」的假值。
    QVERIFY(sha256OfFile(dir.filePath(QStringLiteral("missing.exe"))).isEmpty());
    QVERIFY(sha256OfFile(dir.path()).isEmpty());
}

void OfdConverterTests::integrityTracksFirstUseUnchangedAndChanged() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTempFile(dir, QStringLiteral("ofd2pdf.exe"), QByteArray("version-1"));

    // 从来没用过：首次使用。
    const auto first = checkOfdConverterIntegrity(path, QString(), QString());
    QCOMPARE(first.status, OfdConverterIntegrity::FirstUse);
    QCOMPARE(first.sha256.size(), 64);

    // 记下哈希后再跑：一致。
    const auto same = checkOfdConverterIntegrity(path, path, first.sha256);
    QCOMPARE(same.status, OfdConverterIntegrity::Unchanged);
    QVERIFY(!same.message.isEmpty());

    // 同一个路径上的文件被换掉了：必须认出来。
    writeTempFile(dir, QStringLiteral("ofd2pdf.exe"), QByteArray("version-2-tampered"));
    const auto changed = checkOfdConverterIntegrity(path, path, first.sha256);
    QCOMPARE(changed.status, OfdConverterIntegrity::Changed);
    QVERIFY(changed.sha256 != first.sha256);
    QVERIFY(changed.message.contains(QStringLiteral("SHA-256")));

    // 换了一个转换器（路径变了）：这是新的一套，按首次使用处理。
    const QString other = writeTempFile(dir, QStringLiteral("other-ofd.exe"), QByteArray("other"));
    const auto switched = checkOfdConverterIntegrity(other, path, first.sha256);
    QCOMPARE(switched.status, OfdConverterIntegrity::FirstUse);
}

void OfdConverterTests::missingConverterIsReported() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto missing = checkOfdConverterIntegrity(dir.filePath(QStringLiteral("nope.exe")),
                                                   QString(), QString());
    QCOMPARE(missing.status, OfdConverterIntegrity::Missing);
    QVERIFY(!missing.message.isEmpty());

    const auto empty = checkOfdConverterIntegrity(QString(), QString(), QString());
    QCOMPARE(empty.status, OfdConverterIntegrity::Missing);
}

QTEST_GUILESS_MAIN(OfdConverterTests)
#include "OfdConverterTests.moc"
