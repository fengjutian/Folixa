#pragma once

#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

#include "pdf/forms/FormFieldWriter.h"   // 为了复用 FormFieldKind

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

    // 内置模板库：随程序提供的常用表单模板。
    //
    // 为什么要有内置模板：用户第一次用「套用模板」时一个模板都没有，
    // 只能先手画一遍再存 —— 门槛高得离谱。
    // 内置模板是**只读**的：套用后可以另存为自己的模板。
    static QVector<FormTemplate> builtInTemplates();

    // 按名字找内置模板；找到写进 out 并返回 true。
    //
    // 为什么用出参而不是返回指针：内置模板每次调用都是**现造**的，
    // 返回指针的话指针指向的是函数返回后就没了的临时对象 —— 用它就是野指针。
    static bool builtInTemplate(const QString& name, FormTemplate* out);

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
