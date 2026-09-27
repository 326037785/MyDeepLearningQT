#include "save_project_as_dialog.h"

#include "project_path_validator.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace studio {

SaveProjectAsDialog::SaveProjectAsDialog(
    const QString& current_name,
    const QString& source_root,
    QWidget* parent)
    : QDialog(parent), source_root_(source_root)
{
    setWindowTitle(tr("Save Project As"));
    setMinimumWidth(560);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(
        tr("Create an independent copy. Project resources stored inside the current project are copied; external resources remain shared."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* form = new QFormLayout;
    name_ = new QLineEdit(current_name + tr(" Copy"), this);
    name_->setObjectName("saveProjectAsNameEdit");
    form->addRow(tr("Project name"), name_);

    auto* location_row = new QWidget(this);
    auto* location_layout = new QHBoxLayout(location_row);
    location_layout->setContentsMargins(0, 0, 0, 0);
    location_ = new QLineEdit(this);
    location_->setObjectName("saveProjectAsLocationEdit");
    QString initial = QSettings("Jocelin", "DeepLearningStudio")
                          .value("lastProjectDirectory").toString();
    if (initial.isEmpty() && !source_root.isEmpty())
        initial = QFileInfo(source_root).absolutePath();
    location_->setText(initial);
    auto* browse_button = new QPushButton(tr("Browse..."), this);
    browse_button->setObjectName("saveProjectAsBrowseButton");
    location_layout->addWidget(location_, 1);
    location_layout->addWidget(browse_button);
    form->addRow(tr("Location"), location_row);

    final_path_ = new QLabel(this);
    final_path_->setObjectName("saveProjectAsFinalPathLabel");
    final_path_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    final_path_->setWordWrap(true);
    form->addRow(tr("Final path"), final_path_);
    layout->addLayout(form);

    validation_ = new QLabel(this);
    validation_->setObjectName("saveProjectAsValidationLabel");
    validation_->setWordWrap(true);
    layout->addWidget(validation_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->setObjectName("saveProjectAsButtons");
    save_ = buttons->button(QDialogButtonBox::Save);
    layout->addWidget(buttons);

    connect(name_, &QLineEdit::textChanged, this, [this] { refresh_validation(); });
    connect(location_, &QLineEdit::textChanged, this, [this] { refresh_validation(); });
    connect(browse_button, &QPushButton::clicked, this, [this] { browse(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &SaveProjectAsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    refresh_validation();
}

QString SaveProjectAsDialog::project_name() const { return name_->text().trimmed(); }
QString SaveProjectAsDialog::parent_directory() const { return location_->text().trimmed(); }
QString SaveProjectAsDialog::final_path() const {
    return QDir(parent_directory()).absoluteFilePath(project_name());
}

void SaveProjectAsDialog::browse() {
    const QString path = QFileDialog::getExistingDirectory(
        this, tr("Select project parent directory"), parent_directory());
    if (!path.isEmpty()) location_->setText(path);
}

void SaveProjectAsDialog::refresh_validation() {
    const auto result = ProjectPathValidator::validate_save_as(
        project_name(), parent_directory(), source_root_);
    final_path_->setText(QDir::toNativeSeparators(result.final_path));
    validation_->setText(result.error.isEmpty()
                             ? tr("Ready to create the project copy.")
                             : result.error);
    validation_->setProperty("error", !result.error.isEmpty());
    save_->setEnabled(result.error.isEmpty());
}

void SaveProjectAsDialog::accept() {
    const auto result = ProjectPathValidator::validate_save_as(
        project_name(), parent_directory(), source_root_);
    if (!result) return;
    QSettings("Jocelin", "DeepLearningStudio")
        .setValue("lastProjectDirectory", parent_directory());
    QDialog::accept();
}

} // namespace studio
