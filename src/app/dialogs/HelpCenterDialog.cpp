#include "app/dialogs/HelpCenterDialog.h"

#include "app/ui/Language.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>

namespace aipdf::app {

// 说明：下面的文案一律写成 i18n::tr("aipdf::app::HelpCenterDialog", …)。
//
// 不用裸 tr()：QObject::tr() 的上下文是类名（"HelpCenterDialog"），
// 和 .ts 里登记的 <context> 对不上，运行时查不到翻译。
// 也不用一个 kContext 常量代替字面量：scripts/check-i18n.py 静态扫的是
// tr(「字面量」, …) 的写法，套一层变量它就看不见这条词条了。

HelpCenterDialog::HelpCenterDialog(QWidget* parent) : QDialog(parent) {
    buildUi();
    connect(&i18n::LanguageManager::instance(), &i18n::LanguageManager::languageChanged,
            this, &HelpCenterDialog::retranslateUi);
}

void HelpCenterDialog::buildUi() {
    setObjectName(QStringLiteral("helpCenterDialog"));
    setModal(true);
    resize(880, 600);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    chapterList_ = new QListWidget(this);
    // objectName 是给测试与自动化用的稳定抓手：界面文案会随语言变，名字不会。
    chapterList_->setObjectName(QStringLiteral("helpCenter.chapters"));
    chapterList_->setMinimumWidth(180);
    chapterList_->setMaximumWidth(280);
    layout->addWidget(chapterList_);

    content_ = new QTextBrowser(this);
    content_->setObjectName(QStringLiteral("helpCenter.content"));
    // 帮助正文里不放外链，别让用户点一下跳出程序。
    content_->setOpenExternalLinks(false);
    layout->addWidget(content_, 1);

    chapters_ = buildChapters();
    for (const Chapter& chapter : chapters_) {
        chapterList_->addItem(chapter.title);
    }
    connect(chapterList_, &QListWidget::currentRowChanged, this, &HelpCenterDialog::showChapter);
    // 选中第一行：右侧一开始就有正文，不是空白页。
    chapterList_->setCurrentRow(0);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->setObjectName(QStringLiteral("helpCenterButtons"));
    texts_.bind([this, buttons] {
        buttons->button(QDialogButtonBox::Close)->setText(
            i18n::tr("aipdf::app::HelpCenterDialog", "关闭"));
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    texts_.bind([this] {
        setWindowTitle(i18n::tr("aipdf::app::HelpCenterDialog", "帮助中心"));
    });
    retranslateUi();
}

void HelpCenterDialog::retranslateUi() {
    texts_.retranslate();
    // 章节表按当前语言重建：列表里的名字和正文一起换掉。
    chapters_ = buildChapters();
    const int rows = chapterList_->count();
    for (int index = 0; index < rows && index < chapters_.size(); ++index) {
        if (QListWidgetItem* item = chapterList_->item(index)) {
            item->setText(chapters_.at(index).title);
        }
    }
    const int row = chapterList_->currentRow();
    showChapter(row >= 0 ? row : 0);
}

QVector<HelpCenterDialog::Chapter> HelpCenterDialog::buildChapters() const {
    using Kind = Block::Kind;
    QVector<Chapter> chapters;

    // ------------------------------------------------------------------
    // 快速上手
    // ------------------------------------------------------------------
    Chapter quickStart;
    quickStart.title = i18n::tr("aipdf::app::HelpCenterDialog", "快速上手");
    quickStart.blocks.push_back({Kind::Heading, quickStart.title});
    quickStart.blocks.push_back({Kind::Lead, i18n::tr(
        "aipdf::app::HelpCenterDialog", "打开、翻页、搜索，三步读起一份 PDF。")});
    quickStart.blocks.push_back({Kind::Paragraph, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "打开文档有三种方式：「文件 → 打开」挑文件、直接把 PDF 拖进窗口，"
        "或在命令行里把文件路径交给程序。")});
    quickStart.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog", "「文件 → 打开」：可一次选多份 PDF，每份开成一个标签页。")});
    quickStart.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog", "拖拽：把 PDF 从资源管理器拖到窗口里即可。")});
    quickStart.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog", "命令行：AiPdfEditor.exe 文件.pdf，直接打开这一份。")});
    quickStart.blocks.push_back({Kind::Paragraph, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "「主页」页签管缩放与旋转，「视图」页签管页面布局：连续滚动、单页、双页。")});
    quickStart.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog", "缩放与旋转在「主页 → 显示」；按住 Ctrl 滚动滚轮也能缩放。")});
    quickStart.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog", "跳页：在「主页 → 导航」输入页码，或点左侧缩略图与书签。")});
    quickStart.blocks.push_back({Kind::Paragraph, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "搜索分两处：左侧「搜索」面板查当前文档，「文档管理 → 检索」查整个目录。"
        "改完按 Ctrl+S 保存，「文件 → 另存为」保留原文件。")});
    chapters.push_back(quickStart);

