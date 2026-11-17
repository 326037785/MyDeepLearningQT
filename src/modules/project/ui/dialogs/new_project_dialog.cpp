#include "modules/project/ui/dialogs/new_project_dialog.h"
#include "modules/project/application/project_path_validator.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace studio {

NewProjectDialog::NewProjectDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("New Project"));
    setMinimumWidth(520);
    auto* root = new QVBoxLayout(this);

    auto* heading = new QLabel(tr("Create a new project"), this);
    heading->setProperty("heading", true);
    root->addWidget(heading);

    auto* intro = new QLabel(
        tr("Choose a name and parent location. The project directory and project.json "
           "will be created immediately."), this);
    intro->setWordWrap(true);
    root->addWidget(intro);
    root->addSpacing(8);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    name_ = new QLineEdit(this);
    name_->setObjectName("newProjectNameEdit");
    name_->setPlaceholderText(tr("My Deep Learning Project"));
    form->addRow(tr("Name"), name_);

    // Location row: line edit + browse button
    auto* locationRow = new QWidget(this);
    auto* locationLayout = new QHBoxLayout(locationRow);
    locationLayout->setContentsMargins(0, 0, 0, 0);
    location_ = new QLineEdit(locationRow);
    location_->setObjectName("newProjectLocationEdit");
    // Default: last used directory from settings
    const QString lastDir = QSettings("Jocelin", "DeepLearningStudio")
                                .value("lastProjectDirectory")
                                .toString();
    location_->setPlaceholderText(tr("C:\\Projects"));
    if (!lastDir.isEmpty()) location_->setText(lastDir);
    auto* browse = new QPushButton("...", locationRow);
    browse->setFixedWidth(40);
    browse->setToolTip(tr("Browse for project directory"));
    locationLayout->addWidget(location_, 1);
    locationLayout->addWidget(browse);
    form->addRow(tr("Location"), locationRow);

    auto* descEdit = new QLineEdit(this);
    descEdit->setObjectName("newProjectDescriptionEdit");
    descEdit->setPlaceholderText(tr("Optional project description"));
    form->addRow(tr("Description"), descEdit);
    description_ = descEdit;

    root->addLayout(form);
    root->addSpacing(12);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));
    buttons->setObjectName("newProjectButtons");
    root->addWidget(buttons);

    connect(browse, &QPushButton::clicked, this, &NewProjectDialog::browseLocation);
    connect(buttons, &QDialogButtonBox::accepted, this, &NewProjectDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString NewProjectDialog::projectName() const { return name_->text().trimmed(); }
QString NewProjectDialog::projectLocation() const {
    return QDir(location_->text().trimmed()).filePath(projectName());
}
QString NewProjectDialog::projectDescription() const { return description_->text().trimmed(); }

QString NewProjectDialog::validateName(const QString& name) {
    return ProjectPathValidator::validate_name(name);
}

QString NewProjectDialog::validateProjectTarget(const QString& target_path) {
    const QFileInfo target(QDir::cleanPath(target_path));
    return ProjectPathValidator::validate_save_as(
               target.fileName(), target.absolutePath()).error;
}

void NewProjectDialog::browseLocation() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select project location"), location_->text());
    if (!dir.isEmpty()) location_->setText(dir);
}

void NewProjectDialog::accept() {
    const QString name = projectName();
    const QString name_error = validateName(name);
    if (!name_error.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid name"), name_error);
        name_->setFocus();
        return;
    }

    const QString location = projectLocation();
    const QString location_error = validateProjectTarget(location);
    if (!location_error.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid location"), location_error);
        location_->setFocus();
        return;
    }

    // Remember the parent directory for next time
    QSettings("Jocelin", "DeepLearningStudio")
        .setValue("lastProjectDirectory", location_->text().trimmed());

    if (!QDir().mkpath(location)) {
        QMessageBox::warning(this, tr("Create failed"),
                             tr("Unable to create the project directory."));
        location_->setFocus();
        return;
    }

    QDialog::accept();
}

} // namespace studio
