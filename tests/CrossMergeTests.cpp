// 交叉合并（多份文件逐页交叉）的单元测试。
//
// 这一组测三件事：
//   1. **页序计算**（pdf::planCrossMerge）：纯计算，不碰 PDFium、不碰界面。
//      其中一条是「与旧实现逐项一致」——旧实现只支持与一份文件交叉，
//      新实现不能把这个口径改掉（这是防回归的关键一条）。
//   2. **页备份 / 还原往返**（pdf::PdfDocument::backupPage + restorePageFrom）：
//      真 PDFium：备份一页再还原回去，内容不能变。
//   3. **命令的 apply / undo 往返**（doc::CrossMergeCommand）：
//      执行后能撤销回交叉前的页面集合；没有来源可取页时不能停在半截状态。
//
// 判据：页序看「每一页来自哪份来源的第几页」；PDFium 那两条看页面文本——
// 文本对得上才算真的把页搬对了，光看页数不够。

#include "app/layout/PdfPageOperations.h"
#include "doc/edit/CrossMergeCommand.h"
#include "pdf/kernel/PdfiumAdapter.h"
#include "pdf/ops/CrossMergePlanner.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QStringList>
#include <QTest>
#include <QVector>

#include <algorithm>
#include <memory>

using aipdf::doc::CrossMergeCommand;
using aipdf::doc::CrossMergePageSource;
using aipdf::doc::PageBackup;
using aipdf::doc::PageOperations;
using aipdf::pdf::CrossMergePlan;
using aipdf::pdf::CrossMergeSlot;
using aipdf::pdf::CrossMergeSource;
using aipdf::pdf::kCrossMergeCurrentSource;
using aipdf::pdf::PdfDocument;

namespace {

QString fixturePath(const QString& name) {
    return QDir(QStringLiteral(AIPDF_PDF_FIXTURES_DIR)).filePath(name);
}

// 一页的文本（去掉首尾空白）。
QString pageText(const PdfDocument& document, int index) {
    const auto page = document.page(index);
    if (!page.has_value()) {
        return QString();
    }
    const auto text = page->extractText();
    if (!text.has_value()) {
        return QString();
    }
    return text->plainText().trimmed();
}

// 页序计划的「哪一页来自谁」清单：每个页位写成 "来源id#页号"。
QStringList slotKeys(const CrossMergePlan& plan) {
    QStringList keys;
    keys.reserve(plan.pageSlots.size());
    for (const CrossMergeSlot& slot : plan.pageSlots) {
        keys.append(QStringLiteral("%1#%2").arg(slot.sourceId).arg(slot.sourcePage));
    }
    return keys;
}

// 旧实现（PdfView::crossMergeDialog 里那一段）的页序，逐项照抄。
//
// 为什么要把它搬进测试：新的页序计算是「多份来源」的推广，
// 但推广必须**与旧口径完全一致**——只有一个来源时，新旧结果要逐项相同。
// 这里保留旧算法当参照系，比对着新算法的输出自我感觉良好可靠得多。
//
// 返回 (哪一边, 页号)：0 = 当前文档，1 = 对方。
QVector<QPair<int, int>> legacyCrossOrder(int mine, int theirs) {
    QVector<QPair<int, int>> order;
    const int total = std::max(mine, theirs);
    for (int index = 0; index < total; ++index) {
        if (index < mine) {
            order.append({0, index});
        }
        if (index < theirs) {
            order.append({1, index});
        }
    }
    return order;
}

// 假的一页：只记一个编号，用来认「这一页是谁」。
class FakePageBackup final : public PageBackup {
public:
    explicit FakePageBackup(int id) : id_(id) {}
    int id() const { return id_; }

private:
    int id_ = -1;
};

// 假的页面操作：页是一串编号，备份就是把编号带出去。
class FakePageOperations final : public PageOperations {
public:
    void setPages(const QVector<int>& pages) { pages_ = pages; }
    QVector<int> pages() const { return pages_; }