    // ------------------------------------------------------------------
    // 页面与导航
    // ------------------------------------------------------------------
    Chapter navigation;
    navigation.title = i18n::tr("aipdf::app::HelpCenterDialog", "页面与导航");
    navigation.blocks.push_back({Kind::Heading, navigation.title});
    navigation.blocks.push_back({Kind::Lead, i18n::tr(
        "aipdf::app::HelpCenterDialog", "文档再长，也能直接跳到要看的那一页。")});
    navigation.blocks.push_back({Kind::Paragraph, i18n::tr(
        "aipdf::app::HelpCenterDialog", "左侧导航栏里有四个入口：页面缩略图、书签、表单与索引。")});
    navigation.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog", "页面缩略图：列出整份文档的页面缩略图，点一下就跳过去。")});
    navigation.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog", "书签：文档自带的目录；没有书签可用「编辑 → 导航」生成。")});
    navigation.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog", "表单与索引：按页列出表单字段和可检索的文本块。")});
    navigation.blocks.push_back({Kind::Paragraph, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "页面本身的属性在「页面管理」页签里改：标签、页面操作与装配都在那儿。")});
    navigation.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "「页面管理 → 标签」：给页面加自定义标签（如「待办」「已确认」）。")});
    navigation.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog", "「页面管理 → 页面」：插入、删除、旋转、抽取、重排页面。")});
    navigation.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog", "「页面管理 → 装配」：把多份 PDF 合成一份，或按页码区间抽取。")});
    navigation.blocks.push_back({Kind::Paragraph, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "放映走「主页 → 放映」：全屏逐页播放，可指定起止页；录屏也用这条路径。")});
    chapters.push_back(navigation);

    // ------------------------------------------------------------------
    // 批注与审阅
    // ------------------------------------------------------------------
    Chapter annotation;
    annotation.title = i18n::tr("aipdf::app::HelpCenterDialog", "批注与审阅");
    annotation.blocks.push_back({Kind::Heading, annotation.title});
    annotation.blocks.push_back(
        {Kind::Lead, i18n::tr("aipdf::app::HelpCenterDialog", "批注写在文件里，不会改动原文。")});
    annotation.blocks.push_back({Kind::Paragraph, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "「注释」页签按用途分了组：标记、文本、图形、手写、测量与附件、历史。")});
    annotation.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "「标记」：高亮、下划线、删除线、波浪线，也可以给选区加一条便签。")});
    annotation.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog", "「文本」：在页面上直接输入文字注释。")});
    annotation.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog", "「图形」：矩形、椭圆、直线、箭头等矢量图形，可调线宽与颜色。")});
    annotation.blocks.push_back(
        {Kind::Bullet, i18n::tr("aipdf::app::HelpCenterDialog", "「手写」：用鼠标或触控笔自由涂画。")});
    annotation.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog", "「测量与附件」：量长度与面积，并给批注挂上附件。")});
    annotation.blocks.push_back({Kind::Paragraph, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "「注释 → 审阅」用来逐条过批注：接受、拒绝、评论，并汇总成一份审阅意见；"
        "「共享 → 审阅」则把文档发给协作者，收回的批注并入同一份列表。")});
    annotation.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog", "「注释 → 历史」：撤销与重做批注操作，误删的批注能找回来。")});
    chapters.push_back(annotation);

    // ------------------------------------------------------------------
    // 编辑与安全
    // ------------------------------------------------------------------
    Chapter editing;
    editing.title = i18n::tr("aipdf::app::HelpCenterDialog", "编辑与安全");
    editing.blocks.push_back({Kind::Heading, editing.title});
    editing.blocks.push_back({Kind::Lead, i18n::tr(
        "aipdf::app::HelpCenterDialog", "改内容、加表单、设权限——三件最常用的事。")});
    editing.blocks.push_back({Kind::Paragraph, i18n::tr(
        "aipdf::app::HelpCenterDialog", "「编辑」页签改的是文档内容本身：文字、链接、附件与导航结构。")});
    editing.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog", "「编辑 → 内容」：改写正文文字，支持重排与写回。")});
    editing.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog", "「编辑 → 文本工具」：批量查找替换、抽取文字。")});
    editing.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog", "「编辑 → 链接」：加内部跳转与外部超链接。")});
    editing.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog", "「编辑 → 附件」：把文件作为附件放进 PDF。")});
    editing.blocks.push_back({Kind::Paragraph, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "「表单」页签建域与填表：「表单 → 识别」从现有 PDF 里识别字段，"
        "「表单 → 建域」手工加字段，「表单 → 数据」做数据合并。")});
    editing.blocks.push_back({Kind::Paragraph, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "「保护 → 加密与权限」设置打开口令与权限位（打印、复制、批注、填表等）。")});
    editing.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "已加密的文档要改权限，用「权限管理」面板：它用所有者口令把文档解开、"
        "再按新的权限位重新加密（PDF 2.0 / AES-256 下没法原地改权限位）。")});
    editing.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "「保护 → 清理隐藏信息」删除元数据、脚本与嵌入文件；"
        "「保护 → 密文遮盖」把选区下层内容永久抹掉并烧入遮盖。")});
    editing.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "「保护 → 打开安全文档」在独立进程里打开 PDF：主进程不接触文件内容，"
        "表单填写、批注与保存都由那个进程完成。")});
    chapters.push_back(editing);

    // ------------------------------------------------------------------
    // 常见问题
    // ------------------------------------------------------------------
    Chapter faq;
    faq.title = i18n::tr("aipdf::app::HelpCenterDialog", "常见问题");
    faq.blocks.push_back({Kind::Heading, faq.title});
    faq.blocks.push_back(
        {Kind::Lead, i18n::tr("aipdf::app::HelpCenterDialog", "打不开、加密了、放不出来——先看这几条。")});
    faq.blocks.push_back({Kind::Paragraph, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "PDF 打不开或提示已损坏：文件可能正被别的程序占用，先关掉其它程序再试。"
        "仍然失败，多半是文件本身损坏，可以用「文档管理 → 批处理」跑一遍报告定位问题。")});
    faq.blocks.push_back({Kind::Paragraph, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "提示需要口令：这份文档设了打开口令。到「保护 → 加密与权限」里输入口令打开；"
        "口令只能由提供文档的人给出，程序不会绕过它。")});
    faq.blocks.push_back({Kind::Paragraph, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "页内放不出声音或视频：PDF 里的多媒体是注释（声音 / 影片），不是页面内容本身。")});
    faq.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "声音注释：设备要能解码该音频格式，解不了时页面上显示一个占位图标。")});
    faq.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "影片注释：能否播放取决于 PDFium 对该编码的支持，H.264 等常见编码一般没问题。")});
    faq.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "批注上挂的附件：在「注释 → 测量与附件」里加入，播放要靠系统里装好的解码器或播放器。")});
    faq.blocks.push_back({Kind::Paragraph, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "批量转换提示找不到外部程序：OFD、Office 等批量转换依赖系统里已安装的转换器。")});
    faq.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "「文档管理 → OFD 批量转换」要求机器上装有可调用的 OFD 转换器。")});
    faq.blocks.push_back({Kind::Bullet, i18n::tr(
        "aipdf::app::HelpCenterDialog",
        "转 Office 走内置解析器，不依赖外部程序；缺依赖时按提示单独安装即可。")});
    chapters.push_back(faq);

    return chapters;
}

