#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace aipdf::doc {

// 一个批处理步骤：对文件字节做一次变换。
//
// 为什么用「变换函数」而不是枚举：批处理的操作会一直加
// （重命名、拆分、合并、清理隐藏信息、压缩、加水印……），
// 用枚举就得每加一种操作改一遍类型定义，还逼着上层写一大坨 switch。
// 换成函数以后，上层把界面上的一个按钮直接包成一步就行，
// 这一层不需要知道有哪些操作。
//
// 约定：变换失败时往 error 里写原因（中文）并返回空字节；
// 返回空字节但 error 为空不算失败（确实可能产出空文件）。
using BatchTransform = std::function<QByteArray(const QByteArray& input, QString* error)>;

// 批处理里的一步。
struct BatchStep {
    // 步骤名，会出现在报告的错误描述里（例如 "sanitize"）。
    QString name;
    // 变换函数。为空表示这一步跳过：字节原样传给下一步。
    BatchTransform transform;
};

// 单个文件的结果。
struct BatchItemResult {
    // 输入路径。
    QString inputPath;
    // 产出路径。没有产出时为空（例如没有落盘，或这一步就失败了）。
    QString outputPath;
    // 是否成功。
    bool succeeded = false;
    // 是否被跳过：整批没有任何可执行步骤时才算跳过。
    bool skipped = false;
    // 这个文件耗掉的毫秒数。
    qint64 durationMs = 0;
    // 失败原因（中文）。成功或跳过时为空。
    QString error;
    // 输入字节数。
    qint64 inputBytes = 0;
    // 输出字节数：变换产出的字节数。
    // 不落盘（只跑变换）时也照记，否则这种模式下报告里全是 0，看不出任何东西。
    qint64 outputBytes = 0;
};

// 整批的结果。
//
// 这一层是纯数据 + 纯执行：不认识界面，也不放任何文案（文案由 app 层拼）。
// 为什么报告要能序列化：批处理是给脚本用的，跑完要能存盘、能被别的程序读回来。
struct BatchReport {
    // 逐个文件的结果，顺序与输入顺序一致。
    QVector<BatchItemResult> items;
    // 整批耗掉的毫秒数。
    qint64 totalDurationMs = 0;
    // 成功 / 失败 / 跳过的条数。
    //
    // 这三个计数由条目推导而来，不单独保存：
    // 一旦两份数字不一致，界面上「共 10 个，成功 8 个」就会和列表对不上。
    int succeededCount = 0;
    int failedCount = 0;
    int skippedCount = 0;
    // 是否中途停下（continueOnError=false 且遇到失败时为 true）。
    bool aborted = false;

    // 序列化成 JSON：缩进 2 空格，UTF-8，中文不转义（转义了就没法给人看）。
    QByteArray toJson() const;

    // 从 JSON 读回。
    //
    // 解析失败、字段缺失、类型不对，一律返回 false 并写明原因：
    // 这里绝不能把坏 JSON 吞成一份空报告 —— 那会让调用方以为「批处理跑完了，
    // 一个文件都没处理」，而真相是报告根本没读进来。
    static bool fromJson(const QByteArray& json, BatchReport* report, QString* error = nullptr);
};

// 跑一批。
//
// inputPaths 是要处理的文件；outputPaths 与它一一对应，
// 少给的部分按输入文件名兜底（只在有输出目录时才有意义）。
//
// steps 依次作用在每个文件上：某一步失败，这个文件就算失败，后面的步骤不再执行。
// 默认 continueOnError=true：单个文件失败不拖垮整批，失败原因写进报告。
// continueOnError=false 时遇到第一个失败就停下，后面的文件不再处理，
// 报告的 aborted 置为 true。
//
// outputDirectory 为空表示「不落盘、只跑变换」：变换照跑，字节数照记，但不写任何文件。
// 非空时产出写到这个目录下（目录不存在会自动创建），
// outputPaths 里给的是绝对路径就直接用那个路径，给相对路径就拼在输出目录下。
BatchReport runBatch(const QStringList& inputPaths,
                     const QStringList& outputPaths,
                     const QVector<BatchStep>& steps,
                     bool continueOnError = true,
                     const QString& outputDirectory = QString());

}  // namespace aipdf::doc
