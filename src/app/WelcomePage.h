#pragma once

#include "i18n/Translate.h"

#include <QStringList>
#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace aipdf::app {

// 欢迎页：品牌标识、拖放入口与最近文档。
//
// 这是用户看到的第一个界面，职责只有三件事：说清这是什么产品、
// 让「打开 PDF」成为一眼可见的主操作、把最近读过的文件摆在手边。
// 背景（渐变与棱光）由自己绘制，其余全部是普通 Qt 控件，主题在 Theme.cpp。
class WelcomePage final : public QWidget {
    Q_OBJECT

public:
    explicit WelcomePage(const QStringList& recentFiles, QWidget* parent = nullptr);

    // 最近文档换了就地重建列表：刚打开的文档不该等到下次启动才出现在这里。
    void setRecentFiles(const QStringList& files);

signals:
    void openPdfRequested();
    void recentFileRequested(const QString& path);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QWidget* createRecentCard(const QStringList& files);
    QWidget* createRecentRow(const QString& path, bool isLast);
    // 按当前的 recentFiles_ 重铺列表本体与计数。
    void rebuildRecentCard();
    // 切换语言后重译页面上登记过的常驻文案。
    void retranslateUi();

    // 列表的当前内容：重建时要用，也用来判断「有没有变」。
    QStringList recentFiles_;
    // 列表挂载点：换内容只动这里，标题与间距不动。
    QVBoxLayout* recentLayout_ = nullptr;
    QWidget* recentCardHost_ = nullptr;
    QLabel* recentCount_ = nullptr;

    // 一次设定、长期显示的界面文案：切换语言时就地重译。
    i18n::TextBindings texts_;
};

}  // namespace aipdf::app
