// 多方签署的流程状态机 + 签署报告 + 签署记录。
//
// 这一层不碰 PDF 字节，也不碰页面坐标：签署流程关心的是
// 「谁在什么时候做了什么」，跟签名长什么样、盖在哪一页没有关系。
// 因此这里只依赖 QtCore，专项单测不必牵进 PDFium 与 qpdf。
//
// 状态机为什么要这么严：
// 签署记录是事后追责用的流水。流程一旦走到 Completed / Aborted，
// 如果还能改签署人状态，这条流水就不再可信了——所以终态之后
// 任何修改都直接拒绝，并把「为什么不行」写进错误串。

#include "pdf/SignatureWorkflow.h"

#include <QStringList>
#include <QUuid>

namespace aipdf::pdf {
namespace {

// 时间一律用 ISO 8601 序列化：既是人能读的，也是能被别处解析的。
// 报告文本里也用它，这样报告被复制出去之后仍然能还原成时间点。
QString toIso(const QDateTime& when) {
    return when.isValid() ? when.toString(Qt::ISODate) : QString();
}

QString statusLabel(SignStepStatus status) {
    switch (status) {
    case SignStepStatus::Signed:
        return QStringLiteral("已签署");
    case SignStepStatus::Rejected:
        return QStringLiteral("已拒签");
    case SignStepStatus::Skipped:
        return QStringLiteral("已跳过");
    case SignStepStatus::Pending:
        break;
    }
    return QStringLiteral("待签署");
}

QString stateLabel(WorkflowState state) {
    switch (state) {
    case WorkflowState::InProgress:
        return QStringLiteral("进行中");
    case WorkflowState::Completed:
        return QStringLiteral("已完成");
    case WorkflowState::Aborted:
        return QStringLiteral("已中止");
    case WorkflowState::Draft:
        break;
    }
    return QStringLiteral("草稿");
}

// 重复处理某位签署人时的原因说明。
// 为什么单独写一个：三种"已经处理过"的措辞不一样——
// 签过、拒签、跳过在事后追责时是三件不同的事，混成一句"已处理"就丢信息了。
QString duplicateReason(const Signatory& entry) {
    switch (entry.status) {
    case SignStepStatus::Signed:
        return QStringLiteral("该签署人已经签过：") + entry.name;
    case SignStepStatus::Rejected:
        return QStringLiteral("该签署人已经拒签：") + entry.name;
    case SignStepStatus::Skipped:
        return QStringLiteral("该签署人已经被跳过：") + entry.name;
    case SignStepStatus::Pending:
        break;
    }
    return QStringLiteral("该签署人当前不是待签署状态：") + entry.name;
}

void writeError(QString* error, const QString& message) {
    if (error) {
        *error = message;
    }
}

// 流程编号：8 位十六进制短码。
// 为什么不用完整 UUID：编号会显示在标题栏和报告抬头里，太长没法看；
// 8 位十六进制（约 40 亿种组合）在一个本地应用里几乎不可能撞号。
QString newWorkflowId() {
    const QString raw = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return QStringLiteral("WF-") + raw.left(8).toUpper();
}

}  // namespace

// ---------------------------------------------------------------- 流程 ----

QString SignatureWorkflow::createWorkflow(const QString& title, const QString& initiator, QString* error) {
    if (m_created) {
        // 同一个实例只允许有一条流程：再调一次 create 等于想把旧流水丢掉重开，
        // 而流水一旦开始就不该被丢弃。要开新流程就 new 一个实例。
        writeError(error, QStringLiteral("流程已创建，不能重复创建"));
        return QString();
    }

    const QString trimmedTitle = title.trimmed();
    if (trimmedTitle.isEmpty()) {
        writeError(error, QStringLiteral("流程标题不能为空"));
        return QString();
    }

    const QString trimmedInitiator = initiator.trimmed();
    if (trimmedInitiator.isEmpty()) {
        writeError(error, QStringLiteral("发起人不能为空"));
        return QString();
    }

    m_created = true;
    m_id = newWorkflowId();
    m_title = trimmedTitle;
    m_initiator = trimmedInitiator;
    m_state = WorkflowState::Draft;
    m_createdAt = QDateTime::currentDateTime();
    appendEvent(m_createdAt, m_initiator, QStringLiteral("created"),
                QStringLiteral("创建签署流程：") + m_title);
    return m_id;
}

void SignatureWorkflow::addSignatory(const Signatory& s, QString* error) {
    const QString name = s.name.trimmed();
    if (name.isEmpty()) {
        writeError(error, QStringLiteral("签署人姓名不能为空"));
        return;
    }

    if (!m_created) {
        writeError(error, QStringLiteral("流程尚未创建"));
        return;
    }

    // 只允许在草稿阶段加签署人：签署开始之后再往名单里插人，
    // 「签署记录」就没法解释——流水里会出现一个中途加进来的签署人，
    // 而前面已经签过的人并不知道这件事。
    if (m_state == WorkflowState::InProgress) {
        writeError(error, QStringLiteral("流程已开始，不能再添加签署人"));
        return;
    }
    if (m_state == WorkflowState::Completed || m_state == WorkflowState::Aborted) {
        writeError(error, QStringLiteral("流程已结束，不能再修改"));
        return;
    }

    if (findByName(name) >= 0) {
        writeError(error, QStringLiteral("签署人已存在：") + name);
        return;
    }

    // order 传 0 时按「未指定」处理，自动排到当前最后一位；
    // 这样上层新增签署人时不用先算一遍序号。
    int order = s.order;
    if (order <= 0) {
        order = nextFreeOrder();
    }
    if (findByOrder(order) >= 0) {
        writeError(error, QStringLiteral("签署顺序重复：") + QString::number(order));
        return;
    }

    Signatory entry = s;
    entry.name = name;
    entry.contact = s.contact.trimmed();
    entry.order = order;
    // 状态一律从 Pending 起步：外部传进来的 status 一律不认。
    // 否则调用方可以凭一个 struct 直接把流程推进到「已签」，流水就成了摆设。
    entry.status = SignStepStatus::Pending;
    entry.signedAt = QDateTime();
    entry.rejectReason.clear();

    // 插到按 order 升序的位置：signatories() 的返回顺序就是显示顺序。
    int insertAt = 0;
    while (insertAt < m_signatories.size() && m_signatories.at(insertAt).order < order) {
        ++insertAt;
    }
    m_signatories.insert(insertAt, entry);

    appendEvent(QDateTime::currentDateTime(), m_initiator, QStringLiteral("signatory_added"),
                QStringLiteral("添加签署人：") + name + QStringLiteral("（第 ") + QString::number(order) +
                    QStringLiteral(" 位）"));
}

bool SignatureWorkflow::removeSignatory(const QString& name, QString* error) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        writeError(error, QStringLiteral("签署人姓名不能为空"));
        return false;
    }
    if (!m_created) {
        writeError(error, QStringLiteral("流程尚未创建"));
        return false;
    }
    // 只有草稿阶段能删：流程一旦开始，名单就是审计凭据的一部分。
    if (m_state != WorkflowState::Draft) {
        writeError(error, QStringLiteral("流程已开始或已结束，不能再删除签署人"));
        return false;
    }
    const int index = findByName(trimmed);
    if (index < 0) {
        writeError(error, QStringLiteral("找不到签署人：") + trimmed);
        return false;
    }
    m_signatories.remove(index);
    appendEvent(QDateTime::currentDateTime(), m_initiator, QStringLiteral("signatory_removed"),
                QStringLiteral("移除签署人：") + trimmed);
    return true;
}

