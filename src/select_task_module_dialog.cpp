#include "select_task_module_dialog.h"

#include "task_module_catalog.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace studio {

SelectTaskModuleDialog::SelectTaskModuleDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Select Task Module"));
    setMinimumSize(680, 360);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("Choose the task semantics for the new node. The task module cannot be changed after creation."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    modules_ = new QTableWidget(this);
    modules_->setObjectName("taskModuleTable");
    modules_->setColumnCount(4);
    modules_->setHorizontalHeaderLabels(
        {tr("Module"), tr("ID"), tr("Purpose"), tr("Availability")});
    modules_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    modules_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    modules_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    modules_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    modules_->verticalHeader()->hide();
    modules_->setSelectionBehavior(QAbstractItemView::SelectRows);
    modules_->setSelectionMode(QAbstractItemView::SingleSelection);
    modules_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    modules_->setRowCount(static_cast<int>(task_modules.size()));
    for (int row = 0; row < static_cast<int>(task_modules.size()); ++row) {
        const auto& module = task_modules[static_cast<size_t>(row)];
        auto* name = new QTableWidgetItem(tr(module.display_name));
        name->setData(Qt::UserRole, QString::fromLatin1(module.id));
        auto* id = new QTableWidgetItem(QString::fromLatin1(module.id));
        auto* purpose = new QTableWidgetItem(tr(module.description));
        auto* availability = new QTableWidgetItem(
            module.available ? tr("Available") : tr("Unavailable: %1").arg(tr(module.unavailable_reason)));
        for (auto* item : {name, id, purpose, availability}) {
            if (!module.available) item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            modules_->setItem(row, item == name ? 0 : item == id ? 1 : item == purpose ? 2 : 3, item);
        }
    }
    modules_->setCurrentCell(-1, -1);
    modules_->clearSelection();
    layout->addWidget(modules_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    buttons->setObjectName("taskModuleButtons");
    create_ = buttons->addButton(tr("Create Task"), QDialogButtonBox::AcceptRole);
    create_->setObjectName("createTaskModuleButton");
    create_->setEnabled(false);
    layout->addWidget(buttons);

    connect(modules_, &QTableWidget::itemSelectionChanged,
            this, [this] { update_create_state(); });
    connect(modules_, &QTableWidget::cellDoubleClicked,
            this, [this](int, int) { create_selected(); });
    connect(create_, &QPushButton::clicked, this, [this] { create_selected(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString SelectTaskModuleDialog::selected_module_id() const {
    const int row = modules_->currentRow();
    if (row < 0 || row >= static_cast<int>(task_modules.size())) return {};
    const auto& module = task_modules[static_cast<size_t>(row)];
    return module.available ? QString::fromLatin1(module.id) : QString{};
}

void SelectTaskModuleDialog::update_create_state() {
    create_->setEnabled(!selected_module_id().isEmpty());
}

void SelectTaskModuleDialog::create_selected() {
    if (!selected_module_id().isEmpty()) accept();
}

} // namespace studio
