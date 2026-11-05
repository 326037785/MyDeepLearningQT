#include "project_module.h"
#include "main_window.h"
#include "project_home.h"
#include "recent_project_store.h"
#include "new_project_dialog.h"
#include "save_project_as_dialog.h"
#include "icon_provider.h"
#include <QAction>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QFormLayout>
#include <QFontMetrics>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolBar>
#include <QVBoxLayout>

namespace studio {

ProjectModule::ProjectModule(ProjectManager& project, MainWindow& shell)
    : QObject(&shell), project_(project), shell_(shell) {
    home_ = new ProjectHome(&shell);
    shell_.mount_page(home_);
    home_->create_project = [this] { newProject(); };
    home_->open_project = [this] { openProject(); };
    home_->open_recent = [this](const QString& path) { openProject(path); };
    home_->delete_project = [this](const QString& path) { delete_project(path); };
    create_panel();
}

void ProjectModule::mount(QMenu& menu, QToolBar& home_bar, QToolBar& workflow_bar) {
    const auto action = [this](const QString& text, const char* id, auto callback) {
        auto* value = new QAction(text, this);
        value->setObjectName(id);
        connect(value, &QAction::triggered, this, callback);
        actions_.push_back(value);
        return value;
    };
    auto* home = action(tr("Home"), "projectHomeAction", [this] { show_home(); });
    home->setIcon(shell_.style()->standardIcon(QStyle::SP_DirHomeIcon));
    workflow_bar.addAction(home);
    workflow_bar.addSeparator();
    auto* create = action(tr("New Project"), "newProjectAction", [this] { newProject(); });
    create->setShortcut(QKeySequence::New);
    create->setIcon(IconProvider::icon("new-project"));
    auto* open = action(tr("Open Project"), "openProjectAction", [this] { openProject(); });
    open->setShortcut(QKeySequence::Open);
    open->setIcon(IconProvider::icon("open-project"));
    save_ = action(tr("Save"), "saveProjectAction", [this] { saveProject(); });
    save_->setShortcut(QKeySequence::Save);
    save_->setIcon(IconProvider::icon("save"));
    menu.addAction(create);
    menu.addAction(open);
    recent_menu_ = menu.addMenu(tr("Recent Projects"));
    recent_menu_->setObjectName("recentProjectsMenu");
    auto* templates = menu.addMenu(tr("New from Template"));
    auto* segmentation = action(tr("Semantic segmentation"), "segmentationTemplateAction", [this] { newProjectFromTemplate("sg"); });
    auto* classification = action(tr("Image classification"), "classificationTemplateAction", [this] { newProjectFromTemplate("cls"); });
    templates->addAction(segmentation);
    templates->addAction(classification);
    menu.addSeparator();
    menu.addAction(save_);
    save_as_ = action(tr("Save As..."), "saveProjectAsAction", [this] { saveProjectAs(); });
    save_as_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    save_as_->setIcon(IconProvider::icon("save"));
    menu.addAction(save_as_);
    for (auto* command : {create, open, save_}) workflow_bar.addAction(command);
    workflow_bar.addSeparator();
    for (auto* command : {create, open, save_, save_as_}) home_bar.addAction(command);
    menu.addSeparator();
    auto* properties = action(tr("Project Properties..."), "projectPropertiesAction", [this] {
        show_workflow(); dock_->show(); dock_->raise(); project_name_->setFocus();
    });
    menu.addAction(properties);
    auto* close = action(tr("Close Project"), "closeProjectAction", [this] { close_project(); });
    menu.addAction(close);
    menu.addAction(home);
    menu.addSeparator();
    auto* exit = menu.addAction(tr("Exit"));
    connect(exit, &QAction::triggered, &shell_, &QWidget::close);
}

void ProjectModule::create_panel() {
    auto* project_panel = new QWidget(&shell_);
    project_panel->setMinimumWidth(0);
    auto* project_layout = new QVBoxLayout(project_panel);
    auto* metadata_group = new QGroupBox(tr("Project metadata"), project_panel);
    auto* metadata_form = new QFormLayout(metadata_group);
    project_name_ = new QLineEdit(metadata_group);
    project_name_->setMinimumWidth(0);
    project_name_->setObjectName("projectNameEdit");
    project_description_ = new QLineEdit(metadata_group);
    project_description_->setMinimumWidth(0);
    project_description_->setObjectName("projectDescriptionEdit");
    project_root_ = new QLabel(metadata_group);
    project_root_->setWordWrap(false);
    metadata_form->addRow(tr("Name"), project_name_);
    metadata_form->addRow(tr("Description"), project_description_);
    metadata_form->addRow(tr("Location"), project_root_);
    project_layout->addWidget(metadata_group);
    auto* workflow_hint = new QLabel(tr("Manage workflows from the tabs above the canvas. Double-click a tab to rename it."), project_panel);
    workflow_hint->setWordWrap(true);
    workflow_hint->setProperty("muted", true);
    project_layout->addWidget(workflow_hint);
    project_layout->addStretch();
    connect(project_name_, &QLineEdit::editingFinished, this, [this] {
        try { project_.set_project_name(project_name_->text()); refresh(); }
        catch (const std::exception& error) { QMessageBox::warning(&shell_, tr("Invalid project name"), error.what()); refresh(); }
    });
    connect(project_description_, &QLineEdit::editingFinished, this, [this] {
        project_.set_project_description(project_description_->text()); refresh();
    });
    dock_ = shell_.mount_dock(tr("Project"), "projectDock", project_panel, Qt::LeftDockWidgetArea);
    dock_->setMinimumWidth(170);
    dock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
}

void ProjectModule::refresh() {
    set_editing_enabled(!busy || !busy());
    const QSignalBlocker name_block(project_name_);
    const QSignalBlocker description_block(project_description_);
    project_name_->setText(project_.metadata().name);
    project_description_->setText(project_.metadata().description);
    const QString root = project_.has_storage() ? project_.root_directory() : tr("Not saved");
    project_root_->setText(QFontMetrics(project_root_->font()).elidedText(root, Qt::ElideMiddle, 190));
    project_root_->setToolTip(root);
    const QString title = QStringLiteral("DeepLearning Workflow Studio");
    shell_.setWindowTitle(project_.has_storage() || project_.is_dirty()
        ? project_.metadata().name + " - " + title + (project_.is_dirty() ? " *" : "") : title);
}

void ProjectModule::set_editing_enabled(bool enabled) {
    for (auto* action : actions_) action->setEnabled(enabled);
    save_->setEnabled(enabled && (project_.has_storage() || project_.is_dirty()));
    save_as_->setEnabled(save_->isEnabled());
    recent_menu_->setEnabled(enabled && !recent_menu_->actions().isEmpty());
    home_->setEnabled(enabled);
    dock_->setEnabled(enabled);
}

void ProjectModule::refreshProjectUi() { refresh(); if (changed) changed(); }
WorkflowGraph& ProjectModule::graph() { return project_.active_workflow().document.graph; }
std::map<int, QPointF>& ProjectModule::positions() { return project_.active_workflow().document.positions; }
void ProjectModule::show_home() { home_->refresh_recent_projects(); shell_.show_home(home_); }
void ProjectModule::close_project() {
    if (!confirmProjectTransition()) return;
    before_switch(); project_.new_project(); after_switch(); show_home();
}
bool ProjectModule::can_close() {
    if (busy && busy()) {
        QMessageBox::information(&shell_, tr("Task running"),
            tr("Wait for the current operation to finish before closing."));
        return false;
    }
    return confirmProjectTransition();
}

void ProjectModule::updateRecentProjects(const QString& project_file) {
    RecentProjectStore store;
    const QStringList recent = project_file.isEmpty()
        ? store.prune() : store.add(project_file);
    recent_menu_->clear();
    for (const QString& path : recent) {
        auto* action = recent_menu_->addAction(path);
        connect(action, &QAction::triggered, this, [this, path] { openProject(path); });
    }
    recent_menu_->setEnabled(!recent.isEmpty() && (!busy || !busy()));
    if (home_) home_->refresh_recent_projects();
}

bool ProjectModule::confirmProjectTransition() {
    if (busy && busy()) return false;
    if (!project_.is_dirty()) return true;
    const auto answer = QMessageBox::warning(
        &shell_, tr("Unsaved project"), tr("Save changes to %1 before continuing?").arg(project_.metadata().name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return saveProject();
    return true;
}

bool ProjectModule::saveProject() {
    if (busy && busy()) return false;
    QString root = project_.root_directory();
    if (root.isEmpty()) return saveProjectAs();
    try {
        project_.save_project(root);
        refreshProjectUi();
        updateRecentProjects(project_.project_file());
        log(tr("Project saved: %1").arg(project_.project_file()));
        shell_.show_message(tr("Project saved"), 4000);
        return true;
    } catch (const std::exception& error) {
        QMessageBox::critical(&shell_, tr("Save failed"), QString::fromUtf8(error.what()));
        return false;
    }
}

bool ProjectModule::saveProjectAs() {
    if (busy && busy()) return false;
    SaveProjectAsDialog dialog(
        project_.metadata().name, project_.root_directory(), &shell_);
    if (dialog.exec() != QDialog::Accepted) return false;
    const size_t external_count = project_.external_reference_count();
    if (external_count > 0) {
        QMessageBox::information(
            &shell_, tr("Shared external resources"),
            tr("This project contains %1 external resource reference(s). They will remain shared and will not be copied.")
                .arg(external_count));
    }
    try {
        const QString old_project = project_.project_file();
        project_.save_project_as(dialog.project_name(), dialog.parent_directory());
        refreshProjectUi();
        if (!old_project.isEmpty()) updateRecentProjects(old_project);
        updateRecentProjects(project_.project_file());
        log(tr("Project copy created: %1").arg(project_.project_file()));
        shell_.show_message(tr("Project saved as an independent copy"), 4000);
        return true;
    } catch (const std::exception& error) {
        QMessageBox::critical(&shell_, tr("Save Project As failed"), QString::fromUtf8(error.what()));
        return false;
    }
}

void ProjectModule::newProject() {
    newProjectFromTemplate({});
}

void ProjectModule::newProjectFromTemplate(const QString& template_task) {
    if (!confirmProjectTransition()) return;
    NewProjectDialog dialog(&shell_);
    if (dialog.exec() != QDialog::Accepted) return;
    const QString name = dialog.projectName();
    const QString location = dialog.projectLocation();
    const QString desc = dialog.projectDescription();
    before_switch();
    try {
        project_.new_project(name);
        if (!desc.isEmpty()) project_.set_project_description(desc);
        if (!template_task.isEmpty()) {
            auto& template_graph = graph();
            const int source_node_id = template_graph.add_node(NodeKind::DatasetInput);
            const int node_id = template_graph.add_task_node({template_task.toStdString()});
            template_graph.connect(source_node_id, 0, node_id, 0);
            positions()[source_node_id] = QPointF(-300, -40);
            positions()[node_id] = QPointF(80, -40);
            project_.mark_active_dirty();
        }
        project_.save_project(location);
        after_switch();
        show_workflow();
        updateRecentProjects(project_.project_file());
        log(tr("Project created: %1").arg(project_.project_file()));
        shell_.show_message(template_task.isEmpty() ? tr("Project created") : tr("Project created from template"), 4000);
    } catch (const std::exception& error) {
        after_switch();
        QMessageBox::critical(&shell_, tr("Create project failed"), QString::fromUtf8(error.what()));
    }
}

void ProjectModule::openProject(const QString& requested_path) {
    if (!confirmProjectTransition()) return;
    QString path = requested_path;
    if (path.isEmpty()) path = QFileDialog::getOpenFileName(&shell_, tr("Open project"), {}, tr("DeepLearning Project (project.json)"));
    if (path.isEmpty()) return;
    before_switch();
    try {
        const auto missing = project_.open_project(path);
        if (!missing.empty()) {
            QStringList details;
            for (const auto& issue : missing) {
                const QString owner = tr("Workflow %1 / TaskNode %2").arg(issue.workflow_id).arg(issue.node_id);
                if (issue.model_id.isEmpty()) {
                    details.push_back(tr("%1: model_list.json is missing; the ModelList is empty.").arg(owner));
                } else {
                    details.push_back(tr("%1 / model %2: missing weights %3%4")
                        .arg(owner, issue.model_id, issue.weight_ids.join(", "),
                             issue.all_weights_missing ? tr(" (all model weights are missing)") : QString{}));
                }
                details.append(issue.paths);
            }
            QMessageBox prompt(QMessageBox::Warning, tr("Missing model assets"),
                tr("Model assets no longer exist. Existing inference results depend on these models.\n\n"
                   "Clean up the missing weights and their inference results? Models whose weights are all missing "
                   "will be deleted. A missing ModelList will remain empty and its orphaned inference results "
                   "will be cleared.\n\nCancel keeps Project data unchanged; unavailable weights cannot be used for training or inference."),
                QMessageBox::Yes | QMessageBox::Cancel, &shell_);
            prompt.setDefaultButton(QMessageBox::Cancel);
            prompt.setInformativeText(details.join('\n'));
            if (prompt.exec() == QMessageBox::Yes) {
                const size_t active = project_.active_index();
                try {
                    for (const auto& issue : missing) {
                        for (size_t index = 0; index < project_.workflows().size(); ++index) {
                            if (project_.workflows()[index].id == issue.workflow_id) {
                                project_.set_active_index(index);
                                break;
                            }
                        }
                        if (issue.model_id.isEmpty()) {
                            project_.remove_models(issue.node_id);
                        } else if (issue.all_weights_missing) {
                            project_.remove_models(issue.node_id, {issue.model_id});
                        } else {
                            for (const QString& weight_id : issue.weight_ids) {
                                project_.remove_models(issue.node_id, {issue.model_id}, weight_id);
                            }
                        }
                    }
                } catch (const std::exception& error) {
                    QMessageBox::warning(&shell_, tr("Cannot clean up model assets"), QString::fromUtf8(error.what()));
                }
                project_.set_active_index(active);
            }
        }
        after_switch();
        show_workflow();
        updateRecentProjects(project_.project_file());
        log(tr("Project opened: %1").arg(project_.project_file()));
    } catch (const std::exception& error) {
        after_switch();
        QMessageBox::critical(&shell_, tr("Open rejected"), QString::fromUtf8(error.what()));
    }
}

void ProjectModule::delete_project(const QString& path) {
    if (busy && busy()) return;
    const QFileInfo input(path);
    const QString project_file = input.isDir()
        ? QDir(input.absoluteFilePath()).filePath("project.json")
        : input.absoluteFilePath();
    const QFileInfo manifest(project_file);
    if (!manifest.isFile()) {
        QMessageBox::warning(&shell_, tr("Cannot delete project"), tr("Project file is missing:\n%1").arg(project_file));
        RecentProjectStore().remove(path);
        home_->refresh_recent_projects();
        return;
    }
    const QString root = manifest.absolutePath();
    if (QDir(root).isRoot()) {
        QMessageBox::warning(&shell_, tr("Cannot delete project"), tr("Refusing to delete a filesystem root."));
        return;
    }
    if (QMessageBox::question(
            &shell_, tr("Delete project"),
            tr("Permanently delete this Project and all files under:\n\n%1\n\nThis cannot be recovered.").arg(root),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    const bool deleting_current = project_.has_storage() &&
        QFileInfo(project_.project_file()).absoluteFilePath().compare(project_file, Qt::CaseInsensitive) == 0;
    if (!QDir(root).removeRecursively()) {
        QMessageBox::warning(&shell_, tr("Cannot delete project"), tr("Cannot delete Project directory:\n%1").arg(root));
        return;
    }
    RecentProjectStore().remove(project_file);
    if (deleting_current) {
        before_switch();
        project_.new_project();
        after_switch();
    }
    home_->refresh_recent_projects();
    show_home();
}

} // namespace studio
