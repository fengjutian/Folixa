#pragma once

#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

#include "pdf/FormFieldWriter.h"   // 为了复用 FormFieldKind

namespace aipdf::pdf {

// 模板里的一个域。
struct FormTemplateField {
    QString name;
    QString toolTip;
    FormFieldKind kind = FormFieldKind::Text;
    // 相对页面尺寸的比例位置（左上角 x/y 与宽/高，取 0..1 的比例），
    // 这样模板套到不同尺寸的页面上也能对齐。
    double relX = 0.0;
    double relY = 0.0;
    double relWidth = 0.2;
    double relHeight = 0.03;
    QStringList options;
    bool readOnly = false;
    bool required = false;
    bool multiline = false;
};

// 一个模板：一组域定义 + 模板名。
struct FormTemplate {
    QString name;
    QVector<FormTemplateField> fields;
};

// 模板仓库：存在本地目录，一个模板一个 JSON 文件。
class FormTemplateStore {
public:
    // rootDir 为空时用 defaultRootDir()（QStandardPaths::AppDataLocation 下的 "form-templates"）。
    explicit FormTemplateStore(const QString& rootDir = QString());

    // 模板名列表，按名称排序。
    QStringList templateNames() const;

    // 保存（同名覆盖）。成功返回 true。
    bool save(const FormTemplate& tmpl, QString* error = nullptr);

    // 读取。模板不存在或文件损坏返回 false 并写 error。
    bool load(const QString& name, FormTemplate* out, QString* error = nullptr) const;

    // 删除。
    bool remove(const QString& name, QString* error = nullptr);

    // 默认根目录。
    static QString defaultRootDir();

private:
    // 模板名对应的标准文件名（<净化后的名字>.json）。
    QString primaryFilePath(const QString& name) const;

    QString m_rootDir;
};

}  // namespace aipdf::pdf
