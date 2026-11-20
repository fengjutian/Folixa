#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

namespace aipdf::app {

// 一次录制的种类。
//
// 为什么用固定的三种而不是随手写字符串：界面上要按种类分组显示，
// 索引文件也要按种类过滤，字符串散着写迟早出现「screen」和「Screen」两种写法。
enum class RecordingKind {
    Screen,      // 屏幕 / 窗口录制（视频）
    Presentation,  // PDF 放映录制（视频）
    Audio,       // 声音录制（音频）
    Screenshot,  // 截图（图片）
};

// 列表里的一条录制。
struct RecordingEntry {
    // 稳定标识：同一个文件重复登记时用它去重。
    QString id;
    RecordingKind kind = RecordingKind::Screen;
    // 文件的绝对路径。
    QString path;
    QDateTime createdAt;
    // 文件大小（字节）。
    qint64 sizeBytes = 0;
    // 时长（毫秒）。图片为 0。
    int durationMs = 0;
    // 视频的宽高（像素）。非视频为 0。
    int width = 0;
    int height = 0;

    bool isVideo() const {
        return kind == RecordingKind::Screen || kind == RecordingKind::Presentation;
    }
    // 界面上显示的名字（文件名去掉扩展名）。
    QString displayName() const;
};

// 录制中心：录制产物的清单。
//
// 为什么自己维护一份索引而不是每次去扫文件夹：
//   * 扫描只能看出「有哪些文件」，看不出「哪次录制对应哪页 PDF」；
//   * 用户把文件挪走或删了，索引要能对得上（文件没了就从清单里去掉）；
//   * 索引里存着时长与尺寸，这些信息扫文件夹算不出来。
// 索引是一份 JSON，放在应用数据目录；它是**缓存**：文件被用户从别处删掉时，
// scanDirectory() 会把对不上的条目清掉。
class RecordingLibrary {
public:
    explicit RecordingLibrary(const QString& indexPath);

    // 读索引。文件不存在不是错误（当作空清单）。
    bool load(QString* error = nullptr);
    // 写索引。写失败时清单仍然在内存里，调用方要照实报错。
    bool save(QString* error = nullptr);

    // 清单内容，按录制时间**倒序**（新的在前）。
    QVector<RecordingEntry> entries() const;

    // 清单里能当音视频附件用的条目（视频 + 音频，截图不算），同样按时间倒序。
    //
    // 为什么单独一个方法：往 PDF 里挂附件时只该挑音视频，
    // 「筛选规则」放在这里才能被单元测试钉住；界面上自己再写一遍筛选条件，
    // 迟早和这里对不上。
    QVector<RecordingEntry> mediaEntries() const;

    // 登记一条录制并落盘。同一路径重复登记时更新原条目而不是追加两条。
    bool add(const RecordingEntry& entry, QString* error = nullptr);

    // 移除一条。deleteFile 为 true 时连同磁盘上的文件一起删。
    bool remove(const QString& id, bool deleteFile, QString* error = nullptr);

    // 扫描录制目录，把索引里没有的文件补进来、把文件已经不存在的条目去掉。
    //
    // 返回本次变化后的条数；error 里写明删掉了哪几条（文件已不存在）。
    int syncWithDirectory(QString* error = nullptr);

    // 这个路径是否已经在清单里。
    bool contains(const QString& path) const;

    // 默认的录制目录与索引位置。
    static QString defaultDirectory();
    static QString defaultIndexPath();
    // 按种类与时间生成文件名（不含目录）。
    static QString fileNameFor(RecordingKind kind, const QDateTime& at);
    // 种类的中文名。
    static QString kindLabel(RecordingKind kind);
    // 从文件名反推种类（认不出来时返回 false）。
    static bool kindFromFileName(const QString& fileName, RecordingKind* kind);

private:
    QString indexPath_;
    QVector<RecordingEntry> entries_;
};

}  // namespace aipdf::app