QString HelpCenterDialog::renderBody(const Chapter& chapter) const {
    QString html;
    bool inList = false;
    for (const Block& block : chapter.blocks) {
        // 文案进 HTML 前先转义：译文里出现 < & 也不会把版面撑坏。
        const QString text = block.text.toHtmlEscaped();
        switch (block.kind) {
        case Block::Kind::Heading:
            if (inList) {
                html += QStringLiteral("</ul>");
                inList = false;
            }
            html += QStringLiteral("<h3>%1</h3>").arg(text);
            break;
        case Block::Kind::Lead:
            if (inList) {
                html += QStringLiteral("</ul>");
                inList = false;
            }
            html += QStringLiteral("<p><b>%1</b></p>").arg(text);
            break;
        case Block::Kind::Paragraph:
            if (inList) {
                html += QStringLiteral("</ul>");
                inList = false;
            }
            html += QStringLiteral("<p>%1</p>").arg(text);
            break;
        case Block::Kind::Bullet:
            if (!inList) {
                html += QStringLiteral("<ul>");
                inList = true;
            }
            html += QStringLiteral("<li>%1</li>").arg(text);
            break;
        }
    }
    if (inList) {
        html += QStringLiteral("</ul>");
    }
    return html;
}

void HelpCenterDialog::showChapter(int index) {
    if (index < 0 || index >= chapters_.size()) {
        return;
    }
    content_->setHtml(renderBody(chapters_.at(index)));
    content_->verticalScrollBar()->setValue(0);
}

int HelpCenterDialog::chapterCount() const {
    return static_cast<int>(chapters_.size());
}

QString HelpCenterDialog::chapterTitle(int index) const {
    if (index < 0 || index >= chapters_.size()) {
        return QString();
    }
    return chapters_.at(index).title;
}

QString HelpCenterDialog::chapterBodyHtml(int index) const {
    if (index < 0 || index >= chapters_.size()) {
        return QString();
    }
    return renderBody(chapters_.at(index));
}

}  // namespace aipdf::app