    bool insertBlankPage(int, double, double, QVector<int>*) override { return false; }
    bool deletePage(int index, QVector<int>* pageMapping) override {
        if (index < 0 || index >= pages_.size()) {
            return false;
        }
        pages_.remove(index);
        if (pageMapping != nullptr) {
            pageMapping->resize(pages_.size() + 1);
            for (int i = 0; i < pageMapping->size(); ++i) {
                (*pageMapping)[i] = (i < index) ? i : i + 1;
            }
            (*pageMapping)[index] = -1;
        }
        return true;
    }
    bool setPageRotation(int, int) override { return false; }
    bool movePage(int, int, QVector<int>*) override { return false; }

    bool backupPage(int index, std::shared_ptr<PageBackup>* backup) override {
        if (backup == nullptr || index < 0 || index >= pages_.size()) {
            return false;
        }
        *backup = std::make_shared<FakePageBackup>(pages_.at(index));
        return true;
    }
    bool restorePage(int index, const PageBackup& backup) override {
        const auto* page = dynamic_cast<const FakePageBackup*>(&backup);
        if (page == nullptr || index < 0 || index > pages_.size()) {
            return false;
        }
        pages_.insert(index, page->id());
        return true;
    }
    int pageCount() override { return pages_.size(); }

private:
    QVector<int> pages_;
};

// 假的编辑上下文：只记下页面结构变化时交出来的页号映射。
class RecordingEditContext final : public aipdf::doc::EditContext {
public:
    explicit RecordingEditContext(PageOperations* pages) : pages_(pages) {}

    aipdf::doc::AnnotationModel* annotationModel() override { return nullptr; }
    PageOperations* pages() override { return pages_; }
    void pageStructureChanged(const QVector<int>& pageMapping) override {
        mappings_.append(pageMapping);
    }
    QVector<QVector<int>> mappings() const { return mappings_; }

private:
    PageOperations* pages_ = nullptr;
    QVector<QVector<int>> mappings_;
};

// 假的外部来源：来源 id → 页编号列表。
class FakePageSource final : public CrossMergePageSource {
public:
    void addSource(int id, const QVector<int>& pages) { sources_.insert(id, pages); }

    bool backupPage(int sourceId,
                    int sourcePage,
                    std::shared_ptr<PageBackup>* backup) override {
        if (backup == nullptr) {
            return false;
        }
        const auto it = sources_.constFind(sourceId);
        if (it == sources_.constEnd()) {
            return false;
        }
        if (sourcePage < 0 || sourcePage >= it->size()) {
            return false;
        }
        *backup = std::make_shared<FakePageBackup>(it->at(sourcePage));
        return true;
    }

private:
    QHash<int, QVector<int>> sources_;
};

// 真 PDFium 的外部来源：拿 app::PdfPageOperations 去备份对方文档的页。
//
// 备份由 app::PdfPageOperations 拍（它认识自己那套 PdfPageBackup），
// 这样当前文档那侧用同一个适配器还原时，类型是对得上的。
class PdfiumPageSource final : public CrossMergePageSource {
public:
    explicit PdfiumPageSource(aipdf::app::PdfPageOperations* operations)
        : operations_(operations) {}

    bool backupPage(int sourceId,
                    int sourcePage,
                    std::shared_ptr<PageBackup>* backup) override {
        if (operations_ == nullptr) {
            return false;
        }
        return operations_->backupPage(sourcePage, backup);
    }

private:
    aipdf::app::PdfPageOperations* operations_ = nullptr;
};

}  // namespace

class CrossMergeTests : public QObject {
    Q_OBJECT

private slots:
    // 页序计算。
    void singleSourceMatchesLegacyOrder();
    void singleSourceKeepsLegacyOrderForEveryLength();
    void threeSourcesRoundRobin();
    void unequalLengthSkipsShortSource();
    void emptySourceListKeepsCurrentOrder();
    void zeroPageSourceIsIgnored();
    void emptyCurrentStartsFromFirstSource();
    void planCountsTakenPagesPerSource();
    void describePlanListsEverySlot();

