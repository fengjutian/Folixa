#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 一位签署人在流程中的状态。
//
// 为什么不用「bool 签了没签」：拒签与跳过是两种不同的结论，
// 报告里必须能分开统计，事后追责时也必须能分开看。
// 布尔量表达不了三态，所以这里用枚举。
enum class SignStepStatus {
    Pending,   // 还没处理
    Signed,    // 已签署
    Rejected,  // 已拒签（rejectReason 有效）
    Skipped,   // 已跳过（主动放行，不算反对）
};

// 一位签署人。
struct Signatory {
    QString name;     // 姓名；流程内唯一，定位签署人靠它
    QString contact;  // 邮箱/手机，可空
    int order = 0;    // 签署顺序，从 1 开始；0 = 未指定（自动排到末尾）
    SignStepStatus status = SignStepStatus::Pending;
    QDateTime signedAt;    // 仅 status == Signed 时有效
    QString rejectReason;  // 仅 status == Rejected 时有效
};

// 流程状态机：
//
//   Draft --startSigning--> InProgress --completeWorkflow--> Completed
//   Draft / InProgress --abortWorkflow--> Aborted
//
// Completed 与 Aborted 都是终态：终态之后任何修改都必须被拒绝。
// 为什么这么严：签署记录是给事后追责用的流水，一旦流程结束后还能改，
// 这条流水就不再可信了。
enum class WorkflowState { Draft, InProgress, Completed, Aborted };

// 签署记录里的一条事件（append-only 审计流水）。
//
// action 是机器可读的动作码，取值固定：
//
//   created          创建流程
//   signatory_added  添加签署人
//   signatory_removed 移除签署人（只可能发生在草稿阶段）
//   started          开始签署
//   signed           签署完成
//   rejected         拒签
//   skipped          跳过
//   completed        全部签署人处理完毕，流程完成
//   aborted          流程中止
//
// 为什么 action 码与 detail 两份文字都要：上层要按动作筛选与统计
// （机器读 action），又要在列表里给人看一句说明（人读 detail）。
// 只留一句中文就没法统计，只留动作码则没人看得懂发生了什么。
struct WorkflowEvent {
    QDateTime at;
    QString actor;   // 谁做的：发起人 / 签署人姓名
    QString action;  // 机器可读动作码，见上表
    QString detail;  // 人可读说明
};

// 多方签署的流程状态机 + 签署报告 + 签署记录。
//
// 这一层是纯数据层：不碰 PDF 字节、不碰页面坐标、不碰界面，
// 因此既不依赖 PDFium 也不依赖 qpdf，专项单测可以独立编译运行。
//
// 领域层不放 tr()：失败原因一律用中文常量写清楚「为什么不行」，
// 上层要国际化时自己再翻一层。
//
// 几条刻意的取舍：
//
// 1. 顺序（order）只决定「显示与统计的次序」，**不**当作硬闸门：
//    真实业务里多方签署经常是并行的（张三李四同时签），
//    强行拦下「顺序不对」的签署只会把功能做成摆设。
//    需要「下一个该谁签」时用 nextPendingSignatory()。
// 2. 事件流水只追加，不删不改：没有「删除记录」的接口，
//    也没有就地修改事件的接口。
// 3. 拒绝原因必须写清楚还差谁（completeWorkflow 失败时列出未处理的姓名），
//    否则调用方只能自己去猜是哪一步卡住了。
class SignatureWorkflow {
public:
    // 签署报告：结构化字段 + 一段人可读文本。
    //
    // 为什么同时给两种：上层面板要按字段渲染表格与徽标，
    // 而导出/打印需要一段能直接贴进邮件或工单的文本。
    struct Report {
        QString id;
        QString title;
        QString initiator;
        WorkflowState state = WorkflowState::Draft;
        QDateTime createdAt;
        QDateTime updatedAt;
        QVector<Signatory> signatories;  // 按 order 升序
        QVector<WorkflowEvent> events;   // 追加顺序
        int signedCount = 0;
        int rejectedCount = 0;
        int skippedCount = 0;
        int pendingCount = 0;

        // 人可读报告（纯文本，UTF-8）。
        QString toPlainText() const;
    };

    // 创建一个签署流程，返回流程编号（形如 WF-1A2B3C4D）。
    // 失败返回空串并写明原因；同一个实例上只能创建一次。
    QString createWorkflow(const QString& title, const QString& initiator, QString* error = nullptr);

    // 添加签署人。失败时写明原因（空姓名 / 姓名重复 / 顺序重复）。
    // order 传 0 表示「未指定顺序」，自动排到当前最后一位。
    void addSignatory(const Signatory& s, QString* error = nullptr);

    // 删除一位签署人。失败时写明原因并返回 false。
    //
    // 只在「流程还是草稿」时允许：名单一旦开始签署，就是「谁被要求签过」的
    // 凭据，中途删人会让签署记录对不上——流水里提到的人不在名单上，
    // 事后追责时这条流水就不可信了。
    //
    // 草稿阶段删人不会留下任何矛盾（还没人签、也还没发出去），
    // 而「加错一个人」是最常见的事故，没有这个口子很难用。
    bool removeSignatory(const QString& name, QString* error = nullptr);

    // Draft → InProgress。没有签署人时拒绝（没人签的流程没有意义）。
    bool startSigning(QString* error = nullptr);

    // 记录一次签署。at 无效时按「此刻」记（调用方忘了传时间不该让流程卡住）。
    bool recordSigned(const QString& name, const QDateTime& at, QString* error = nullptr);

    // 记录一次拒签。理由不能为空：拒签是流程里最需要留痕的一步。
    bool recordRejected(const QString& name, const QString& reason, const QDateTime& at, QString* error = nullptr);

    // 跳过一位签署人（主动放行，不算反对）。
    bool skipSignatory(const QString& name, QString* error = nullptr);

    // 所有签署人都不再是 Pending 时才能完成；否则失败并列出还差谁。
    bool completeWorkflow(QString* error = nullptr);

    // 中止流程。理由不能为空。
    bool abortWorkflow(const QString& reason, QString* error = nullptr);

    WorkflowState state() const;

    // 签署人列表，按 order 升序。
    QVector<Signatory> signatories() const;

    // 签署记录，顺序即追加顺序（只增不改）。
    QVector<WorkflowEvent> events() const;

    // 生成签署报告。
    Report buildReport() const;

    QString id() const;
    QString title() const;
    QString initiator() const;

    // 下一个待处理的签署人（order 最小的 Pending）；没有则返回空串。
    QString nextPendingSignatory() const;

private:
    int findByName(const QString& name) const;
    int findByOrder(int order) const;
    int nextFreeOrder() const;

    // 校验「当前处于 InProgress」，否则写明原因并返回 false。
    bool ensureInProgress(QString* error) const;

    void appendEvent(const QDateTime& at, const QString& actor, const QString& action, const QString& detail);

    bool m_created = false;
    QString m_id;
    QString m_title;
    QString m_initiator;
    WorkflowState m_state = WorkflowState::Draft;
    QVector<Signatory> m_signatories;  // 始终按 order 升序
    QVector<WorkflowEvent> m_events;   // 只追加
    QDateTime m_createdAt;
};

}  // namespace aipdf::pdf
