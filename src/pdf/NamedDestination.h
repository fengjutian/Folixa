#pragma once

#include <QByteArray>
#include <QPointF>
#include <QString>
#include <QVector>

namespace aipdf::pdf {

// 一个命名目标（named destination）：给「某一页的某个位置」起个名字。
//
// 名字是用户自己起的（「封面」「第一章」「附录A」）。写进 PDF 之后别的阅读器、
// 别的工具也认，所以名字按 UTF-8 存进 PDF 的名字对象里。
struct NamedDestination {
    // 目标名。写入时作为 PDF 名字对象的键。
    QString name;
    // 目标所在页，0 基页号。读回时若目标页解析不出来则为 -1。
    int pageIndex = -1;
    // 落点：页面 user space 里的坐标（PDF 的 y 向上，原点在左下角）。
    QPointF position = {};
    // 缩放系数；0 表示「不指定缩放」
    //（PDF 的 /XYZ 里 zoom=0 的含义就是保持当前缩放）。
    double zoom = 0.0;
    // 为真时写成 /Fit（整页适配），position 与 zoom 被忽略。
    bool fitPage = false;
};

// 把一组命名目标写进 PDF 的 catalog（/Dests 字典），返回新的 PDF 字节。
//
// 语义要点：
//   * 传入的集合就是文档里的**全部**命名目标。写之前会先把
//     /Names /Dests 名称树摘掉——名称树的优先级比 catalog /Dests 高，
//     留着它会让「刚写进去的目标」在别的阅读器里根本看不见。
//   * 传入空集合 = 清空文档里的命名目标。
//   * 失败返回空字节，并通过 error 给出中文原因；
//     绝不会「原样返回却声称已写入」。
QByteArray writeNamedDestinations(const QByteArray& input,
                                   const QVector<NamedDestination>& destinations,
                                   QString* error = nullptr);

// 读回命名目标：catalog 的 /Dests 字典与 /Names /Dests 名称树两种写法都认。
//
// 文档里没有命名目标时返回空列表（这不是错误，error 保持为空）；
// 只有文件本身坏了才返回错误。
QVector<NamedDestination> readNamedDestinations(const QByteArray& input, QString* error = nullptr);

// 按名字查目标所在页（0 基页号）。找不到返回 -1（error 保持为空）；
// 只有文件本身坏了才写 error。
int pageIndexOfNamedDestination(const QByteArray& input, const QString& name, QString* error = nullptr);

}  // namespace aipdf::pdf