    // PDFium 的页备份 / 还原往返。
    void pdfiumBackupRestoreKeepsPageContent();

    // 命令的 apply / undo 往返。
    void commandAppliesPlannedOrder();
    void commandUndoRestoresOriginalPages();
    void commandApplyWithoutSourceLeavesDocumentIntact();
    void commandAppliesAndUndoesOnRealPdfium();
    void commandSerializationKeepsPageOrder();
};

// ---------------------------------------------------------------------------
// 页序计算
// ---------------------------------------------------------------------------

// 单份来源：页序与旧实现逐项一致。
void CrossMergeTests::singleSourceMatchesLegacyOrder() {
    CrossMergeSource source;
    source.id = 7;
    source.pageCount = 2;
    source.label = QStringLiteral("other.pdf");

    // 当前文档 3 页 + 对方 2 页 → 当前#0, 对方#0, 当前#1, 对方#1, 当前#2
    const CrossMergePlan plan = aipdf::pdf::planCrossMerge(3, {source});
    QCOMPARE(slotKeys(plan),
             QStringList({QStringLiteral("-1#0"), QStringLiteral("7#0"), QStringLiteral("-1#1"),
                           QStringLiteral("7#1"), QStringLiteral("-1#2")}));
    QCOMPARE(plan.rounds, 3);
    QCOMPARE(plan.totalPages(), 5);
}

// 单份来源：把各种页数组合都跑一遍，逐项与旧实现比对。
//
// 这一条是防回归的核心：多来源版本不能把「只有一份来源」的口径改掉。
void CrossMergeTests::singleSourceKeepsLegacyOrderForEveryLength() {
    for (int mine = 0; mine <= 6; ++mine) {
        for (int theirs = 0; theirs <= 6; ++theirs) {
            CrossMergeSource source;
            source.id = 1;
            source.pageCount = theirs;

            const CrossMergePlan plan = aipdf::pdf::planCrossMerge(mine, {source});

            QStringList expected;
            for (const auto& item : legacyCrossOrder(mine, theirs)) {
                expected.append(QStringLiteral("%1#%2")
                                    .arg(item.first == 0 ? kCrossMergeCurrentSource : 1)
                                    .arg(item.second));
            }
            QCOMPARE(slotKeys(plan), expected);
        }
    }
}

// 三份来源：以页号为主序、来源为次序。
void CrossMergeTests::threeSourcesRoundRobin() {
    CrossMergeSource first;
    first.id = 0;
    first.pageCount = 2;
    first.label = QStringLiteral("A");
    CrossMergeSource second;
    second.id = 1;
    second.pageCount = 2;
    second.label = QStringLiteral("B");
    CrossMergeSource third;
    third.id = 2;
    third.pageCount = 2;
    third.label = QStringLiteral("C");

    const CrossMergePlan plan = aipdf::pdf::planCrossMerge(2, {first, second, third});

    QCOMPARE(slotKeys(plan),
             QStringList({QStringLiteral("-1#0"), QStringLiteral("0#0"), QStringLiteral("1#0"),
                           QStringLiteral("2#0"), QStringLiteral("-1#1"), QStringLiteral("0#1"),
                           QStringLiteral("1#1"), QStringLiteral("2#1")}));
    QCOMPARE(plan.rounds, 2);
    QCOMPARE(plan.totalPages(), 8);
}

// 页数不等：以最长的为准，短的那份到头了就跳过。
void CrossMergeTests::unequalLengthSkipsShortSource() {
    CrossMergeSource first;
    first.id = 0;
    first.pageCount = 2;
    CrossMergeSource second;
    second.id = 1;
    second.pageCount = 3;

    // 当前 3 页 + A 2 页 + B 3 页 → 8 页。
    const CrossMergePlan plan = aipdf::pdf::planCrossMerge(3, {first, second});
    QCOMPARE(slotKeys(plan),
             QStringList({QStringLiteral("-1#0"), QStringLiteral("0#0"), QStringLiteral("1#0"),
                           QStringLiteral("-1#1"), QStringLiteral("0#1"), QStringLiteral("1#1"),
                           QStringLiteral("-1#2"), QStringLiteral("1#2")}));
    QCOMPARE(plan.rounds, 3);
    QCOMPARE(plan.totalPages(), 8);
}