bool SignatureWorkflow::startSigning(QString* error) {
    if (!m_created) {
        writeError(error, QStringLiteral("流程尚未创建"));
        return false;
    }
    if (m_state == WorkflowState::InProgress) {
        writeError(error, QStringLiteral("流程已开始，不能重复开始"));
        return false;
    }
    if (m_state == WorkflowState::Completed || m_state == WorkflowState::Aborted) {
        writeError(error, QStringLiteral("流程已结束，不能再修改"));
        return false;
    }
    if (m_signatories.isEmpty()) {
        writeError(error, QStringLiteral("没有签署人，无法开始签署"));
        return false;
    }

    m_state = WorkflowState::InProgress;
    appendEvent(QDateTime::currentDateTime(), m_initiator, QStringLiteral("started"),
                QStringLiteral("开始签署，共 ") + QString::number(m_signatories.size()) +
                    QStringLiteral(" 位签署人"));
    return true;
}

bool SignatureWorkflow::recordSigned(const QString& name, const QDateTime& at, QString* error) {
    if (!ensureInProgress(error)) {
        return false;
    }

    const QString trimmed = name.trimmed();
    const int index = findByName(trimmed);
    if (index < 0) {
        writeError(error, QStringLiteral("签署人不存在：") + trimmed);
        return false;
    }

    Signatory& entry = m_signatories[index];
    if (entry.status != SignStepStatus::Pending) {
        writeError(error, duplicateReason(entry));
        return false;
    }

    // 时间无效就按「此刻」记：调用方忘了传时间不该把流程卡住，
    // 而且流水中宁可有一个不精确的时间，也不要空时间。
    const QDateTime when = at.isValid() ? at : QDateTime::currentDateTime();
    entry.status = SignStepStatus::Signed;
    entry.signedAt = when;
    entry.rejectReason.clear();

    appendEvent(when, trimmed, QStringLiteral("signed"), QStringLiteral("已完成签署"));
    return true;
}

