#include "app/controllers/PresentationLoop.h"

#include "i18n/Translate.h"

#include <algorithm>

namespace aipdf::app {

int nextPresentationPage(int currentPage, int pageCount, int delta, bool loop) {
    // 没有页：没有可停的地方，一律回 0（调用方拿它当「不动」用）。
    if (pageCount <= 0) return 0;

    const int last = pageCount - 1;
    // 当前页可能越界（调用方的状态没同步好）：先夹到合法区间，
    // 这样返回值一定在 [0, pageCount) 里，不会把窗口翻到一个不存在的页。
    const int base = std::clamp(currentPage, 0, last);
    if (delta == 0) return base;

    // 用 long long 算目标页：delta 可能是任意大的整数，
    // int 相加会溢出，溢出后的正负号是错的，绕回的方向也就错了。
    const long long target = static_cast<long long>(base) + delta;
    if (target < 0 || target > last) {
        if (!loop) return base;  // 到头就停住，不环绕
        const long long period = pageCount;
        long long wrapped = target % period;
        if (wrapped < 0) wrapped += period;  // C++ 的负数取模是负的
        return static_cast<int>(wrapped);
    }
    return static_cast<int>(target);
}

QString presentationLoopTitle() {
    return i18n::tr("aipdf::app::PresentationLoop", "放映（循环）");
}

QString presentationLoopHint(bool loop) {
    if (loop) return i18n::tr("aipdf::app::PresentationLoop", "放映到最后一页后自动回到第一页");
    return i18n::tr("aipdf::app::PresentationLoop", "放映到最后一页后停住");
}

}  // namespace aipdf::app