// 空来源列表：只剩当前文档的页，顺序不变（等于不做交叉）。
void CrossMergeTests::emptySourceListKeepsCurrentOrder() {
    const CrossMergePlan plan = aipdf::pdf::planCrossMerge(3, {});
    QCOMPARE(slotKeys(plan),
             QStringList({QStringLiteral("-1#0"), QStringLiteral("-1#1"), QStringLiteral("-1#2")}));
    QCOMPARE(plan.rounds, 3);
    QCOMPARE(plan.takenPerSource.size(), 1);
    QCOMPARE(plan.takenPerSource.at(0), 3);
}

// 0 页的来源：不参与（每一轮都跳过它）。
void CrossMergeTests::zeroPageSourceIsIgnored() {
    CrossMergeSource empty;
    empty.id = 0;
    empty.pageCount = 0;

    const CrossMergePlan plan = aipdf::pdf::planCrossMerge(2, {empty});
    QCOMPARE(slotKeys(plan), QStringList({QStringLiteral("-1#0"), QStringLiteral("-1#1")}));
    QCOMPARE(plan.rounds, 2);
    QCOMPARE(plan.totalPages(), 2);
    QCOMPARE(plan.takenPerSource.at(1), 0);

    // 当前文档 0 页 + 一个 0 页来源 → 空页序，不崩、不越界。
    const CrossMergePlan emptyPlan = aipdf::pdf::planCrossMerge(0, {empty});
    QVERIFY(emptyPlan.pageSlots.isEmpty());
    QCOMPARE(emptyPlan.rounds, 0);
    QCOMPARE(emptyPlan.totalPages(), 0);
}

// 当前文档 0 页：页序从第一个来源开始。
void CrossMergeTests::emptyCurrentStartsFromFirstSource() {
    CrossMergeSource source;
    source.id = 4;
    source.pageCount = 3;

    const CrossMergePlan plan = aipdf::pdf::planCrossMerge(0, {source});
    QCOMPARE(slotKeys(plan),
             QStringList({QStringLiteral("4#0"), QStringLiteral("4#1"), QStringLiteral("4#2")}));
    QCOMPARE(plan.takenPerSource.at(0), 0);
    QCOMPARE(plan.takenPerSource.at(1), 3);
}

// 每个来源实际被取了几页。
void CrossMergeTests::planCountsTakenPagesPerSource() {
    CrossMergeSource first;
    first.id = 0;
    first.pageCount = 2;
    CrossMergeSource second;
    second.id = 1;
    second.pageCount = 5;

    const CrossMergePlan plan = aipdf::pdf::planCrossMerge(3, {first, second});
    // 下标 0 是当前文档，1、2 依次是第 1、2 份外部来源。
    QCOMPARE(plan.takenPerSource.size(), 3);
    QCOMPARE(plan.takenPerSource.at(0), 3);
    QCOMPARE(plan.takenPerSource.at(1), 2);
    QCOMPARE(plan.takenPerSource.at(2), 5);
}

// 页序的描述文本：人能读懂「第几页来自哪一份」。
void CrossMergeTests::describePlanListsEverySlot() {
    CrossMergeSource source;
    source.id = 0;
    source.pageCount = 1;
    source.label = QStringLiteral("B.pdf");

    const CrossMergePlan plan = aipdf::pdf::planCrossMerge(2, {source});
    QCOMPARE(aipdf::pdf::describeCrossMergePlan(plan, {source}),
             QStringLiteral("当前#0, B.pdf#0, 当前#1"));
}

// ---------------------------------------------------------------------------
// PDFium 的页备份 / 还原往返
// ---------------------------------------------------------------------------

