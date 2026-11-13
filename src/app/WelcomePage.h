#pragma once

#include <QStringList>
#include <QWidget>

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

signals:
    void openPdfRequested();
    void recentFileRequested(const QString& path);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QWidget* createRecentCard(const QStringList& files);
    QWidget* createRecentRow(const QString& path, bool isLast);
};

}  // namespace aipdf::app
