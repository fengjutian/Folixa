#pragma once

#include <QString>

namespace aipdf::ai {

// 文档数据可以离开本机的三种方式（v0.12.0）。
//
// 顺序就是严格程度：越靠前越严格。未知/损坏的配置一律回退到最严格的
// LocalOnly——默认值不能偷偷放宽。
enum class DataMode {
    LocalOnly,       // 本地：文档内容一律不出本机
    SelectedChunks,  // 片段：只送当前页/选区，或整篇问答检索到的几块
    SelectedPages,   // 选定页面：只送用户明确勾选的页
};

// 一次请求的规模，用来判断是否超出该模式允许的上限。
struct DataScope {
    int chunkCount = 0;
    int charCount = 0;
    bool hasExplicitPageSelection = false;
};

// 判定结果：这次能不能走云端，以及给用户看的原因。
struct PolicyDecision {
    bool allowed = false;
    QString reason;  // 拒绝时给用户看的原因；允许时为空
};

// 一次请求最多送多少块、多少字。超出就拒绝，而不是悄悄截断：
// 截断会让「用户以为发出去的和实际发出去的不一样」。
constexpr int kMaxChunksPerRequest = 24;
constexpr int kMaxCharsPerRequest = 12000;

// 模式与配置字符串互转。未知字符串回退到 LocalOnly，并通过 ok 告诉调用方。
QString dataModeId(DataMode mode);
DataMode dataModeFromId(const QString& id, bool* ok = nullptr);

// 不看请求规模、只看模式：这种模式下能不能用云端 Provider。
// ProviderFactory 用它把云端分支短路掉——这是「本地模式发不出去」的机械保证。
bool mayUseCloud(DataMode mode);

// 规则本身：纯函数，可单测，不依赖界面与 IPC。
PolicyDecision evaluate(DataMode mode, const DataScope& scope);

}  // namespace aipdf::ai