// 备份一页再还原回去：页数对，内容也不能变。
void CrossMergeTests::pdfiumBackupRestoreKeepsPageContent() {
    PdfDocument document = aipdf::pdf::openDocument(fixturePath(QStringLiteral("multipage.pdf")))
                               .document;
    QVERIFY(document.isValid());
    QCOMPARE(document.pageCount(), 12);

    // 备份第 3 页（"Page 3"），再插到最前面。
    const PdfDocument backup = document.backupPage(2);
    QVERIFY(backup.isValid());
    QCOMPARE(backup.pageCount(), 1);

    QVERIFY(document.restorePageFrom(backup, 0));
    QCOMPARE(document.pageCount(), 13);
    QCOMPARE(pageText(document, 0), QStringLiteral("Page 3"));
    // 原来那页被挤到后面一位，内容不变。
    QCOMPARE(pageText(document, 3), QStringLiteral("Page 3"));
    QCOMPARE(pageText(document, 1), QStringLiteral("Page 1"));
}

// ---------------------------------------------------------------------------
// 命令的 apply / undo 往返
// ---------------------------------------------------------------------------

// 执行：页面按计划重排。
void CrossMergeTests::commandAppliesPlannedOrder() {
    FakePageOperations pages;
    pages.setPages({10, 11, 12});

    FakePageSource source;
    source.addSource(0, {20, 21});
    source.addSource(1, {30});

    CrossMergeSource descriptor;
    descriptor.id = 0;
    descriptor.pageCount = 2;
    CrossMergeSource descriptorTwo;
    descriptorTwo.id = 1;
    descriptorTwo.pageCount = 1;

    const CrossMergePlan plan = aipdf::pdf::planCrossMerge(3, {descriptor, descriptorTwo});
    CrossMergeCommand command(plan, std::make_shared<FakePageSource>(source));

    RecordingEditContext context(&pages);
    QVERIFY(command.apply(&context));

    // 当前#0, 源0#0, 源1#0, 当前#1, 源0#1, 当前#2
    QCOMPARE(pages.pages(), QVector<int>({10, 20, 30, 11, 21, 12}));
    QCOMPARE(command.originalPageCount(), 3);

    // 页号映射：交叉前的第 i 页 → 交叉后的第几页。
    QCOMPARE(context.mappings().size(), 1);
    const QVector<int> mapping = context.mappings().first();
    QCOMPARE(mapping.size(), 3);
    QCOMPARE(mapping.at(0), 0);
    QCOMPARE(mapping.at(1), 3);
    QCOMPARE(mapping.at(2), 5);
}

// 撤销：回到交叉前的页面集合。
void CrossMergeTests::commandUndoRestoresOriginalPages() {
    FakePageOperations pages;
    pages.setPages({10, 11, 12});

    FakePageSource source;
    source.addSource(0, {20, 21});

    CrossMergeSource descriptor;
    descriptor.id = 0;
    descriptor.pageCount = 2;

    const CrossMergePlan plan = aipdf::pdf::planCrossMerge(3, {descriptor});
    CrossMergeCommand command(plan, std::make_shared<FakePageSource>(source));

    RecordingEditContext context(&pages);
    QVERIFY(command.apply(&context));
    QCOMPARE(pages.pages(), QVector<int>({10, 20, 11, 21, 12}));

    QVERIFY(command.revert(&context));
    QCOMPARE(pages.pages(), QVector<int>({10, 11, 12}));

    // 撤销时的映射按「合并后的页号」索引。
    QCOMPARE(context.mappings().size(), 2);
    const QVector<int> revertMapping = context.mappings().at(1);
    QCOMPARE(revertMapping.size(), 5);
    QCOMPARE(revertMapping.at(0), 0);
    QCOMPARE(revertMapping.at(1), -1);
    QCOMPARE(revertMapping.at(2), 1);
    QCOMPARE(revertMapping.at(3), -1);
    QCOMPARE(revertMapping.at(4), 2);
}

