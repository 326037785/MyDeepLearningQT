#include "modules/task/ui/annotation/shared/annotation_classes_panel.h"
#include "modules/task/ui/annotation/shared/annotation_panels.h"
#include "common/ui/ui_metrics.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <exception>

namespace studio {

AnnotationClassesPanel::AnnotationClassesPanel(const AnnotationClassPanelNames& names, QWidget* parent)
    : QFrame(parent) {
    setObjectName("annotationClassesPanel");
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(ThemeTokens::Space2, ThemeTokens::Space2,
                               ThemeTokens::Space2, ThemeTokens::Space2);
    list_ = annotation_class_list(this, names.list);
    layout->addWidget(list_, 1);
    tools_ = new QHBoxLayout;
    add_ = annotation_icon_button(this, tools_, names.add, "add", tr("Add class"));
    rename_ = annotation_icon_button(this, tools_, names.rename, "rename", tr("Rename class"));
    remove_ = annotation_icon_button(this, tools_, names.remove, "trash", tr("Delete class"));
    layout->addLayout(tools_);
    connect(list_, &QListWidget::currentItemChanged, this, [this] { update_actions(); });
    connect(add_, &QToolButton::clicked, this, [this] { add(); });
    connect(rename_, &QToolButton::clicked, this, [this] { rename(); });
    connect(remove_, &QToolButton::clicked, this, [this] { remove(); });
    update_actions();
}

void AnnotationClassesPanel::set_available(bool available) {
    available_ = available;
    update_actions();
}

std::string AnnotationClassesPanel::selected_id() const {
    const QListWidgetItem* item = list_->currentItem();
    return item ? item->data(Qt::UserRole).toString().toStdString() : std::string{};
}

void AnnotationClassesPanel::update_actions() {
    add_->setEnabled(available_);
    const bool selected = available_ && !selected_id().empty();
    rename_->setEnabled(selected);
    remove_->setEnabled(selected);
}

void AnnotationClassesPanel::add() {
    if (!available_ || !add_class) {
        return;
    }
    const QString name = prompt_annotation_class_name(this, tr("Add class"));
    if (name.isEmpty()) {
        return;
    }
    try {
        add_class(name);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Cannot add class"), QString::fromUtf8(error.what()));
    }
}

void AnnotationClassesPanel::rename() {
    const std::string id = selected_id();
    if (!available_ || id.empty() || !rename_class) {
        return;
    }
    const QString previous = list_->currentItem()->data(NameRole).toString();
    const QString name = prompt_annotation_class_name(this, tr("Rename class"), previous);
    if (name.isEmpty() || name == previous) {
        return;
    }
    try {
        rename_class(id, name);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Cannot rename class"), QString::fromUtf8(error.what()));
    }
}

void AnnotationClassesPanel::remove() {
    const std::string id = selected_id();
    if (!available_ || id.empty() || !remove_class) {
        return;
    }
    try {
        const QString message = removal_message ? removal_message(id) : QString{};
        if (!message.isEmpty() && QMessageBox::question(this, tr("Delete class"), message,
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
            return;
        }
        remove_class(id);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Cannot delete class"), QString::fromUtf8(error.what()));
    }
}

} // namespace studio
