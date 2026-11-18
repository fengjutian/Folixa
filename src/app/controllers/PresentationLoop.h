#pragma once

#include <QString>

namespace aipdf::app {

// 放映翻页的纯函数状态机：算出「翻 delta 页之后应该停在哪一页」。
//
// 为什么单独抽成纯函数：放映窗口是 QWidget，测它就得建窗口、跑事件循环；
// 而「到头停住还是绕回第一页」这件事本身没有任何界面依赖。
// 抽出来之后单测直接调函数，不用碰窗口系统，也不用起 PDFium。
//
// 语义：
//   * pageCount <= 0：没有页可翻，返回 0（不动）；
//   * delta == 0：停在当前页；
//   * loop == false：翻过去会越界时停在当前页
//     （放映时最烦的就是最后一页按右键跳回第一页）；
//   * loop == true：越界时绕回另一头——最后一页往后翻回到第一页，
//     第一页往前翻回到最后一页；
//   * currentPage 越界（>= pageCount 或 < 0）时先夹到合法区间，
//     保证返回值一定落在 [0, pageCount) 内。
int nextPresentationPage(int currentPage, int pageCount, int delta, bool loop);

// 循环打开时放映窗口标题用的那一条文案。
//
// 只给「循环打开」这一种情况用：关着循环时窗口标题沿用原来的「放映」
// （那是窗口自己的 tr()，翻译词条已经在目录里了，不要平白多出一条）。
QString presentationLoopTitle();

// 循环开关的说明文案（界面层拼提示用）。
//
// 放在这里是为了让「循环」这件事的措辞只有一处：
// 窗口标题与按钮提示说的是同一件事，不至于各说各话。
QString presentationLoopHint(bool loop);

}  // namespace aipdf::app
