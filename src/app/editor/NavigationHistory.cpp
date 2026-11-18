#include "app/NavigationHistory.h"

#include <algorithm>

namespace aipdf::app {

void NavigationHistory::reset(const ViewState& current) {
    states_.clear();
    states_.append(current);
    index_ = 0;
}

void NavigationHistory::record(const ViewState& state) {
    if (states_.isEmpty()) {
        reset(state);
        return;
    }
    // 与当前项相同就不记：连续缩放/滚动不该把历史灌满。
    if (states_.at(index_) == state) {
        return;
    }

    // 在历史中间产生新动作：截断前进栈。
    if (index_ + 1 < states_.size()) {
        states_.resize(index_ + 1);
    }
    states_.append(state);
    if (states_.size() > kMaxStates) {
        states_.removeFirst();
    }
    index_ = states_.size() - 1;
}

ViewState NavigationHistory::goBack() {
    if (canGoBack()) {
        --index_;
    }
    return current();
}

ViewState NavigationHistory::goForward() {
    if (canGoForward()) {
        ++index_;
    }
    return current();
}

ViewState NavigationHistory::current() const {
    if (index_ < 0 || index_ >= states_.size()) {
        return ViewState{};
    }
    return states_.at(index_);
}

}  // namespace aipdf::app
