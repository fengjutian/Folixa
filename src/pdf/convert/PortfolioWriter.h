#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 包里的一个条目。
struct PortfolioEntry {
    QString name;         // 包内条目名（同时是显示名）
    QByteArray bytes;     // 内容（PDF 或任意附件）
    QString description;  // 说明文字
};

// 写包时带上的文档属性。
struct PortfolioOptions {
    QString title;
    QString author;
    QString subject;
};

// 把若干条目写成 PDF 包（活页夹）。
//
// 做法：catalog 上挂 /Collection（PDF 规范里的「文件包」），每个条目作为
// /Names /EmbeddedFiles 名字树里的一个嵌入文件（Filespec + EmbeddedFile 流）；
// 另外给每个条目配一页封面，catalog 的 /Dests 按条目名指向那一页。
//
// 失败时返回空字节，error 里写中文原因。
//
// 重名的条目直接判失败（不自动改名）：包里的条目靠名字互相指认
// （名字树与 /Dests 都按名字索引），悄悄改名会让「界面上叫 A、包里其实叫 A(2)」
// 这种错位极难查。要自己改名的话，调用方先把名字改好再传进来。
QByteArray writePortfolio(const QVector<PortfolioEntry>& entries,
                           const PortfolioOptions& options,
                           QString* error = nullptr);

// 读回包里的条目。
struct PortfolioReadResult {
    bool ok = false;
    QString title;
    // 作者与主题：写进去时用的是文档属性（/Info），这里原样读回来。
    QString author;
    QString subject;
    // 条目按包里的展示顺序（/Collection /D）排列。
    QVector<PortfolioEntry> entries;
    QString error;
};

// 读一个 PDF 包。不是包（缺少 /Collection）时 ok 为 false，error 里写中文原因。
PortfolioReadResult readPortfolio(const QByteArray& bytes);

}  // namespace aipdf::pdf
