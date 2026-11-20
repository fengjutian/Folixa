#pragma once

#include "i18n/Translate.h"

#include <QDialog>
#include <QString>
#include <QVector>

class QListWidget;
class QTextBrowser;

namespace aipdf::app {

// 帮助中心：**站内**的帮助对话框。
//
// 为什么不再跳外部链接：外部 issue 列表要求用户有网络、能唤起浏览器，
// 而且和程序里已有的功能名对不上号——用户问「怎么合并 PDF」，
// 链接只会把他送到一整个 issue 列表里自己翻。这里把说明直接放进程序：
// 左侧章节列表 + 右侧正文，点开就能读，离线也能用。
//
// 正文是**内置**的：写在代码里，不依赖资源文件、不依赖网络。
//
// i18n：每条文案都走 i18n::tr("aipdf::app::HelpCenterDialog", …) 的**显式上下文**。
// 不用裸 tr()——那会把上下文推成 QObject 的类名（"HelpCenterDialog"），
// 和 .ts 里登记的上下文对不上，运行时查不到翻译。
class HelpCenterDialog final : public QDialog {
    Q_OBJECT

public:
    explicit HelpCenterDialog(QWidget* parent = nullptr);

    // 章节数量。
    int chapterCount() const;

    // 第 index 章的标题。
    QString chapterTitle(int index) const;

    // 第 index 章渲染好的正文 HTML（章节标题的 <h3> 在最前面）。
    //
    // 单独暴露出来有两个用处：测试可以逐章断言「正文非空」，
    // 将来做「从帮助跳到某个功能」时也能按章节号定位。
    QString chapterBodyHtml(int index) const;

private:
    // 正文的最小单位：一节标题 / 一句加粗引导 / 一段话 / 一个列表项。
    //
    // 拆得这么细，是为了让**翻译只看到中文句子**：HTML 标签由代码拼，
    // 翻译的人不用碰标记，也不会把标签译坏。
    struct Block {
        enum class Kind {
            Heading,    // <h3>…</h3>：章节标题（直接用章节名，不另起词条）
            Lead,       // <p><b>…</b></p>：加粗的引导句
            Paragraph,  // <p>…</p>
            Bullet,     // <li>…</li>（相邻的并进同一个 <ul>）
        };

        Kind kind = Kind::Paragraph;
        QString text;
    };

    // 一章：标题（左侧列表里的名字）+ 正文块。
    struct Chapter {
        QString title;
        QVector<Block> blocks;
    };

    void buildUi();
    void retranslateUi();

    // 章节表：每次调用都按**当前语言**重新翻译一遍。
    // 所以切语言时只要重建这张表、再刷新界面即可，不用手工同步两处文案。
    QVector<Chapter> buildChapters() const;

    // 把第 index 章的正文块拼成 HTML。
    QString renderBody(const Chapter& chapter) const;

    // 在右侧显示第 index 章。
    void showChapter(int index);

    QListWidget* chapterList_ = nullptr;
    QTextBrowser* content_ = nullptr;
    QVector<Chapter> chapters_;
    i18n::TextBindings texts_;
};

}  // namespace aipdf::app
