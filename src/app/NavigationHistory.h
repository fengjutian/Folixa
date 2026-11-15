#pragma once

#include <QPointF>
#include <QVector>

namespace aipdf::app {

// 一个"能回去的地方"：页码 + 缩放 + 页内锚点。
//
// 只存页码是不够的：用户在同一页里缩放到 400% 又滚到页脚，按"后退"如果只回到
// 同一页的顶部，等于什么都没退。
struct ViewState {
    int pageIndex = 0;
    double zoom = 1.0;
    // 页内偏移比例（0..1）：0 是页顶，1 是页底。存比例而不是像素，
    // 换缩放或换窗口大小之后仍然指向同一处。
    QPointF anchor;

    bool operator==(const ViewState& other) const {
        return pageIndex == other.pageIndex && qFuzzyCompare(zoom, other.zoom) &&
               qFuzzyCompare(anchor.x() + 1.0, other.anchor.x() + 1.0) &&
               qFuzzyCompare(anchor.y() + 1.0, other.anchor.y() + 1.0);
    }
    bool operator!=(const ViewState& other) const { return !(*this == other); }
};

// 前进 / 后退历史。
//
// 规则只有两条，但都很关键：
//   1) **连续重复的状态不记**：缩放按钮按十次只该产生一条历史，否则后退要按十次；
//   2) **后退之后产生新动作会截断前进栈**：这是浏览器的行为，也是用户的预期——
//      退回两页再点别处，原来的"前进"就没有意义了。
class NavigationHistory {
public:
    // 回到起点：清空两端，把当前状态作为第一项。
    void reset(const ViewState& current);

    // 记录一次新状态（通常在跳转完成后调用）。
    void record(const ViewState& state);

    bool canGoBack() const { return index_ > 0; }
    bool canGoForward() const { return index_ + 1 < states_.size(); }

    // 后退/前进一格；越界时原样返回当前项。
    ViewState goBack();
    ViewState goForward();

    // 当前所在项（历史为空时返回默认构造）。
    ViewState current() const;

    int size() const { return states_.size(); }
    int index() const { return index_; }

private:
    QVector<ViewState> states_;
    int index_ = -1;
    // 上限：历史不是日志，留太多只会让内存无谓增长。
    static constexpr int kMaxStates = 64;
};

}  // namespace aipdf::app
