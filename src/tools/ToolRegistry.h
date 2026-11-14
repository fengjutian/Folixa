#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace aipdf::tools {

// 工具的权限分类。
//
// 这一层是「AI 能做什么」的唯一入口：模型永远拿不到执行权，
// 它只能产出提案，提案要过校验，执行要用户点确认。
enum class ToolPermission {
    ReadOnly,     // 只读：可以随便调，不改文档
    ProposeOnly,  // 提案：只能生成「打算做什么」，由用户确认后才执行
    Forbidden,    // 禁止：出现在提案里直接判失败
};

struct ToolSpec {
    QString id;                          // 稳定名字，模型只能用它
    ToolPermission permission = ToolPermission::Forbidden;
    QString displayName;                 // 界面与预览里给人看的名字
    QJsonObject schema;                  // 参数 schema（这一版支持的子集见 ProposalValidator）
};

// 工具注册表。
//
// 没有「执行」接口，这是故意的：注册表只描述工具，执行路径在命令系统里，
// 由用户在预览对话框点确认后才走。想加一个能改文档的工具，
// 就必须同时加它的 schema 与预览文案——绕不过去。
class ToolRegistry {
public:
    static const ToolRegistry& instance();

    // 查工具；未知工具返回 nullptr（调用方必须当成错误，不能当成「无操作」）。
    const ToolSpec* find(const QString& id) const;

    QVector<ToolSpec> tools() const { return tools_; }

    // 明确禁止的工具名。
    //
    // 为什么要单列：模型可能从训练数据里「学会」调用 shell/file 这类名字，
    // 如果只是「查不到」，错误信息会含糊；单独判禁止，提示更清楚，测试也更直接。
    static bool isForbidden(const QString& id);

private:
    ToolRegistry();
    QVector<ToolSpec> tools_;
};

}  // namespace aipdf::tools