bool SignatureWorkflow::recordRejected(const QString& name, const QString& reason, const QDateTime& at,
                                       QString* error) {
    if (!ensureInProgress(error)) {
        return false;
    }

    const QString trimmedReason = reason.trimmed();
    if (trimmedReason.isEmpty()) {
        // 拒签是整个流程里最需要留痕的一步，没理由的拒签等于没拒签。
        writeError(error, QStringLiteral("拒签原因不能为空"));
        return false;
    }

    const QString trimmed = name.trimmed();
    const int index = findByName(trimmed);
    if (index < 0) {
        writeError(error, QStringLiteral("签署人不存在：") + trimmed);
        return false;
    }

    Signatory& entry = m_signatories[index];
    if (entry.status != SignStepStatus::Pending) {
        writeError(error, duplicateReason(entry));
        return false;
    }

    const QDateTime when = at.isValid() ? at : QDateTime::currentDateTime();
    entry.status = SignStepStatus::Rejected;
    entry.rejectReason = trimmedReason;
    entry.signedAt = QDateTime();

    appendEvent(when, trimmed, QStringLiteral("rejected"), QStringLiteral("已拒签：") + trimmedReason);
    return true;
}

bool SignatureWorkflow::skipSignatory(const QString& name, QString* error) {
    if (!ensureInProgress(error)) {
        return false;
    }

    const QString trimmed = name.trimmed();
    const int index = findByName(trimmed);
    if (index < 0) {
        writeError(error, QStringLiteral("签署人不存在：") + trimmed);
        return false;
    }

    Signatory& entry = m_signatories[index];
    if (entry.status != SignStepStatus::Pending) {
        writeError(error, duplicateReason(entry));
        return false;
    }

    entry.status = SignStepStatus::Skipped;
    entry.signedAt = QDateTime();
    entry.rejectReason.clear();

    appendEvent(QDateTime::currentDateTime(), trimmed, QStringLiteral("skipped"),
                QStringLiteral("已跳过签署"));
    return true;
}

bool SignatureWorkflow::completeWorkflow(QString* error) {
    if (!ensureInProgress(error)) {
        return false;
    }

    // 还差谁，必须点名：只说「还有未完成的步骤」的话，
    // 调用方根本不知道该催谁，只能自己再遍历一遍名单。
    QStringList pending;
    for (const Signatory& entry : m_signatories) {
        if (entry.status == SignStepStatus::Pending) {
            pending.append(entry.name);
        }
    }
    if (!pending.isEmpty()) {
        writeError(error, QStringLiteral("还有 %1 位签署人未处理：%2")
                               .arg(pending.size())
                               .arg(pending.join(QStringLiteral("、"))));
        return false;
    }

    m_state = WorkflowState::Completed;
    appendEvent(QDateTime::currentDateTime(), m_initiator, QStringLiteral("completed"),
                QStringLiteral("签署流程已完成"));
    return true;
}

bool SignatureWorkflow::abortWorkflow(const QString& reason, QString* error) {
    if (!m_created) {
        writeError(error, QStringLiteral("流程尚未创建"));
        return false;
    }
    if (m_state == WorkflowState::Completed || m_state == WorkflowState::Aborted) {
        writeError(error, QStringLiteral("流程已结束，不能再修改"));
        return false;
    }

    const QString trimmedReason = reason.trimmed();
    if (trimmedReason.isEmpty()) {
        writeError(error, QStringLiteral("中止原因不能为空"));
        return false;
    }

    m_state = WorkflowState::Aborted;
    appendEvent(QDateTime::currentDateTime(), m_initiator, QStringLiteral("aborted"),
                QStringLiteral("中止签署流程：") + trimmedReason);
    return true;
}

// ---------------------------------------------------------------- 查询 ----

WorkflowState SignatureWorkflow::state() const {
    return m_state;
}

QVector<Signatory> SignatureWorkflow::signatories() const {
    return m_signatories;
}

QVector<WorkflowEvent> SignatureWorkflow::events() const {
    return m_events;
}

QString SignatureWorkflow::id() const {
    return m_id;
}

QString SignatureWorkflow::title() const {
    return m_title;
}

QString SignatureWorkflow::initiator() const {
    return m_initiator;
}

QString SignatureWorkflow::nextPendingSignatory() const {
    for (const Signatory& entry : m_signatories) {
        if (entry.status == SignStepStatus::Pending) {
            return entry.name;
        }
    }
    return QString();
}

