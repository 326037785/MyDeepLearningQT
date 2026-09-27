#pragma once

#include <QDialog>

class QDialogButtonBox;
class QPushButton;
class QTableWidget;

namespace studio {

class SelectTaskModuleDialog final : public QDialog {
public:
    explicit SelectTaskModuleDialog(QWidget* parent = nullptr);

    QString selected_module_id() const;

private:
    void update_create_state();
    void create_selected();

    QTableWidget* modules_ = nullptr;
    QPushButton* create_ = nullptr;
};

} // namespace studio
