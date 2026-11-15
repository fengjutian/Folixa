#pragma once

#include <QCoreApplication>
#include <QString>

#include <functional>
#include <utility>
#include <vector>

namespace aipdf::i18n {

// 非 QObject 代码里的翻译入口。
//
// QObject 派生类用自己的 tr()（上下文自动等于类名），其余代码
// （自由函数、结构体、纯数据层）走这里：上下文显式写成稳定的
// 命名空间 + 类型名，与 .ts 里的 <context><name> 一一对应，
// 这样翻译目录不依赖文件路径，重构搬家也不会丢词条。
inline QString tr(const char* context, const char* sourceText) {
    return QCoreApplication::translate(context, sourceText);
}

// 一次设定、长期显示的界面文案的「应用 + 登记」。
//
// 用法：构造界面时把设置文案的那一行交给 bind()，它立即执行一次；
// 切换语言时 retranslate() 按登记顺序重跑一遍，界面就地变成新语言。
//
//     texts_.bind([this, action] { action->setText(tr("打开 PDF…")); });
//
// 每次刷新都会重新计算的文案（状态栏消息、页数标签、按钮可用性提示）
// 不要登记：它们由自己的 update*() 函数按当前语言重新生成。
class TextBindings {
public:
    void bind(std::function<void()> apply) {
        apply();
        appliers_.push_back(std::move(apply));
    }

    void retranslate() const {
        for (const auto& apply : appliers_) {
            apply();
        }
    }

    bool empty() const { return appliers_.empty(); }
    std::size_t size() const { return appliers_.size(); }

private:
    std::vector<std::function<void()>> appliers_;
};

}  // namespace aipdf::i18n
