#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

namespace studio {

class SaveProjectAsDialog final : public QDialog {
public:
    explicit SaveProjectAsDialog(
        const QString& current_name,
        const QString& source_root,
        QWidget* parent = nullptr);

    QString project_name() const;
    QString parent_directory() const;
    QString final_path() const;

private:
    void browse();
    void refresh_validation();
    void accept() override;

    QString source_root_;
    QLineEdit* name_ = nullptr;
    QLineEdit* location_ = nullptr;
    QLabel* final_path_ = nullptr;
    QLabel* validation_ = nullptr;
    QPushButton* save_ = nullptr;
};

} // namespace studio
