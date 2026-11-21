#include "app/main_window.h"
#include "modules/diagnostics/activity_log.h"
#include "modules/execution/application/execution_module.h"
#include "modules/model_management/application/model_actions.h"
#include "modules/workflow/ui/node_workspace.h"
#include "modules/project/ui/project_module.h"
#include "modules/settings/ui/settings_module.h"
#include "modules/workflow/ui/workflow_module.h"
#include "modules/settings/application/ui_language.h"
#include "common/ui/ui_theme.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFontDatabase>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QResizeEvent>
#include <QScreen>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>

namespace studio {
namespace {

QFont studio_ui_font() {
    const QStringList installed = QFontDatabase().families();
    const bool chinese = ui_language() == "zh_CN";
    const QStringList preferred_fonts = chinese
        ? QStringList{"Microsoft YaHei UI", "Microsoft YaHei", "Segoe UI", "Noto Sans"}
        : QStringList{"Segoe UI Variable Text", "Segoe UI", "Microsoft YaHei UI", "Noto Sans"};
    for (const QString& preferred : preferred_fonts) {
        if (!installed.contains(preferred, Qt::CaseInsensitive)) continue;
        QFont font(preferred);
        font.setPointSizeF(8.5);
        font.setStyleStrategy(QFont::PreferAntialias);
        return font;
    }
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPointSizeF(8.5);
    return font;
}

} // namespace

MainWindow::MainWindow() {
    QApplication::setFont(studio_ui_font());
    apply_ui_theme(*qApp);
    setFont(QApplication::font());
    setWindowIcon(QIcon(":/studio/studio.ico"));
    buildUi();
    const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
    resize(qMin(1440, available.width() - 32), qMin(900, available.height() - 32));
    setMinimumSize(760, 480);
    restoreShellState();
    workflow_->bindActiveWorkflow();
    project_module_->updateRecentProjects();
    workflow_->refreshValidation();
    project_module_->show_home();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi() {
    pages_ = new QStackedWidget(this);
    setCentralWidget(pages_);
    workflow_toolbar_ = addToolBar(tr("Project and workflow"));
    workflow_toolbar_->setObjectName("workflowToolbar");
    workflow_toolbar_->setMovable(false);
    workflow_toolbar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    workflow_toolbar_->setIconSize(QSize(20, 20));
    home_toolbar_ = addToolBar(tr("Project commands"));
    home_toolbar_->setObjectName("homeToolbar");
    home_toolbar_->setMovable(false);
    home_toolbar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* file_menu = menuBar()->addMenu(tr("File"));
    file_menu->setObjectName("fileMenu");
    edit_menu_ = menuBar()->addMenu(tr("Edit"));
    edit_menu_->setObjectName("editMenu");
    auto* view_menu = menuBar()->addMenu(tr("View"));
    auto* settings_menu = menuBar()->addMenu(tr("Settings"));
    settings_menu->setObjectName("settingsMenu");

    context_ = new QLabel(statusBar());
    runtime_ = new QLabel(tr("Ready"), statusBar());
    context_->setObjectName("workflowStatusContext");
    runtime_->setObjectName("runtimeStatus");
    context_->setMinimumWidth(0);
    context_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    runtime_->setProperty("status", "ready");
    runtime_->hide();
    statusBar()->addWidget(context_, 1);
    statusBar()->addPermanentWidget(runtime_);

    log_ = new ActivityLog(this);
    log_->mount(*view_menu);
    project_ = std::make_unique<ProjectManager>();
    project_module_ = std::make_unique<ProjectModule>(*project_, *this);
    workflow_ = std::make_unique<WorkflowModule>(*project_, *this, *log_);
    project_module_->mount(*file_menu, *home_toolbar_, *workflow_toolbar_);
    project_dock_ = project_module_->dock();
    view_menu->addAction(project_dock_->toggleViewAction());
    workflow_->mount(*workflow_toolbar_, *edit_menu_, *view_menu);
    library_dock_ = workflow_->library_dock();
    property_dock_ = workflow_->property_dock();
    auto& workspace = workflow_->node_workspace();
    models_ = std::make_unique<ModelActions>(*project_, this);
    execution_ = std::make_unique<ExecutionModule>(*project_, workspace, this);

    project_module_->confirm_annotation_transition = [this] {
        return workflow_->node_workspace().confirm_annotation_transition();
    };
    project_module_->before_switch = [this] { workflow_->unbindWorkflowViews(); };
    project_module_->after_switch = [this] { workflow_->bindActiveWorkflow(); };
    project_module_->show_workflow = [this] { workflow_->showWorkflowWorkspace(); };
    project_module_->changed = [this] { workflow_->refreshProjectUi(); };
    project_module_->log = [this](const QString& text) { log_->append_log(text); };
    project_module_->busy = [this] { return execution_->is_running(); };
    workflow_->busy = [this] { return execution_->is_running(); };
    workflow_->project_changed = [this] { project_module_->refresh(); };
    workflow_->request_training = [this](int id) { execution_->start_training(id); };
    models_->log = [this](const QString& text) { log_->append_log(text); };
    models_->status = [this](const QString& text, int timeout) { show_message(text, timeout); };
    models_->models_changed = [&workspace] { workspace.refresh_models(); };
    models_->mount(workspace);
    execution_->log = [this](const QString& text) { log_->append_log(text); };
    execution_->show_log = [this] { log_->show_log(); };
    execution_->status = [this](const QString& text, int timeout) { show_message(text, timeout); };
    execution_->editing_changed = [this](bool enabled) {
        project_module_->set_editing_enabled(enabled);
        workflow_->setEditingEnabled(enabled);
        set_runtime_status(enabled ? tr("Ready") : tr("Running"), enabled ? "ready" : "running");
    };
    execution_->runtime_status = [this](const QString& text, const char* state) { set_runtime_status(text, state); };
    execution_->changed = [this] { workflow_->refreshValidation(); workflow_->refreshProjectUi(); };

    auto* spacer = new QWidget(home_toolbar_);
    spacer->setObjectName("homeToolbarSpacer");
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    home_toolbar_->addWidget(spacer);
    (new SettingsModule(this))->mount(*settings_menu, *home_toolbar_);
    resizeDocks({project_dock_, property_dock_}, {196, 260}, Qt::Horizontal);
}

void MainWindow::mount_page(QWidget* page) { pages_->addWidget(page); }
QDockWidget* MainWindow::mount_dock(const QString& title, const char* id, QWidget* content, Qt::DockWidgetArea area) {
    auto* dock = new QDockWidget(title, this);
    dock->setObjectName(id);
    dock->setWidget(content);
    mount_dock(area, dock);
    return dock;
}
void MainWindow::mount_dock(Qt::DockWidgetArea area, QDockWidget* dock) {
    addDockWidget(area, dock);
    docks_.append(dock);
}
void MainWindow::hide_docks() { for (auto* dock : docks_) dock->hide(); }
bool MainWindow::is_current_page(QWidget* page) const { return pages_->currentWidget() == page; }
void MainWindow::show_home(QWidget* page) {
    pages_->setCurrentWidget(page);
    clear_message();
    workflow_toolbar_->hide(); home_toolbar_->show(); hide_docks();
    edit_menu_->setEnabled(false);
    context_->clear();
}
void MainWindow::show_workflow(QWidget* page) {
    pages_->setCurrentWidget(page);
    clear_message(); workflow_toolbar_->show(); home_toolbar_->hide();
    edit_menu_->setEnabled(true);
    project_dock_->show(); library_dock_->show(); library_dock_->raise();
    property_dock_->setVisible(width() >= 1200);
}
void MainWindow::show_task(QWidget* page) {
    pages_->setCurrentWidget(page);
    workflow_toolbar_->show(); home_toolbar_->hide(); hide_docks();
    edit_menu_->setEnabled(false);
}
void MainWindow::show_message(const QString& text, int timeout) { statusBar()->showMessage(text, timeout); }
void MainWindow::clear_message() { statusBar()->clearMessage(); }
void MainWindow::set_context_status(const QString& text) { context_->setText(text); }
void MainWindow::set_runtime_status(const QString& text, const char* state) {
    runtime_->setText(text); runtime_->setProperty("status", state);
    runtime_->setVisible(QByteArray(state) != "ready");
    runtime_->style()->unpolish(runtime_); runtime_->style()->polish(runtime_);
}

void MainWindow::restoreShellState() {
    QSettings settings("Jocelin", "DeepLearningStudio");
    const QByteArray geometry = settings.value("shell/geometry").toByteArray();
    const QByteArray state = settings.value("shell/docks").toByteArray();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
    if (!state.isEmpty()) restoreState(state);

    bool visible = false;
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->availableGeometry().intersects(frameGeometry())) {
            visible = true;
            break;
        }
    }
    if (!visible) {
        const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
        resize(qMin(1440, available.width() - 32), qMin(900, available.height() - 32));
        move(available.center() - rect().center());
    }
    normalizeDockState();
}

void MainWindow::normalizeDockState() {
    // Project navigation and node creation must never restore as overlapping
    // floating palettes. Tabs preserve canvas width on small displays.
    for (QDockWidget* dock : {project_dock_, library_dock_}) {
        if (dock->isFloating()) dock->setFloating(false);
        addDockWidget(Qt::LeftDockWidgetArea, dock);
    }
    tabifyDockWidget(project_dock_, library_dock_);
    project_dock_->raise();
    if (property_dock_->isFloating()) property_dock_->setFloating(false);
    addDockWidget(Qt::RightDockWidgetArea, property_dock_);
    resizeDocks({project_dock_, property_dock_}, {196, 260}, Qt::Horizontal);
}


void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (workflow_) {
        workflow_->adapt_to_resize(event);
    }
}
void MainWindow::closeEvent(QCloseEvent* event) {
    if (!project_module_->can_close()) { event->ignore(); return; }
    QSettings settings("Jocelin", "DeepLearningStudio");
    settings.setValue("shell/geometry", saveGeometry());
    settings.setValue("shell/docks", saveState());
    event->accept();
}
} // namespace studio
