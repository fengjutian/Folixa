#include "app/Language.h"

#include "i18n/Translate.h"

#include <QFileDialog>
#include <QMessageBox>

namespace aipdf::i18n {

// 「安装语言包…」的界面流程：选文件 → 校验 → 复制 → 切语言 → 提示结果。
//
// 为什么单独一个文件：这一段要用 QFileDialog / QMessageBox（Widgets），
// 而语言管理的核心逻辑（校验、加载、切换）只依赖 QtCore。
// 拆开之后核心那部分仍能被 I18nTests 这类只链 Core 的用例覆盖。

bool installLanguagePackInteractively(QWidget* parent) {
    const QString path = QFileDialog::getOpenFileName(
        parent, i18n::tr("aipdf::app::Language", "选择语言包文件"), QString(),
        i18n::tr("aipdf::app::Language", "语言包 (*.ts)"));
    if (path.isEmpty()) {
        return false;  // 用户自己取消了，不是失败。
    }

    const QString code = declaredLanguageOf(path);
    if (code.isEmpty()) {
        QMessageBox::warning(
            parent, i18n::tr("aipdf::app::Language", "安装语言包"),
            i18n::tr("aipdf::app::Language",
                     "%1 读不出语言代码：它可能不是标准的 .ts 翻译目录。")
                .arg(path));
        return false;
    }

    QString error;
    if (!installLanguagePack(path, code, &error)) {
        QMessageBox::warning(parent, i18n::tr("aipdf::app::Language", "安装语言包"),
                             i18n::tr("aipdf::app::Language", "%1 的语言包安装失败：%2")
                                 .arg(code, error));
        return false;
    }

    // 装好了就切过去：用户刚装完就是要用。
    if (!LanguageManager::instance().setPreference(code)) {
        QMessageBox::warning(
            parent, i18n::tr("aipdf::app::Language", "安装语言包"),
            i18n::tr("aipdf::app::Language", "%1 的语言包装好了，但切换失败：%2")
                .arg(code, LanguageManager::instance().lastError()));
        return false;
    }
    QMessageBox::information(
        parent, i18n::tr("aipdf::app::Language", "安装语言包"),
        i18n::tr("aipdf::app::Language", "已安装 %1 的语言包，可以在「设置 → 界面语言」里切换。")
            .arg(code));
    return true;
}

}  // namespace aipdf::i18n
