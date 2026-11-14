#include "tools/ToolRegistry.h"

#include <QJsonArray>
#include <QStringList>

namespace aipdf::tools {

namespace {

QJsonObject field(const QString& type) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), type);
    return object;
}

QJsonObject intField(int minimum, int maximum) {
    QJsonObject object = field(QStringLiteral("integer"));
    object.insert(QStringLiteral("minimum"), minimum);
    object.insert(QStringLiteral("maximum"), maximum);
    return object;
}

QJsonObject pageArrayField() {
    QJsonObject items = field(QStringLiteral("integer"));
    items.insert(QStringLiteral("minimum"), 1);
    QJsonObject array = field(QStringLiteral("array"));
    array.insert(QStringLiteral("items"), items);
    array.insert(QStringLiteral("minItems"), 1);
    array.insert(QStringLiteral("maxItems"), 50);
    return array;
}

QJsonObject objectSchema(const QJsonObject& properties, const QStringList& required) {
    QJsonObject schema = field(QStringLiteral("object"));
    schema.insert(QStringLiteral("properties"), properties);
    QJsonArray requiredArray;
    for (const QString& name : required) {
        requiredArray.append(name);
    }
    schema.insert(QStringLiteral("required"), requiredArray);
    return schema;
}

}  // namespace

bool ToolRegistry::isForbidden(const QString& id) {
    static const QStringList forbidden{
        QStringLiteral("shell.exec"),     QStringLiteral("process.start"),
        QStringLiteral("file.read"),      QStringLiteral("file.write"),
        QStringLiteral("file.delete"),    QStringLiteral("net.download"),
        QStringLiteral("net.request"),    QStringLiteral("script.eval"),
    };
    return forbidden.contains(id, Qt::CaseInsensitive);
}

ToolRegistry::ToolRegistry() {
    ToolSpec listBlank;
    listBlank.id = QStringLiteral("doc.list_blank_pages");
    listBlank.permission = ToolPermission::ReadOnly;
    listBlank.displayName = QStringLiteral("找出空白页");
    listBlank.schema = objectSchema(
        {{QStringLiteral("minChars"), intField(0, 100)}}, {});
    tools_.append(listBlank);

    ToolSpec deletePages;
    deletePages.id = QStringLiteral("doc.delete_pages");
    deletePages.permission = ToolPermission::ProposeOnly;
    deletePages.displayName = QStringLiteral("删除页面");
    deletePages.schema = objectSchema({{QStringLiteral("pages"), pageArrayField()},
                                       {QStringLiteral("reason"), field(QStringLiteral("string"))}},
                                      {QStringLiteral("pages")});
    tools_.append(deletePages);

    ToolSpec insertPage;
    insertPage.id = QStringLiteral("doc.insert_blank_page");
    insertPage.permission = ToolPermission::ProposeOnly;
    insertPage.displayName = QStringLiteral("插入空白页");
    insertPage.schema = objectSchema({{QStringLiteral("index"), intField(1, 100000)},
                                      {QStringLiteral("width"), field(QStringLiteral("number"))},
                                      {QStringLiteral("height"), field(QStringLiteral("number"))}},
                                     {QStringLiteral("index")});
    tools_.append(insertPage);

    ToolSpec movePage;
    movePage.id = QStringLiteral("doc.move_page");
    movePage.permission = ToolPermission::ProposeOnly;
    movePage.displayName = QStringLiteral("移动页面");
    movePage.schema = objectSchema({{QStringLiteral("from"), intField(1, 100000)},
                                    {QStringLiteral("to"), intField(1, 100000)}},
                                   {QStringLiteral("from"), QStringLiteral("to")});
    tools_.append(movePage);

    ToolSpec addNote;
    addNote.id = QStringLiteral("annotation.add_note");
    addNote.permission = ToolPermission::ProposeOnly;
    addNote.displayName = QStringLiteral("添加批注草稿");
    QJsonObject rectItems = field(QStringLiteral("number"));
    QJsonObject rectArray = field(QStringLiteral("array"));
    rectArray.insert(QStringLiteral("items"), rectItems);
    rectArray.insert(QStringLiteral("minItems"), 4);
    rectArray.insert(QStringLiteral("maxItems"), 4);
    addNote.schema = objectSchema({{QStringLiteral("page"), intField(1, 100000)},
                                   {QStringLiteral("rect"), rectArray},
                                   {QStringLiteral("text"), field(QStringLiteral("string"))}},
                                  {QStringLiteral("page"), QStringLiteral("rect"), QStringLiteral("text")});
    tools_.append(addNote);
}

const ToolRegistry& ToolRegistry::instance() {
    static const ToolRegistry registry;
    return registry;
}

const ToolSpec* ToolRegistry::find(const QString& id) const {
    for (const ToolSpec& tool : tools_) {
        if (tool.id == id) {
            return &tool;
        }
    }
    return nullptr;
}

}  // namespace aipdf::tools
