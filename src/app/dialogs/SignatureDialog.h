#pragma once

#include <QByteArray>
#include <QDialog>
#include <QString>

class QListWidget;
class QPushButton;

namespace aipdf::app {

struct StoredSignature {
    QString name;
    QByteArray image;

    bool isValid() const { return !name.trimmed().isEmpty() && !image.isEmpty(); }
};

// 本地签章库。签章图片只保存在用户的应用数据目录，不上传、不写入注册表。
class SignatureDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SignatureDialog(bool selectionMode, QWidget* parent = nullptr);

    StoredSignature selectedSignature() const;

private:
    void reload();
    void importImage();
    void createHandwritten();
    void removeSelected();
    void acceptSelected();
    void updateActions();

    bool selectionMode_ = false;
    QListWidget* list_ = nullptr;
    QPushButton* useButton_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    StoredSignature selected_;
};

}  // namespace aipdf::app
