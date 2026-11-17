#pragma once

#include <QByteArray>
#include <QString>

#include <memory>

class QFile;

namespace aipdf::pdf {

// 文档字节的来源。
//
// 为什么要有这个概念：打开一份大文件时，把整份文件 readAll 进内存意味着
// 常驻内存与文件同量级——1 GB 的文件就是 1 GB 的常驻，K6 的峰值内存预算
// 直接被文件大小绑架，打开耗时也被读盘速度钉死。
// 因此「按路径打开」只把文件留在磁盘上，由 PDFium 按需读块；
// 只有调用方手里本来就有字节（校验刚写出来的文件、重放恢复日志）才走内存字节。
enum class DocumentSourceKind {
    FilePath,   // 文件留在磁盘上，按需读块。内存占用与文件大小无关。
    MemoryBytes // 整份字节已经在调用方手里。
};

// 文档字节的读取器：PDFium 的 FPDF_FILEACCESS 回调用它取数据。
//
// 两种模式共用同一个接口，PDFium 那边不用区分：
//   * FilePath   —— 只记住路径，按需 seek + read；内存占用与文件大小无关。
//   * MemoryBytes—— 与调用方共享同一块缓冲（QByteArray 隐式共享，不复制数据）。
class DocumentReader {
public:
    DocumentReader() = default;
    ~DocumentReader();
    DocumentReader(const DocumentReader&) = delete;
    DocumentReader& operator=(const DocumentReader&) = delete;

    // 按路径打开：只记住路径，不读内容。文件不存在或打不开时返回空指针，
    // 失败原因（操作系统原话）写在 error 里。
    static std::shared_ptr<DocumentReader> openPath(const QString& filePath,
                                                    QString* error = nullptr);
    // 从内存字节：与调用方共享同一块缓冲，不复制数据。
    static std::shared_ptr<DocumentReader> fromBytes(const QByteArray& bytes);

    DocumentSourceKind kind() const { return kind_; }
    // 文档总长度（字节）。
    qint64 size() const { return size_; }
    // 读一段：[position, position + length)。越界或读失败返回 false。
    bool read(qint64 position, char* buffer, qint64 length);
    // 累计从磁盘读了多少字节（内存字节模式恒为 0）。
    //
    // 这是「有没有把整份文件读进内存」的直接证据：
    // 按路径打开大文件时它应当远小于文件大小；
    // 整份读进内存的旧做法走的是内存字节模式，这个数是 0。
    qint64 bytesReadFromDisk() const { return bytesReadFromDisk_; }
    // 内存字节模式下的原始字节；文件模式返回空数组。
    const QByteArray& bytes() const { return bytes_; }
    const QString& filePath() const { return filePath_; }
    // 是否把整份文档留在了内存里。
    //
    // 内存字节模式恒为 true；文件模式恒为 false（文件在磁盘上，内存里只有按需读的块）。
    bool holdsWholeFileInMemory() const { return kind_ == DocumentSourceKind::MemoryBytes; }

private:
    DocumentSourceKind kind_ = DocumentSourceKind::FilePath;
    QString filePath_;
    QByteArray bytes_;
    qint64 size_ = 0;
    qint64 bytesReadFromDisk_ = 0;
    std::unique_ptr<QFile> file_;
};

// 在文档字节里找一段标记（用来判断有没有 /XFA）。
//
// 文件模式按块扫描：内存占用有上限（不把整份文件读进来），
// 代价是要顺序读完整个文件——这是不把文件留在内存里必须付的代价。
// 扫描用的是另一个文件句柄，不经过 DocumentReader，
// 因此这些字节不计入 bytesReadFromDisk()（那个计数只管交给 PDFium 的数据）。
bool documentContainsMarker(const DocumentReader& reader, const QByteArray& marker);

}  // namespace aipdf::pdf