// 没有来源可取页时：执行失败，而且不能停在半截状态。
void CrossMergeTests::commandApplyWithoutSourceLeavesDocumentIntact() {
    FakePageOperations pages;
    pages.setPages({10, 11, 12});

    CrossMergeSource descriptor;
    descriptor.id = 0;
    descriptor.pageCount = 2;

    const CrossMergePlan plan = aipdf::pdf::planCrossMerge(3, {descriptor});
    // 没有给来源入口：外部的页取不到。
    CrossMergeCommand command(plan, nullptr);

    RecordingEditContext context(&pages);
    QVERIFY(!command.apply(&context));
    // 文档回到交叉前的样子（失败不留半截）。
    QCOMPARE(pages.pages(), QVector<int>({10, 11, 12}));
    // 失败时不改页号映射。
    QVERIFY(context.mappings().isEmpty());
}

// 真 PDFium 上跑一遍：执行后页序对得上，撤销后回到原样。
void CrossMergeTests::commandAppliesAndUndoesOnRealPdfium() {
    const PdfDocument current =
        aipdf::pdf::openDocument(fixturePath(QStringLiteral("multipage.pdf"))).document;
    QVERIFY(current.isValid());
    QCOMPARE(current.pageCount(), 12);

    const PdfDocument other =
        aipdf::pdf::openDocument(fixturePath(QStringLiteral("text.pdf"))).document;
    QVERIFY(other.isValid());
    QCOMPARE(other.pageCount(), 2);

    aipdf::app::PdfPageOperations currentOperations(current);
    aipdf::app::PdfPageOperations otherOperations(other);

    CrossMergeSource descriptor;
    descriptor.id = 0;
    descriptor.pageCount = other.pageCount();
    descriptor.label = QStringLiteral("text.pdf");

    const CrossMergePlan plan = aipdf::pdf::planCrossMerge(current.pageCount(), {descriptor});
    CrossMergeCommand command(plan, std::make_shared<PdfiumPageSource>(&otherOperations));

    RecordingEditContext context(&currentOperations);
    QVERIFY(command.apply(&context));

    // 12 页 + 2 页 → 14 页：当前#0, 对方#0, 当前#1, 对方#1, 当前#2 … 当前#11。
    QCOMPARE(currentOperations.pageCount(), 14);
    QCOMPARE(pageText(current, 0), QStringLiteral("Page 1"));
    QVERIFY(pageText(current, 1).contains(QStringLiteral("Full text search")));
    QCOMPARE(pageText(current, 2), QStringLiteral("Page 2"));
    QVERIFY(pageText(current, 3).contains(QStringLiteral("appears on the second page")));
    QCOMPARE(pageText(current, 4), QStringLiteral("Page 3"));
    QCOMPARE(pageText(current, 13), QStringLiteral("Page 12"));

    // 撤销：回到交叉前的 12 页，顺序不变。
    QVERIFY(command.revert(&context));
    QCOMPARE(currentOperations.pageCount(), 12);
    QCOMPARE(pageText(current, 0), QStringLiteral("Page 1"));
    QCOMPARE(pageText(current, 11), QStringLiteral("Page 12"));
}

// 序列化：页序描述进得去也出得来。
void CrossMergeTests::commandSerializationKeepsPageOrder() {
    CrossMergeSource descriptor;
    descriptor.id = 3;
    descriptor.pageCount = 2;

    const CrossMergePlan plan = aipdf::pdf::planCrossMerge(2, {descriptor});
    CrossMergeCommand command(plan, nullptr);

    const QByteArray data = command.serialize();
    QVERIFY(!data.isEmpty());
    QCOMPARE(command.typeName(), QStringLiteral("page.crossMerge"));

    CrossMergeCommand restored;
    QString error;
    QVERIFY(restored.deserialize(data, &error));
    QCOMPARE(slotKeys(restored.plan()), slotKeys(plan));
    QCOMPARE(restored.plan().rounds, plan.rounds);
    QCOMPARE(restored.plan().takenPerSource, plan.takenPerSource);
}

QTEST_MAIN(CrossMergeTests)
#include "CrossMergeTests.moc"
