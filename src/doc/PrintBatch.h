#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace aipdf::doc {

// 打印队列里的一项。
struct PrintBatchItem {
    // 要打印的文件路径。空路径会被跳过（并记一条 warning）。
    QString filePath;
    // 打印几份。小于 1 的按 1 份算。
    int copies = 1;
    // 页码范围（例如 "1-3,5"）。空表示全部页。
    //
    // 原样保留：这一层不解析页码范围，解析是执行打印那一层的事。
    QString pageRange;
    // 唯一标识，用来在报告里指认这一项。留空会自动生成。
    QString id;
};

// 整批打印的设置。
struct PrintBatchOptions {
    // 打印任务名（界面显示用）。
    QString jobName;
    // true = 合并成一份打印；false = 每份单独打。
    bool combineIntoOne = false;
    // 合并产物路径。combineIntoOne 时必填。
    QString outputPath;
};

// 打印队列的规划结果。
struct PrintBatchPlan {
    // 规划是否成功。失败时 error 有内容。
    bool ok = false;
    // 按份数展开后的队列。
    QVector<PrintBatchItem> expanded;
    // 队列里的任务数。
    int totalJobs = 0;
    // 总页数。
    //
    // 这一层不打开 PDF，读不到真实页数，所以是 -1（未知）：
    // 真正执行打印的那一层拿到文档后可以再回填。
    int totalPages = -1;
    // 失败原因（中文）。
    QString error;
    // 被跳过的项、被夹住的参数等提醒（中文）。
    QStringList warnings;
};

// 规划一批打印任务。
//
// 这一层只排队列，不真的打印：打印要渲染页面、要弹打印对话框，属于界面层。
//
// 约定：
//   - 空队列算失败；
//   - 合并打印必须给输出路径；
//   - 份数小于 1 按 1 份算，超过上限按上限算；
//   - 路径为空的项整项跳过并记 warning，**不让整批失败**；
//   - 展开后每一项的 id 都唯一。
PrintBatchPlan planPrintBatch(const QVector<PrintBatchItem>& items,
                              const PrintBatchOptions& options,
                              QString* error = nullptr);

}  // namespace aipdf::doc