// ---------------------------------------------------------------- 报告 ----

SignatureWorkflow::Report SignatureWorkflow::buildReport() const {
    Report report;
    report.id = m_id;
    report.title = m_title;
    report.initiator = m_initiator;
    report.state = m_state;
    report.createdAt = m_createdAt;
    report.updatedAt = m_events.isEmpty() ? m_createdAt : m_events.last().at;
    report.signatories = m_signatories;
    report.events = m_events;

    for (const Signatory& entry : m_signatories) {
        switch (entry.status) {
        case SignStepStatus::Signed:
            ++report.signedCount;
            break;
        case SignStepStatus::Rejected:
            ++report.rejectedCount;
            break;
        case SignStepStatus::Skipped:
            ++report.skippedCount;
            break;
        case SignStepStatus::Pending:
            ++report.pendingCount;
            break;
        }
    }
    return report;
}

QString SignatureWorkflow::Report::toPlainText() const {
    QStringList lines;
    lines.append(QStringLiteral("签署报告：") + title);
    lines.append(QStringLiteral("流程编号：") + id);
    lines.append(QStringLiteral("流程状态：") + stateLabel(state));
    lines.append(QStringLiteral("发起人：") + initiator);
    lines.append(QStringLiteral("创建时间：") + toIso(createdAt));
    lines.append(QStringLiteral("更新时间：") + toIso(updatedAt));
    lines.append(QString());
    lines.append(QStringLiteral("签署统计：共 %1 人（已签署 %2，已拒签 %3，已跳过 %4，待签署 %5）")
                     .arg(signatories.size())
                     .arg(signedCount)
                     .arg(rejectedCount)
                     .arg(skippedCount)
                     .arg(pendingCount));
    lines.append(QString());
    lines.append(QStringLiteral("签署明细："));
    for (const Signatory& entry : signatories) {
        QString line = QStringLiteral("  %1. %2").arg(entry.order).arg(entry.name);
        if (!entry.contact.isEmpty()) {
            line.append(QStringLiteral(" <%1>").arg(entry.contact));
        }
        line.append(QStringLiteral(" —— ") + statusLabel(entry.status));
        if (entry.status == SignStepStatus::Signed && entry.signedAt.isValid()) {
            line.append(QStringLiteral("（") + toIso(entry.signedAt) + QStringLiteral("）"));
        }
        if (entry.status == SignStepStatus::Rejected && !entry.rejectReason.isEmpty()) {
            line.append(QStringLiteral("：") + entry.rejectReason);
        }
        lines.append(line);
    }

    if (!events.isEmpty()) {
        lines.append(QString());
        lines.append(QStringLiteral("签署记录："));
        for (const WorkflowEvent& event : events) {
            lines.append(QStringLiteral("  %1  %2  %3  %4")
                             .arg(toIso(event.at), event.actor, event.action, event.detail));
        }
    }

    return lines.join(QLatin1Char('\n'));
}

// ---------------------------------------------------------------- 内部 ----

int SignatureWorkflow::findByName(const QString& name) const {
    for (int i = 0; i < m_signatories.size(); ++i) {
        if (m_signatories.at(i).name == name) {
            return i;
        }
    }
    return -1;
}

int SignatureWorkflow::findByOrder(int order) const {
    for (int i = 0; i < m_signatories.size(); ++i) {
        if (m_signatories.at(i).order == order) {
            return i;
        }
    }
    return -1;
}

int SignatureWorkflow::nextFreeOrder() const {
    int highest = 0;
    for (const Signatory& entry : m_signatories) {
        if (entry.order > highest) {
            highest = entry.order;
        }
    }
    return highest + 1;
}

bool SignatureWorkflow::ensureInProgress(QString* error) const {
    if (!m_created) {
        writeError(error, QStringLiteral("流程尚未创建"));
        return false;
    }
    if (m_state == WorkflowState::Draft) {
        writeError(error, QStringLiteral("流程尚未开始"));
        return false;
    }
    if (m_state == WorkflowState::Completed || m_state == WorkflowState::Aborted) {
        writeError(error, QStringLiteral("流程已结束，不能再修改"));
        return false;
    }
    return true;
}

void SignatureWorkflow::appendEvent(const QDateTime& at, const QString& actor, const QString& action,
                                     const QString& detail) {
    WorkflowEvent event;
    event.at = at.isValid() ? at : QDateTime::currentDateTime();
    event.actor = actor;
    event.action = action;
    event.detail = detail;
    // 只追加：这里没有 removeLast / clear 之类的入口，流水一旦写下就不改。
    m_events.append(event);
}

}  // namespace aipdf::pdf
