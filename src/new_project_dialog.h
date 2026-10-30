#pragma once

#include <QDialog>

class QLineEdit;

namespace studio {

class NewProjectDialog final : public QDialog {
    Q_OBJECT
public:
    explicit NewProjectDialog(QWidget* parent = nullptr);

    QString projectName() const;
    QString projectLocation() const;
    QString projectDescription() const;
    static QString validateName(const QString& name);
    static QString validateProjectTarget(const QString& target);

private:
    void browseLocation();
    void accept() override;

    QLineEdit* name_ = nullptr;
    QLineEdit* location_ = nullptr;
    QLineEdit* description_ = nullptr;
};

} // namespace studio
